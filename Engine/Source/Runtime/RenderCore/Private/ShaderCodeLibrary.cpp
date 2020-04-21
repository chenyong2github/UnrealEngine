// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
ShaderCodeLibrary.cpp: Bound shader state cache implementation.
=============================================================================*/

#include "ShaderCodeLibrary.h"
#include "ShaderCodeArchive.h"
#include "Shader.h"
#include "Algo/Replace.h"
#include "Containers/StringView.h"
#include "Misc/SecureHash.h"
#include "Misc/Paths.h"
#include "Math/UnitConversion.h"
#include "HAL/FileManagerGeneric.h"
#include "HAL/LowLevelMemTracker.h"
#include "HAL/PlatformSplash.h"
#include "Misc/ScopeLock.h"
#include "Misc/ScopeRWLock.h"
#include "Misc/StringBuilder.h"
#include "String/ParseTokens.h"
#include "Async/AsyncFileHandle.h"
#include "PipelineFileCache.h"
#include "Interfaces/IPluginManager.h"
#include "Hash/CityHash.h"
#include "Containers/HashTable.h"
#include "FileCache/FileCache.h"

#include "ShaderPipelineCache.h"
#include "Misc/FileHelper.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CommandLine.h"
#include "ProfilingDebugging/LoadTimeTracker.h"

#if WITH_EDITORONLY_DATA
#include "Modules/ModuleManager.h"
#include "Interfaces/IShaderFormat.h"
#include "Interfaces/IShaderFormatModule.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#endif

#if WITH_EDITOR
#include "PakFileUtilities.h"
#endif

// FORT-93125
#define CHECK_SHADER_CREATION (PLATFORM_XBOXONE && WITH_LEGACY_XDK)

DEFINE_LOG_CATEGORY(LogShaderLibrary);

static uint32 GShaderCodeArchiveVersion = 2;
static uint32 GShaderPipelineArchiveVersion = 1;

static FString ShaderExtension = TEXT(".ushaderbytecode");
static FString StableExtension = TEXT(".scl.csv");
static FString PipelineExtension = TEXT(".ushaderpipelines");

int32 GShaderCodeLibrarySeperateLoadingCache = 0;
static FAutoConsoleVariableRef CVarShaderCodeLibrarySeperateLoadingCache(
	TEXT("r.ShaderCodeLibrary.SeperateLoadingCache"),
	GShaderCodeLibrarySeperateLoadingCache,
	TEXT("if > 0, each shader code library has it's own loading cache."),
	ECVF_Default
);

class FShaderMapResource_SharedCode final : public FShaderMapResource
{
public:
	FShaderMapResource_SharedCode(class FShaderLibraryInstance* InLibraryInstance, int32 InShaderMapIndex);
	virtual ~FShaderMapResource_SharedCode();

	// FRenderResource interface.
	virtual void ReleaseRHI() override;

	// FShaderMapResource interface
	virtual TRefCountPtr<FRHIShader> CreateRHIShader(int32 ShaderIndex) override;
	virtual bool TryRelease() override;
	virtual uint32 GetSizeBytes() const override { return sizeof(*this) + GetAllocatedSize(); }

	class FShaderLibraryInstance* LibraryInstance;
	int32 ShaderMapIndex;
};

static FString GetCodeArchiveFilename(const FString& BaseDir, const FString& LibraryName, FName Platform)
{
	return BaseDir / FString::Printf(TEXT("ShaderArchive-%s-"), *LibraryName) + Platform.ToString() + ShaderExtension;
}

static FString GetStableInfoArchiveFilename(const FString& BaseDir, const FString& LibraryName, FName Platform)
{
	return BaseDir / FString::Printf(TEXT("ShaderStableInfo-%s-"), *LibraryName) + Platform.ToString() + StableExtension;
}

static FString GetPipelinesArchiveFilename(const FString& BaseDir, const FString& LibraryName, FName Platform)
{
	return BaseDir / FString::Printf(TEXT("ShaderArchive-%s-"), *LibraryName) + Platform.ToString() + PipelineExtension;
}

static FString GetShaderCodeFilename(const FString& BaseDir, const FString& LibraryName, FName Platform)
{
	return BaseDir / FString::Printf(TEXT("ShaderCode-%s-"), *LibraryName) + Platform.ToString() + ShaderExtension;
}

static FString GetShaderDebugFolder(const FString& BaseDir, const FString& LibraryName, FName Platform)
{
	return BaseDir / FString::Printf(TEXT("ShaderDebug-%s-"), *LibraryName) + Platform.ToString();
}

FORCEINLINE FName ParseFNameCached(const FStringView& Src, TMap<uint32,FName>& NameCache)
{
	uint32 SrcHash = CityHash32(reinterpret_cast<const char*>(Src.GetData()), Src.Len() * sizeof(TCHAR));
	if (FName* Name = NameCache.Find(SrcHash))
	{
		return *Name;
	}
	else
	{
		return NameCache.Emplace(SrcHash, FName(Src.Len(), Src.GetData()));
	}
}

static void AppendFNameAsUTF8(FAnsiStringBuilderBase& Out, const FName& InName)
{
	if (!InName.TryAppendAnsiString(Out))
	{
		TStringBuilder<128> WideName;
		InName.AppendString(WideName);
		Out << TCHAR_TO_UTF8(WideName.ToString());
	}
}

static void AppendSanitizedFNameAsUTF8(FAnsiStringBuilderBase& Out, const FName& InName, ANSICHAR Delim)
{
	const int32 Offset = Out.Len();
	AppendFNameAsUTF8(Out, InName);
	Algo::Replace(MakeArrayView(Out).Slice(Offset, Out.Len() - Offset), Delim, ' ');
}

static void AppendSanitizedFName(FStringBuilderBase& Out, const FName& InName, TCHAR Delim)
{
	const int32 Offset = Out.Len();
	InName.AppendString(Out);
	Algo::Replace(MakeArrayView(Out).Slice(Offset, Out.Len() - Offset), Delim, TEXT(' '));
}

FString FCompactFullName::ToString() const
{
	TStringBuilder<256> RetString;
	AppendString(RetString);
	return FString(FStringView(RetString));
}

void FCompactFullName::AppendString(FStringBuilderBase& Out) const
{
	const int32 ObjectClassAndPathCount = ObjectClassAndPath.Num();
	if (!ObjectClassAndPathCount)
	{
		Out << TEXT("empty");
	}
	else
	{
		for (int32 NameIdx = 0; NameIdx < ObjectClassAndPathCount; NameIdx++)
		{
			Out << ObjectClassAndPath[NameIdx];
			if (NameIdx == 0)
			{
				Out << TEXT(' ');
			}
			else if (NameIdx < ObjectClassAndPathCount - 1)
			{
				if (NameIdx == ObjectClassAndPathCount - 2)
				{
					Out << TEXT('.');
				}
				else
				{
					Out << TEXT('/');
				}
			}
		}
	}
}

void FCompactFullName::AppendString(FAnsiStringBuilderBase& Out) const
{
	const int32 ObjectClassAndPathCount = ObjectClassAndPath.Num();
	if (!ObjectClassAndPathCount)
	{
		Out << "empty";
	}
	else
	{
		for (int32 NameIdx = 0; NameIdx < ObjectClassAndPathCount; NameIdx++)
		{
			AppendFNameAsUTF8(Out, ObjectClassAndPath[NameIdx]);
			if (NameIdx == 0)
			{
				Out << ' ';
			}
			else if (NameIdx < ObjectClassAndPathCount - 1)
			{
				if (NameIdx == ObjectClassAndPathCount - 2)
				{
					Out << '.';
				}
				else
				{
					Out << '/';
				}
			}
		}
	}
}

void FCompactFullName::ParseFromString(const FStringView& InSrc)
{
	TArray<FStringView, TInlineAllocator<64>> Fields;
	UE::String::ParseTokensMultiple(InSrc.TrimStartAndEnd(), {TEXT(' '), TEXT('.'), TEXT('/'), TEXT('\t')},
		[&Fields](FStringView Field) { if (!Field.IsEmpty()) { Fields.Add(Field); } });
	if (Fields.Num() == 1 && Fields[0] == TEXT("empty"_SV))
	{
		Fields.Empty();
	}
	ObjectClassAndPath.Empty(Fields.Num());
	for (const FStringView& Item : Fields)
	{
		ObjectClassAndPath.Emplace(Item);
	}
}

uint32 GetTypeHash(const FCompactFullName& A)
{
	uint32 Hash = 0;
	for (const FName& Name : A.ObjectClassAndPath)
	{
		Hash = HashCombine(Hash, GetTypeHash(Name));
	}
	return Hash;
}

void FixupUnsanitizedNames(const FString& Src, TArray<FString>& OutFields) 
{
	FString NewSrc(Src);

	int32 ParenOpen = -1;
	int32 ParenClose = -1;

	if (NewSrc.FindChar(TCHAR('('), ParenOpen) && NewSrc.FindChar(TCHAR(')'), ParenClose) && ParenOpen < ParenClose && ParenOpen >= 0 && ParenClose >= 0)
	{
		for (int32 Index = ParenOpen + 1; Index < ParenClose; Index++)
		{
			if (NewSrc[Index] == TCHAR(','))
			{
				NewSrc[Index] = ' ';
			}
		}
		OutFields.Empty();
		NewSrc.TrimStartAndEnd().ParseIntoArray(OutFields, TEXT(","), false);
		// allow formats both with and without pipeline hash
		check(OutFields.Num() == 11 || OutFields.Num() == 12);
	}
}

void FStableShaderKeyAndValue::ComputeKeyHash()
{
	KeyHash = GetTypeHash(ClassNameAndObjectPath);

	KeyHash = HashCombine(KeyHash, GetTypeHash(ShaderType));
	KeyHash = HashCombine(KeyHash, GetTypeHash(ShaderClass));
	KeyHash = HashCombine(KeyHash, GetTypeHash(MaterialDomain));
	KeyHash = HashCombine(KeyHash, GetTypeHash(FeatureLevel));

	KeyHash = HashCombine(KeyHash, GetTypeHash(QualityLevel));
	KeyHash = HashCombine(KeyHash, GetTypeHash(TargetFrequency));
	KeyHash = HashCombine(KeyHash, GetTypeHash(TargetPlatform));

	KeyHash = HashCombine(KeyHash, GetTypeHash(VFType));
	KeyHash = HashCombine(KeyHash, GetTypeHash(PermutationId));
	KeyHash = HashCombine(KeyHash, GetTypeHash(PipelineHash));
}

void FStableShaderKeyAndValue::ParseFromString(const FStringView& Src)
{
	TArray<FStringView, TInlineAllocator<12>> Fields;
	UE::String::ParseTokens(Src.TrimStartAndEnd(), TEXT(','), [&Fields](FStringView Field) { Fields.Add(Field); });

	/* disabled, should not be happening since 1H 2018
	if (Fields.Num() > 12)
	{
		// hack fix for unsanitized names, should not occur anymore.
		FixupUnsanitizedNames(Src, Fields);
	}
	*/

	// for a while, accept old .scl.csv without pipelinehash
	check(Fields.Num() == 11 || Fields.Num() == 12);

	int32 Index = 0;
	ClassNameAndObjectPath.ParseFromString(Fields[Index++]);

	ShaderType = FName(Fields[Index++]);
	ShaderClass = FName(Fields[Index++]);
	MaterialDomain = FName(Fields[Index++]);
	FeatureLevel = FName(Fields[Index++]);

	QualityLevel = FName(Fields[Index++]);
	TargetFrequency = FName(Fields[Index++]);
	TargetPlatform = FName(Fields[Index++]);

	VFType = FName(Fields[Index++]);
	PermutationId = FName(Fields[Index++]);

	OutputHash.FromString(Fields[Index++]);

	check(Index == 11);

	if (Fields.Num() == 12)
	{
		PipelineHash.FromString(Fields[Index++]);
	}
	else
	{
		PipelineHash = FSHAHash();
	}

	ComputeKeyHash();
}


void FStableShaderKeyAndValue::ParseFromStringCached(const FStringView& Src, TMap<uint32, FName>& NameCache)
{
	TArray<FStringView, TInlineAllocator<12>> Fields;
	UE::String::ParseTokens(Src.TrimStartAndEnd(), TEXT(','), [&Fields](FStringView Field) { Fields.Add(Field); });

	/* disabled, should not be happening since 1H 2018
	if (Fields.Num() > 11)
	{
		// hack fix for unsanitized names, should not occur anymore.
		FixupUnsanitizedNames(Src, Fields);
	}
	*/

	// for a while, accept old .scl.csv without pipelinehash
	check(Fields.Num() == 11 || Fields.Num() == 12);

	int32 Index = 0;
	ClassNameAndObjectPath.ParseFromString(Fields[Index++]);

	// There is a high level of uniformity on the following FNames, use
	// the local name cache to accelerate lookup
	ShaderType = ParseFNameCached(Fields[Index++], NameCache);
	ShaderClass = ParseFNameCached(Fields[Index++], NameCache);
	MaterialDomain = ParseFNameCached(Fields[Index++], NameCache);
	FeatureLevel = ParseFNameCached(Fields[Index++], NameCache);

	QualityLevel = ParseFNameCached(Fields[Index++], NameCache);
	TargetFrequency = ParseFNameCached(Fields[Index++], NameCache);
	TargetPlatform = ParseFNameCached(Fields[Index++], NameCache);

	VFType = ParseFNameCached(Fields[Index++], NameCache);
	PermutationId = ParseFNameCached(Fields[Index++], NameCache);

	OutputHash.FromString(Fields[Index++]);

	check(Index == 11);

	if (Fields.Num() == 12)
	{
		PipelineHash.FromString(Fields[Index++]);
	}
	else
	{
		PipelineHash = FSHAHash();
	}

	ComputeKeyHash();
}

FString FStableShaderKeyAndValue::ToString() const
{
	FString Result;
	ToString(Result);
	return Result;
}

void FStableShaderKeyAndValue::ToString(FString& OutResult) const
{
	TStringBuilder<384> Out;
	const TCHAR Delim = TEXT(',');

	const int32 ClassNameAndObjectPathOffset = Out.Len();
	ClassNameAndObjectPath.AppendString(Out);
	Algo::Replace(MakeArrayView(Out).Slice(ClassNameAndObjectPathOffset, Out.Len() - ClassNameAndObjectPathOffset), Delim, TEXT(' '));
	Out << Delim;

	AppendSanitizedFName(Out, ShaderType, Delim);
	Out << Delim;
	AppendSanitizedFName(Out, ShaderClass, Delim);
	Out << Delim;

	Out << MaterialDomain << Delim;
	Out << FeatureLevel << Delim;
	Out << QualityLevel << Delim;
	Out << TargetFrequency << Delim;
	Out << TargetPlatform << Delim;
	Out << VFType << Delim;
	Out << PermutationId << Delim;

	Out << OutputHash << Delim;
	Out << PipelineHash;

	OutResult = FStringView(Out);
}

void FStableShaderKeyAndValue::AppendString(FAnsiStringBuilderBase& Out) const
{
	const ANSICHAR Delim = ',';

	const int32 ClassNameAndObjectPathOffset = Out.Len();
	ClassNameAndObjectPath.AppendString(Out);
	Algo::Replace(MakeArrayView(Out).Slice(ClassNameAndObjectPathOffset, Out.Len() - ClassNameAndObjectPathOffset), Delim, ' ');
	Out << Delim;

	AppendSanitizedFNameAsUTF8(Out, ShaderType, Delim);
	Out << Delim;
	AppendSanitizedFNameAsUTF8(Out, ShaderClass, Delim);
	Out << Delim;

	AppendFNameAsUTF8(Out, MaterialDomain);
	Out << Delim;
	AppendFNameAsUTF8(Out, FeatureLevel);
	Out << Delim;
	AppendFNameAsUTF8(Out, QualityLevel);
	Out << Delim;
	AppendFNameAsUTF8(Out, TargetFrequency);
	Out << Delim;
	AppendFNameAsUTF8(Out, TargetPlatform);
	Out << Delim;
	AppendFNameAsUTF8(Out, VFType);
	Out << Delim;
	AppendFNameAsUTF8(Out, PermutationId);
	Out << Delim;

	Out << OutputHash;
	Out << Delim;
	Out << PipelineHash;
}

FString FStableShaderKeyAndValue::HeaderLine()
{
	FString Result;

	const FString Delim(TEXT(","));

	Result += TEXT("ClassNameAndObjectPath");
	Result += Delim;

	Result += TEXT("ShaderType");
	Result += Delim;
	Result += TEXT("ShaderClass");
	Result += Delim;
	Result += TEXT("MaterialDomain");
	Result += Delim;
	Result += TEXT("FeatureLevel");
	Result += Delim;

	Result += TEXT("QualityLevel");
	Result += Delim;
	Result += TEXT("TargetFrequency");
	Result += Delim;
	Result += TEXT("TargetPlatform");
	Result += Delim;

	Result += TEXT("VFType");
	Result += Delim;
	Result += TEXT("Permutation");
	Result += Delim;

	Result += TEXT("OutputHash");
	Result += Delim;

	Result += TEXT("PipelineHash");

	return Result;
}

void FStableShaderKeyAndValue::SetPipelineHash(const FShaderPipeline* Pipeline)
{
	if (LIKELY(Pipeline))
	{
		// cache this?
		FShaderCodeLibraryPipeline LibraryPipeline;
		LibraryPipeline.Initialize(Pipeline);
		LibraryPipeline.GetPipelineHash(PipelineHash); 
	}
	else
	{
		PipelineHash = FSHAHash();
	}
}

void FShaderCodeLibraryPipeline::Initialize(const FShaderPipeline* Pipeline)
{
	check(Pipeline != nullptr);
	for (uint32 Frequency = 0u; Frequency < SF_NumGraphicsFrequencies; ++Frequency)
	{
		if (Pipeline->Shaders[Frequency].IsValid())
		{
			Shaders[Frequency] = Pipeline->Shaders[Frequency]->GetOutputHash();
		}
	}
}

void FShaderCodeLibraryPipeline::GetPipelineHash(FSHAHash& Output)
{
	FSHA1 Hasher;
	Hasher.Update(&Shaders[SF_Vertex].Hash[0], sizeof(FSHAHash));
	Hasher.Update(&Shaders[SF_Pixel].Hash[0], sizeof(FSHAHash));
	Hasher.Update(&Shaders[SF_Geometry].Hash[0], sizeof(FSHAHash));
	Hasher.Update(&Shaders[SF_Hull].Hash[0], sizeof(FSHAHash));
	Hasher.Update(&Shaders[SF_Domain].Hash[0], sizeof(FSHAHash));

	Hasher.Final();
	Hasher.GetHash(&Output.Hash[0]);
}

class FShaderLibraryInstance
{
public:
	static FShaderLibraryInstance* Create(EShaderPlatform InShaderPlatform, const FString& ShaderCodeDir, FString const& InLibraryName)
	{
		FRHIShaderLibraryRef Library;
		if (RHISupportsNativeShaderLibraries(InShaderPlatform))
		{
			Library = RHICreateShaderLibrary(InShaderPlatform, ShaderCodeDir, InLibraryName);
		}

		if (!Library)
		{
			const FName PlatformName = LegacyShaderPlatformToShaderFormat(InShaderPlatform);
			const FString DestFilePath = GetCodeArchiveFilename(ShaderCodeDir, InLibraryName, PlatformName);
			TUniquePtr<FArchive> Ar(IFileManager::Get().CreateFileReader(*DestFilePath));
			if (Ar)
			{
				uint32 Version = 0;
				*Ar << Version;
				if (Version == GShaderCodeArchiveVersion)
				{
					Library = FShaderCodeArchive::Create(InShaderPlatform, *Ar, DestFilePath, ShaderCodeDir, InLibraryName);
					if (Library)
					{
						bool ShaderCodeLibrarySeperateLoadingCacheCommandLineOverride = FParse::Param(FCommandLine::Get(), TEXT("ShaderCodeLibrarySeperateLoadingCache"));;
						if (GShaderCodeLibrarySeperateLoadingCache || ShaderCodeLibrarySeperateLoadingCacheCommandLineOverride)
						{
							TArray<TArray<FString>> FilesToMakeUnique;
							FilesToMakeUnique.AddDefaulted(1);
							FilesToMakeUnique[0].Add(DestFilePath);
							FPlatformFileManager::Get().GetPlatformFile().MakeUniquePakFilesForTheseFiles(FilesToMakeUnique);
						}
					}
				}
			}
		}

		if (!Library)
		{
			return nullptr;
		}

		FShaderLibraryInstance* Instance = new FShaderLibraryInstance();
		Instance->Library = Library;

		const int32 NumResources = Library->GetNumShaderMaps();
		Instance->Resources.AddZeroed(NumResources);

		const int32 NumShaders = Library->GetNumShaders();
		Instance->RHIShaders.AddZeroed(NumShaders);

		INC_DWORD_STAT_BY(STAT_Shaders_ShaderResourceMemory, Instance->GetSizeBytes());

		return Instance;
	}

	~FShaderLibraryInstance()
	{
		// release RHI on all of the resources
		for (FShaderMapResource_SharedCode* Resource : Resources)
		{
			if (Resource)
			{
				Resource->ReleaseRHI();
			}
		}
		
		Library->Teardown();
		DEC_DWORD_STAT_BY(STAT_Shaders_ShaderResourceMemory, GetSizeBytes());
	}

	EShaderPlatform GetPlatform() const { return Library->GetPlatform(); }
	const int32 GetNumResources() const { return Resources.Num(); }
	const int32 GetNumShaders() const { return RHIShaders.Num(); }

	uint32 GetSizeBytes() const
	{
		return sizeof(*this) + RHIShaders.GetAllocatedSize() + Resources.GetAllocatedSize();
	}

	const int32 GetNumShadersForShaderMap(int32 ShaderMapIndex) const
	{
		return Library->GetNumShadersForShaderMap(ShaderMapIndex);
	}

	void PreloadShader(int32 ShaderIndex, FArchive* Ar)
	{
		SCOPED_LOADTIMER(FShaderLibraryInstance_PreloadShader);
		FGraphEventRef Event = Library->PreloadShader(ShaderIndex);
		if (Ar && Event.IsValid())
		{
			FExternalReadCallback ExternalReadCallback = [this, Event](double ReaminingTime)
			{
				return this->OnExternalReadCallback(Event, ReaminingTime);
			};
			Ar->AttachExternalReadDependency(ExternalReadCallback);
		}
	}

	void PreloadShaderMap(int32 ShaderMapIndex, FArchive* Ar)
	{
		SCOPED_LOADTIMER(FShaderLibraryInstance_PreloadShaderMap);
		FGraphEventRef Event = Library->PreloadShaderMap(ShaderMapIndex);
		if (Ar && Event.IsValid())
		{
			FExternalReadCallback ExternalReadCallback = [this, Event](double ReaminingTime)
			{
				return this->OnExternalReadCallback(Event, ReaminingTime);
			};
			Ar->AttachExternalReadDependency(ExternalReadCallback);
		}
	}

	TRefCountPtr<FShaderMapResource_SharedCode> GetResource(int32 ShaderMapIndex)
	{
		FRWScopeLock Locker(ResourceLock, SLT_ReadOnly);
		return Resources[ShaderMapIndex];
	}

	TRefCountPtr<FShaderMapResource_SharedCode> AddOrDeleteResource(FShaderMapResource_SharedCode* Resource)
	{
		const int32 ShaderMapIndex = Resource->ShaderMapIndex;
		TRefCountPtr<FShaderMapResource_SharedCode> OutResource(Resource);

		FRWScopeLock Locker(ResourceLock, SLT_Write);
		FShaderMapResource_SharedCode* PrevResource = Resources[ShaderMapIndex];
		if (!PrevResource)
		{
			Resources[ShaderMapIndex] = Resource;
		}
		else
		{
			OutResource = PrevResource;
		}
		return OutResource;
	}

	bool TryRemoveResource(FShaderMapResource_SharedCode* Resource)
	{
		FRWScopeLock Locker(ResourceLock, SLT_Write);

		if (Resource->GetNumRefs() == 0)
		{
			const int32 ShaderMapIndex = Resource->ShaderMapIndex;
			check(Resources[ShaderMapIndex] == Resource);
			Resources[ShaderMapIndex] = nullptr;
			return true;
		}

		// Another thread found the resource after ref-count was decremented to zero
		return false;
	}

	TRefCountPtr<FRHIShader> GetOrCreateShader(int32 ShaderIndex)
	{
		const int32 LockIndex = ShaderIndex % NumShaderLocks;
		TRefCountPtr<FRHIShader> Shader;
		{
			FRWScopeLock Locker(ShaderLocks[LockIndex], SLT_ReadOnly);
			Shader = RHIShaders[ShaderIndex];
		}
		if (!Shader)
		{
			FRWScopeLock Locker(ShaderLocks[LockIndex], SLT_Write);
			Shader = Library->CreateShader(ShaderIndex);
			RHIShaders[ShaderIndex] = Shader;
		}
		return Shader;
	}

	void ReleaseShader(int32 ShaderIndex)
	{
		const int32 LockIndex = ShaderIndex % NumShaderLocks;
		FRWScopeLock Locker(ShaderLocks[LockIndex], SLT_Write);
		FRHIShader* Shader = RHIShaders[ShaderIndex];
		if(Shader)
		{
			// The library instance is holding one ref
			// External caller of this method must be holding a ref as well, so there must be at least 2 refs
			// If those are the only 2 refs, we release the ref held by the library instance, to allow the shader to be destroyed once caller releases its ref
			const uint32 NumRefs = Shader->GetRefCount();
			check(NumRefs > 1u);
			if(NumRefs == 2u)
			{
				RHIShaders[ShaderIndex].SafeRelease();
			}
		}
	}

public:
	FRHIShaderLibraryRef Library;

private:
	static const int32 NumShaderLocks = 32;

	FShaderLibraryInstance() {}

	bool OnExternalReadCallback(const FGraphEventRef& Event, double RemainingTime)
	{
		if (Event)
		{
			if (RemainingTime < 0.0)
			{
				return Event->IsComplete();
			}
			FTaskGraphInterface::Get().WaitUntilTaskCompletes(Event);
		}
		return true;
	}

	TArray<TRefCountPtr<FRHIShader>> RHIShaders;
	TArray<FShaderMapResource_SharedCode*> Resources;
	FRWLock ShaderLocks[NumShaderLocks];
	FRWLock ResourceLock;
};

FShaderMapResource_SharedCode::FShaderMapResource_SharedCode(FShaderLibraryInstance* InLibraryInstance, int32 InShaderMapIndex)
	: FShaderMapResource(InLibraryInstance->GetPlatform(), InLibraryInstance->GetNumShadersForShaderMap(InShaderMapIndex))
	, LibraryInstance(InLibraryInstance)
	, ShaderMapIndex(InShaderMapIndex)
{
}

FShaderMapResource_SharedCode::~FShaderMapResource_SharedCode()
{
	
}

TRefCountPtr<FRHIShader> FShaderMapResource_SharedCode::CreateRHIShader(int32 ShaderIndex)
{
	SCOPED_LOADTIMER(FShaderMapResource_SharedCode_InitRHI);

	const int32 LibraryShaderIndex = LibraryInstance->Library->GetShaderIndex(ShaderMapIndex, ShaderIndex);
	return LibraryInstance->GetOrCreateShader(LibraryShaderIndex);
}

void FShaderMapResource_SharedCode::ReleaseRHI()
{
	const int32 NumShaders = GetNumShaders();
	for (int32 i = 0; i < NumShaders; ++i)
	{
		if (HasShader(i))
		{
			const int32 ShaderIndex = LibraryInstance->Library->GetShaderIndex(ShaderMapIndex, i);
			LibraryInstance->ReleaseShader(ShaderIndex);
		}
	}

	LibraryInstance->Library->ReleasePreloadedShaderMap(ShaderMapIndex);

	FShaderMapResource::ReleaseRHI();
}

bool FShaderMapResource_SharedCode::TryRelease()
{
	if (LibraryInstance->TryRemoveResource(this))
	{
		return true;
	}

	return false;
}

#if WITH_EDITOR
struct FShaderCodeStats
{
	int64 ShadersSize;
	int64 ShadersUniqueSize;
	int32 NumShaders;
	int32 NumUniqueShaders;
	//int32 NumPipelines;
	//int32 NumUniquePipelines;

	int32 NumShaderMaps;
	int32 NumShaderMapsWithUnknownAssets;
	int32 NumUnnamedShaderMaps;
	int32 TimesShadersMovedCloser;
};

struct FEditorShaderCodeArchive
{
	FEditorShaderCodeArchive(FName InFormat, bool bInNeedsDeterministicOrder)
		: FormatName(InFormat)
		, Format(nullptr)
		, bNeedsDeterministicOrder(bInNeedsDeterministicOrder)
		, bHasActiveShaderMap(false)
	{
		Format = GetTargetPlatformManagerRef().FindShaderFormat(InFormat);
		check(Format);

		SerializedShaders.ShaderHashTable.Initialize(0x10000);
		SerializedShaders.ShaderMapHashTable.Initialize(0x10000);
	}

	~FEditorShaderCodeArchive() {}

	const IShaderFormat* GetFormat() const
	{
		return Format;
	}

	void OpenLibrary(FString const& Name)
	{
		check(LibraryName.Len() == 0);
		check(Name.Len() > 0);
		LibraryName = Name;
		SerializedShaders.Empty();
		ShaderCode.Empty();
	}

	void CloseLibrary(FString const& Name)
	{
		check(LibraryName == Name);
		LibraryName = TEXT("");
	}

	bool HasShaderMap(const FSHAHash& Hash) const
	{
		return SerializedShaders.FindShaderMap(Hash) != INDEX_NONE;
	}

	void BeginShaderMap(const TArray<FString>& AssociatedAssets, const FName& InShaderMapName, const FPakOrderMap* OrderMap, FShaderCodeStats& CodeStats)
	{
		CodeStats.NumShaderMaps++;

		if (bHasActiveShaderMap)
		{
			UE_LOG(LogShaderLibrary, Error, TEXT("Begin/EndShaderMap parity mismatch for shadermap '%s', currently active '%s'"), *InShaderMapName.ToString(), *ShaderMapName.ToString());
			bHasActiveShaderMap = false;
		}

		ShaderMapName = InShaderMapName;
		if (ShaderMapName == NAME_None)
		{
			UE_LOG(LogShaderLibrary, Warning, TEXT("Shadermap without a type name encountered"));
			CodeStats.NumUnnamedShaderMaps++;
		}

		UE_LOG(LogInit, VeryVerbose, TEXT("BeginShaderMap, type name %s: Got %d associated assets"), *ShaderMapName.ToString(), AssociatedAssets.Num());

		// unlikely special case for a global shadermap
		static FName GlobalShaderMapName(TEXT("GlobalShaderMap"));
		if (LIKELY(OrderMap && ShaderMapName != GlobalShaderMapName))
		{
			if (AssociatedAssets.Num())
			{
				uint64 MinAssetOrder = MAX_uint64;
				for (const FString& AssetPath: AssociatedAssets)
				{
					uint64 ThisAssetOrder = OrderMap->GetFileOrder(AssetPath, false);
					UE_LOG(LogInit, VeryVerbose, TEXT(" '%s', order %llu"), *AssetPath, ThisAssetOrder);
					if (ThisAssetOrder < MinAssetOrder)
					{
						MinAssetOrder = ThisAssetOrder;
						FirstAssociatedAsset = AssetPath;
					}
				}
				FirstAssociatedAssetOrder = MinAssetOrder;
			}
			else
			{
				UE_LOG(LogShaderLibrary, Warning, TEXT("Shadermap '%s' has zero associated assets"), *ShaderMapName.ToString());
				CodeStats.NumShaderMapsWithUnknownAssets++;
			}
		}
		else
		{
			FirstAssociatedAssetOrder = 0;
		}
		UE_LOG(LogInit, VeryVerbose, TEXT(" Order for shaders from this ShaderMap will be %llu"), FirstAssociatedAssetOrder);

		bHasActiveShaderMap = true;
	}

	void EndShaderMap()
	{
		UE_LOG(LogInit, VeryVerbose, TEXT("EndShaderMap"));
		bHasActiveShaderMap = false;
	}

	int32 AddShaderCode(FShaderCodeStats& CodeStats, const FShaderMapResourceCode* Code)
	{
		int32 ShaderMapIndex = INDEX_NONE;
		if (SerializedShaders.FindOrAddShaderMap(Code->ResourceHash, ShaderMapIndex))
		{
			const int32 NumShaders = Code->ShaderEntries.Num();
			FShaderMapEntry& ShaderMapEntry = SerializedShaders.ShaderMapEntries[ShaderMapIndex];
			ShaderMapEntry.NumShaders = NumShaders;
			ShaderMapEntry.ShaderIndicesOffset = SerializedShaders.ShaderIndices.AddZeroed(NumShaders);

			for(int32 i = 0; i < NumShaders; ++i)
			{
				int32 ShaderIndex = INDEX_NONE;
				if (SerializedShaders.FindOrAddShader(Code->ShaderHashes[i], ShaderIndex))
				{
					const FShaderMapResourceCode::FShaderEntry& SourceShaderEntry = Code->ShaderEntries[i];
					FShaderCodeEntry& SerializedShaderEntry = SerializedShaders.ShaderEntries[ShaderIndex];
					SerializedShaderEntry.Frequency = SourceShaderEntry.Frequency;
					SerializedShaderEntry.Size = SourceShaderEntry.Code.Num();
					SerializedShaderEntry.UncompressedSize = SourceShaderEntry.UncompressedSize;
					ShaderCode.Add(SourceShaderEntry.Code);
					check(ShaderCode.Num() == SerializedShaders.ShaderEntries.Num());

					CodeStats.NumUniqueShaders++;
					CodeStats.ShadersUniqueSize += SourceShaderEntry.Code.Num();
					CodeStats.ShadersSize += SourceShaderEntry.Code.Num();
				}
				SerializedShaders.ShaderIndices[ShaderMapEntry.ShaderIndicesOffset + i] = ShaderIndex;
			}
		}
		return ShaderMapIndex;
	}

	int32 AddShaderCode(int32 OtherShaderMapIndex, const FEditorShaderCodeArchive& OtherArchive)
	{
		int32 ShaderMapIndex = 0;
		if (SerializedShaders.FindOrAddShaderMap(OtherArchive.SerializedShaders.ShaderMapHashes[OtherShaderMapIndex], ShaderMapIndex))
		{
			const FShaderMapEntry& PrevShaderMapEntry = OtherArchive.SerializedShaders.ShaderMapEntries[OtherShaderMapIndex];
			FShaderMapEntry& ShaderMapEntry = SerializedShaders.ShaderMapEntries[ShaderMapIndex];
			ShaderMapEntry.NumShaders = PrevShaderMapEntry.NumShaders;
			ShaderMapEntry.ShaderIndicesOffset = SerializedShaders.ShaderIndices.AddZeroed(ShaderMapEntry.NumShaders);

			for (uint32 i = 0; i < ShaderMapEntry.NumShaders; ++i)
			{
				const int32 OtherShaderIndex = OtherArchive.SerializedShaders.ShaderIndices[PrevShaderMapEntry.ShaderIndicesOffset + i];
				int32 ShaderIndex = 0;
				if (SerializedShaders.FindOrAddShader(OtherArchive.SerializedShaders.ShaderHashes[OtherShaderIndex], ShaderIndex))
				{
					const FShaderCodeEntry& OtherShaderEntry = OtherArchive.SerializedShaders.ShaderEntries[OtherShaderIndex];
					SerializedShaders.ShaderEntries[ShaderIndex] = OtherShaderEntry;

					ShaderCode.Add(OtherArchive.ShaderCode[OtherShaderIndex]);
					check(ShaderCode.Num() == SerializedShaders.ShaderEntries.Num());
				}
				SerializedShaders.ShaderIndices[ShaderMapEntry.ShaderIndicesOffset + i] = ShaderIndex;
			}
		}
		return ShaderMapIndex;
	}

	int32 AddShaderCode(int32 OtherShaderMapIndex,
		const FSerializedShaderArchive& OtherShaders,
		int64 OtherShaderCodeOffset,
		FArchive& Ar)
	{
		int32 ShaderMapIndex = 0;
		if (SerializedShaders.FindOrAddShaderMap(OtherShaders.ShaderMapHashes[OtherShaderMapIndex], ShaderMapIndex))
		{
			const FShaderMapEntry& PrevShaderMapEntry = OtherShaders.ShaderMapEntries[OtherShaderMapIndex];
			FShaderMapEntry& ShaderMapEntry = SerializedShaders.ShaderMapEntries[ShaderMapIndex];
			ShaderMapEntry.NumShaders = PrevShaderMapEntry.NumShaders;
			ShaderMapEntry.ShaderIndicesOffset = SerializedShaders.ShaderIndices.AddZeroed(ShaderMapEntry.NumShaders);

			for (uint32 i = 0; i < ShaderMapEntry.NumShaders; ++i)
			{
				const int32 OtherShaderIndex = OtherShaders.ShaderIndices[PrevShaderMapEntry.ShaderIndicesOffset + i];
				int32 ShaderIndex = 0;
				if (SerializedShaders.FindOrAddShader(OtherShaders.ShaderHashes[OtherShaderIndex], ShaderIndex))
				{
					const FShaderCodeEntry& OtherShaderEntry = OtherShaders.ShaderEntries[OtherShaderIndex];
					SerializedShaders.ShaderEntries[ShaderIndex] = OtherShaderEntry;

					TArray<uint8>& Code = ShaderCode.AddDefaulted_GetRef();
					check(ShaderCode.Num() == SerializedShaders.GetNumShaders());

					// Read shader code from archive and add shader to set
					const int64 ReadSize = OtherShaderEntry.Size;
					const int64 ReadOffset = OtherShaderCodeOffset + OtherShaderEntry.Offset;
					Code.SetNumUninitialized(ReadSize);
					Ar.Seek(ReadOffset);
					Ar.Serialize(Code.GetData(), ReadSize);
				}
				SerializedShaders.ShaderIndices[ShaderMapEntry.ShaderIndicesOffset + i] = ShaderIndex;
			}
		}
		return ShaderMapIndex;
	}

	bool LoadExistingShaderCodeLibrary(FString const& MetaDataDir)
	{
		FString IntermediateFormatPath = GetCodeArchiveFilename(MetaDataDir / TEXT("ShaderLibrarySource"), LibraryName, FormatName);
		FArchive* PrevCookedAr = IFileManager::Get().CreateFileReader(*IntermediateFormatPath);
		bool bOK = true;
		if (PrevCookedAr)
		{
			uint32 ArchiveVersion = 0;
			*PrevCookedAr << ArchiveVersion;
			if (ArchiveVersion == GShaderCodeArchiveVersion)
			{
				// Read shader library
				*PrevCookedAr << SerializedShaders;

				ShaderCode.AddDefaulted(SerializedShaders.ShaderEntries.Num());
				for(int32 Index = 0; Index < ShaderCode.Num(); ++Index)
				{
					const FShaderCodeEntry& Entry = SerializedShaders.ShaderEntries[Index];
					TArray<uint8>& Code = ShaderCode[Index];
					Code.SetNumUninitialized(Entry.Size);
					PrevCookedAr->Serialize(Code.GetData(), Entry.Size);
					bOK = !PrevCookedAr->GetError();
					if (!bOK)
					{
						UE_LOG(LogShaderLibrary, Error, TEXT("Failed to deserialize shader code for %s from %s"), *SerializedShaders.ShaderHashes[Index].ToString(), *IntermediateFormatPath);
						break;
					}
				}
			}
			else
			{
				bOK = false;
				UE_LOG(LogShaderLibrary, Warning, TEXT("Failed to deserialize shader code from %s because the archive format %u is incompatible with the current version %u"), *IntermediateFormatPath, ArchiveVersion, GShaderCodeArchiveVersion);
			}
			
			PrevCookedAr->Close();
			delete PrevCookedAr;
		}
		else
		{
			bOK = false;
			UE_LOG(LogShaderLibrary, Error, TEXT("Failed to open shader code library from %s"), *IntermediateFormatPath);
		}
		
#if 0
		if (bOK)
		{
			FString PipelinesPath = GetPipelinesArchiveFilename(MetaDataDir / TEXT("ShaderLibrarySource"), LibraryName, FormatName);
			FArchive* PipelinesArchive = IFileManager::Get().CreateFileReader(*PipelinesPath);
			if (PipelinesArchive)
			{
				uint32 ArchiveVersion = 0;
				*PipelinesArchive << ArchiveVersion;
				if (ArchiveVersion == GShaderPipelineArchiveVersion)
				{
					*PipelinesArchive << Pipelines;
				}
				else
				{
					bOK = false;
					UE_LOG(LogShaderLibrary, Warning, TEXT("Failed to deserialize shader pipelines from %s because the archive format %u is incompatible with the current version %u"), *PipelinesPath, ArchiveVersion, GShaderPipelineArchiveVersion);
				}
				
				PipelinesArchive->Close();
				delete PipelinesArchive;
			}
		}
#endif
		return bOK;
	}

	bool Finalize(FString OutputDir, const FString& MetaOutputDir, bool bNativeFormat)
	{
		check(LibraryName.Len() > 0);

		bool bSuccess = IFileManager::Get().MakeDirectory(*OutputDir, true);

		// Shader library
		if (bSuccess && SerializedShaders.GetNumShaderMaps() > 0)
		{
			// Write to a intermediate file
			FString IntermediateFormatPath = GetShaderCodeFilename(FPaths::ProjectSavedDir() / TEXT("Shaders") / FormatName.ToString(), LibraryName, FormatName);
			FArchive* FileWriter = IFileManager::Get().CreateFileWriter(*IntermediateFormatPath, FILEWRITE_NoFail);

			if (FileWriter)
			{
				check(Format);

				SerializedShaders.Finalize();

				*FileWriter << GShaderCodeArchiveVersion;

                // Write shader library
                *FileWriter << SerializedShaders;
                for (auto& Code : ShaderCode)
                {
                    FileWriter->Serialize(Code.GetData(), Code.Num());
                }

				FileWriter->Close();
				delete FileWriter;

				FString OutputFilePath = GetCodeArchiveFilename(OutputDir, LibraryName, FormatName);

				// Copy to output location - support for iterative native library cooking
				uint32 Result = IFileManager::Get().Copy(*OutputFilePath, *IntermediateFormatPath, true, true);
				if (Result != COPY_OK)
				{
					UE_LOG(LogShaderLibrary, Error, TEXT("FEditorShaderCodeArchive shader library copy failed to %s. Failed to finalize Shared Shader Library %s with format %s"), *OutputFilePath, *LibraryName, *FormatName.ToString());
					bSuccess = false;
				}
				
				if (MetaOutputDir.Len())
				{
					FString MetaFormatPath = GetCodeArchiveFilename(MetaOutputDir / TEXT("../ShaderLibrarySource"), LibraryName, FormatName);
					Result = IFileManager::Get().Copy(*MetaFormatPath, *IntermediateFormatPath, true, true);
					if (Result != COPY_OK)
					{
						UE_LOG(LogShaderLibrary, Error, TEXT("FEditorShaderCodeArchive shader library copy failed to %s. Failed to saved metadata copy of Shared Shader Library %s with format %s"), *OutputFilePath, *LibraryName, *FormatName.ToString());
						bSuccess = false;
					}
				}

			}
		}

#if 0
		// Pipelines
		if (bSuccess && Pipelines.Num() > 0)
		{
			// Write to a temporary file
			FString TempFilePath = GetPipelinesArchiveFilename(FPaths::ProjectSavedDir() / TEXT("Shaders"), LibraryName, FormatName);
			FArchive* FileWriter = IFileManager::Get().CreateFileWriter(*TempFilePath, FILEWRITE_NoFail);

			*FileWriter << GShaderPipelineArchiveVersion;

			if (bNeedsDeterministicOrder)
			{
				Pipelines.Sort([](const FShaderCodeLibraryPipeline& A, const FShaderCodeLibraryPipeline& B){ return A.Hash <= B.Hash; });
			}

			*FileWriter << Pipelines;

			FileWriter->Close();
			delete FileWriter;

			FString OutputFilePath = GetPipelinesArchiveFilename(OutputDir, LibraryName, FormatName);

			// Copy to output location - support for iterative native library cooking
			uint32 Result = IFileManager::Get().Copy(*OutputFilePath, *TempFilePath, true, true);
			if (Result != COPY_OK)
			{
				UE_LOG(LogShaderLibrary, Error, TEXT("FEditorShaderCodeArchive pipeline copy failed to %s. Failed to finalize Shared Shader Library %s with format %s"), *OutputFilePath, *LibraryName, *FormatName.ToString());
				bSuccess = false;
			}
                
            if (MetaOutputDir.Len())
            {
                FString MetaFormatPath = GetPipelinesArchiveFilename(MetaOutputDir / TEXT("../ShaderLibrarySource"), LibraryName, FormatName);
                Result = IFileManager::Get().Copy(*MetaFormatPath, *TempFilePath, true, true);
                if (Result != COPY_OK)
                {
                    UE_LOG(LogShaderLibrary, Error, TEXT("FEditorShaderCodeArchive pipeline copy failed to %s. Failed to save metadata copy of Shared Shader Library %s with format %s"), *OutputFilePath, *LibraryName, *FormatName.ToString());
                    bSuccess = false;
                }
            }
		}
#endif

		return bSuccess;
	}

	bool PackageNativeShaderLibrary(const FString& ShaderCodeDir)
	{
		if (SerializedShaders.GetNumShaders() == 0)
		{
			return true;
		}

		FString IntermediateFormatPath = GetShaderDebugFolder(FPaths::ProjectSavedDir() / TEXT("Shaders") / FormatName.ToString(), LibraryName, FormatName);
		FString TempPath = IntermediateFormatPath / TEXT("NativeLibrary");

		IFileManager::Get().MakeDirectory(*TempPath, true);
		IFileManager::Get().MakeDirectory(*ShaderCodeDir, true);

		EShaderPlatform Platform = ShaderFormatToLegacyShaderPlatform(FormatName);
		const bool bOK = Format->CreateShaderArchive(LibraryName, FormatName, TempPath, ShaderCodeDir, IntermediateFormatPath, SerializedShaders, ShaderCode, nullptr);
		if (bOK)
		{
			// Delete Shader code library / pipelines as we now have native versions
			{
				FString OutputFilePath = GetCodeArchiveFilename(ShaderCodeDir, LibraryName, FormatName);
				IFileManager::Get().Delete(*OutputFilePath);
			}
			{
				FString OutputFilePath = GetPipelinesArchiveFilename(ShaderCodeDir, LibraryName, FormatName);
				IFileManager::Get().Delete(*OutputFilePath);
			}
		}

		// Clean up the saved directory of temporary files
		IFileManager::Get().DeleteDirectory(*IntermediateFormatPath, false, true);
		IFileManager::Get().DeleteDirectory(*TempPath, false, true);

		return bOK;
	}

	void MakePatchLibrary(TArray<FEditorShaderCodeArchive*> const& OldLibraries, FEditorShaderCodeArchive const& NewLibrary)
	{
		for(int32 NewShaderMapIndex = 0; NewShaderMapIndex < NewLibrary.SerializedShaders.ShaderMapHashes.Num(); ++NewShaderMapIndex)
		{
			const FSHAHash& Hash = NewLibrary.SerializedShaders.ShaderMapHashes[NewShaderMapIndex];
			if (!HasShaderMap(Hash))
			{
				bool bInPreviousPatch = false;
				for (FEditorShaderCodeArchive const* OldLibrary : OldLibraries)
				{
					bInPreviousPatch |= OldLibrary->HasShaderMap(Hash);
					if (bInPreviousPatch)
					{
						break;
					}
				}
				if (!bInPreviousPatch)
				{
					AddShaderCode(NewShaderMapIndex, NewLibrary);
				}
			}
		}
	}
	
	static bool CreatePatchLibrary(FName FormatName, FString const& LibraryName, TArray<FString> const& OldMetaDataDirs, FString const& NewMetaDataDir, FString const& OutDir, bool bNativeFormat, bool bNeedsDeterministicOrder)
	{
		TArray<FEditorShaderCodeArchive*> OldLibraries;
		for (FString const& OldMetaDataDir : OldMetaDataDirs)
		{
			FEditorShaderCodeArchive* OldLibrary = new FEditorShaderCodeArchive(FormatName, bNeedsDeterministicOrder);
			OldLibrary->OpenLibrary(LibraryName);
			if (OldLibrary->LoadExistingShaderCodeLibrary(OldMetaDataDir))
			{
				OldLibraries.Add(OldLibrary);
			}
		}

		FEditorShaderCodeArchive NewLibrary(FormatName, bNeedsDeterministicOrder);
		NewLibrary.OpenLibrary(LibraryName);
		bool bOK = NewLibrary.LoadExistingShaderCodeLibrary(NewMetaDataDir);
		if (bOK)
		{
			FEditorShaderCodeArchive OutLibrary(FormatName, bNeedsDeterministicOrder);
			OutLibrary.OpenLibrary(LibraryName);
			OutLibrary.MakePatchLibrary(OldLibraries, NewLibrary);
			bOK = OutLibrary.SerializedShaders.GetNumShaderMaps() > 0;
			if (bOK)
			{
				FString Empty;
				bOK = OutLibrary.Finalize(OutDir, Empty, bNativeFormat);
				UE_CLOG(!bOK, LogShaderLibrary, Error, TEXT("Failed to save %s shader patch library %s, %s, %s"), bNativeFormat ? TEXT("native") : TEXT(""), *FormatName.ToString(), *LibraryName, *OutDir);
				
				if (bOK && bNativeFormat && OutLibrary.GetFormat()->SupportsShaderArchives())
				{
					bOK = OutLibrary.PackageNativeShaderLibrary(OutDir);
					UE_CLOG(!bOK, LogShaderLibrary, Error, TEXT("Failed to package native shader patch library %s, %s, %s"), *FormatName.ToString(), *LibraryName, *OutDir);
				}
			}
			else
			{
				UE_LOG(LogShaderLibrary, Verbose, TEXT("No shaders to patch for library %s, %s, %s"), *FormatName.ToString(), *LibraryName, *OutDir);
			}
		}
		else
		{
			UE_LOG(LogShaderLibrary, Error, TEXT("Failed to open the shader library to patch against %s, %s, %s"), *FormatName.ToString(), *LibraryName, *NewMetaDataDir);
		}
		
		for (FEditorShaderCodeArchive* Lib : OldLibraries)
		{
			delete Lib;
		}
		return bOK;
	}

private:
	FName FormatName;
	FString LibraryName;
	FSerializedShaderArchive SerializedShaders;
	TArray<TArray<uint8>> ShaderCode;
	const IShaderFormat* Format;
	bool bNeedsDeterministicOrder;

	// state
	bool bHasActiveShaderMap;
	uint64 FirstAssociatedAssetOrder;
	FName ShaderMapName;
	FString FirstAssociatedAsset;
};

struct FEditorShaderStableInfo
{
	FEditorShaderStableInfo(FName InFormat)
		: FormatName(InFormat)
	{
	}

	void OpenLibrary(FString const& Name)
	{
		check(LibraryName.Len() == 0);
		check(Name.Len() > 0);
		LibraryName = Name;
		StableMap.Empty();
	}

	void CloseLibrary(FString const& Name)
	{
		check(LibraryName == Name);
		LibraryName = TEXT("");
	}

	void AddShader(FStableShaderKeyAndValue& StableKeyValue)
	{
		FStableShaderKeyAndValue* Existing = StableMap.Find(StableKeyValue);
		if (Existing && Existing->OutputHash != StableKeyValue.OutputHash)
		{
			UE_LOG(LogShaderLibrary, Warning, TEXT("Duplicate key in stable shader library, but different keys, skipping new item:"));
			UE_LOG(LogShaderLibrary, Warning, TEXT("    Existing: %s"), *Existing->ToString());
			UE_LOG(LogShaderLibrary, Warning, TEXT("    New     : %s"), *StableKeyValue.ToString());
			return;
		}
		StableMap.Add(StableKeyValue);
	}

	bool Finalize(FString OutputDir, bool bNativeFormat, FString& OutSCLCSVPath)
	{
		check(LibraryName.Len() > 0);
		OutSCLCSVPath = FString();

		bool bSuccess = IFileManager::Get().MakeDirectory(*OutputDir, true);

		EShaderPlatform Platform = ShaderFormatToLegacyShaderPlatform(FormatName);

		// Shader library
		if (bSuccess && StableMap.Num() > 0)
		{
			// Write to a intermediate file
			FString IntermediateFormatPath = GetStableInfoArchiveFilename(FPaths::ProjectSavedDir() / TEXT("Shaders") / FormatName.ToString(), LibraryName, FormatName);

			// Write directly to the file
			{
				TUniquePtr<FArchive> IntermediateFormatAr(IFileManager::Get().CreateFileWriter(*IntermediateFormatPath));

				FString HeaderText = FStableShaderKeyAndValue::HeaderLine();
				HeaderText += TCHAR('\n');
				auto HeaderSrc = StringCast<ANSICHAR>(*HeaderText, HeaderText.Len());

				IntermediateFormatAr->Serialize((ANSICHAR*)HeaderSrc.Get(), HeaderSrc.Length() * sizeof(ANSICHAR));

				TAnsiStringBuilder<512> LineBuffer;

				for (const FStableShaderKeyAndValue& Item : StableMap)
				{
					LineBuffer.Reset();
					Item.AppendString(LineBuffer);
					LineBuffer << '\n';
					IntermediateFormatAr->Serialize(const_cast<ANSICHAR*>(LineBuffer.ToString()), LineBuffer.Len() * sizeof(ANSICHAR));
				}
			}

			// Only the master cooker needs to write to the output directory, child cookers only write to the Saved directory
			FString OutputFilePath = GetStableInfoArchiveFilename(OutputDir, LibraryName, FormatName);

			// Copy to output location - support for iterative native library cooking
			uint32 Result = IFileManager::Get().Copy(*OutputFilePath, *IntermediateFormatPath, true, true);
			if (Result == COPY_OK)
			{
				OutSCLCSVPath = OutputFilePath;
			}
			else
			{
				UE_LOG(LogShaderLibrary, Error, TEXT("FEditorShaderStableInfo copy failed to %s. Failed to finalize Shared Shader Library %s with format %s"), *OutputFilePath, *LibraryName, *FormatName.ToString());
				bSuccess = false;
			}
		}

		return bSuccess;
	}

private:
	FName FormatName;
	FString LibraryName;
	TSet<FStableShaderKeyAndValue> StableMap;
};
#endif //WITH_EDITOR

class FShaderCodeLibraryImpl
{
	// At runtime, shader code collection for current shader platform
	TArray<TUniquePtr<FShaderLibraryInstance>> ShaderCodeArchiveStack;

	EShaderPlatform ShaderPlatform;
	uint64 ShaderCount;
	FRWLock LibraryMutex;
#if WITH_EDITOR
	FCriticalSection ShaderCodeCS;
	// At cook time, shader code collection for each shader platform
	FEditorShaderCodeArchive* EditorShaderCodeArchive[EShaderPlatform::SP_NumPlatforms];
	// At cook time, shader code collection for each shader platform
	FEditorShaderStableInfo* EditorShaderStableInfo[EShaderPlatform::SP_NumPlatforms];
	// Cached bit field for shader formats that require stable keys
	uint64_t bShaderFormatsThatNeedStableKeys = 0;
	// At cook time, shader stats for each shader platform
	FShaderCodeStats EditorShaderCodeStats[EShaderPlatform::SP_NumPlatforms];
	// At cook time, whether the shader archive supports pipelines (only OpenGL should)
	bool EditorArchivePipelines[EShaderPlatform::SP_NumPlatforms];
	// At cook time, the file open order map
	FPakOrderMap* OpenOrderMap;
#endif //WITH_EDITOR
	bool bSupportsPipelines;
	bool bNativeFormat;

public:
	static FShaderCodeLibraryImpl* Impl;

	FShaderCodeLibraryImpl(bool bInNativeFormat)
		: ShaderPlatform(SP_NumPlatforms)
		, ShaderCount(0)
		, bSupportsPipelines(false)
		, bNativeFormat(bInNativeFormat)
	{
#if WITH_EDITOR
		FMemory::Memzero(EditorShaderCodeArchive);
		FMemory::Memzero(EditorShaderStableInfo);
		FMemory::Memzero(EditorShaderCodeStats);
		FMemory::Memzero(EditorArchivePipelines);

		OpenOrderMap = nullptr;
#endif
	}

	~FShaderCodeLibraryImpl()
	{
#if WITH_EDITOR
		for (uint32 i = 0; i < EShaderPlatform::SP_NumPlatforms; i++)
		{
			if (EditorShaderCodeArchive[i])
			{
				delete EditorShaderCodeArchive[i];
			}
			if (EditorShaderStableInfo[i])
			{
				delete EditorShaderStableInfo[i];
			}
		}
		FMemory::Memzero(EditorShaderCodeArchive);
		FMemory::Memzero(EditorShaderStableInfo);
#endif
	}

	bool OpenLibrary(FString const& Name, FString const& Directory)
	{
		LLM_SCOPE(ELLMTag::Shaders);
		
		bool bResult = false;

		if (ShaderPlatform < SP_NumPlatforms)
		{
			if (OpenShaderCode(Directory, ShaderPlatform, Name))
			{
				bResult = true;
				
				// Attempt to open the shared-cooked override code library if there is one.
				// This is probably not ideal, but it should get shared-cooks working.
				OpenShaderCode(Directory, ShaderPlatform, Name + TEXT("_SC"));

				// Inform the pipeline cache that the state of loaded libraries has changed
				FShaderPipelineCache::ShaderLibraryStateChanged(FShaderPipelineCache::Opened, ShaderPlatform, Name);
			}
		}

#if WITH_EDITOR
		for (uint32 i = 0; i < EShaderPlatform::SP_NumPlatforms; i++)
		{
			FEditorShaderCodeArchive* CodeArchive = EditorShaderCodeArchive[i];
			if (CodeArchive)
			{
				CodeArchive->OpenLibrary(Name);
			}
		}
		for (uint32 i = 0; i < EShaderPlatform::SP_NumPlatforms; i++)
		{
			FEditorShaderStableInfo* StableArchive = EditorShaderStableInfo[i];
			if (StableArchive)
			{
				StableArchive->OpenLibrary(Name);
			}
		}
#endif
		
		return bResult;
	}

	void CloseLibrary(FString const& Name)
	{
		{
			FRWScopeLock(LibraryMutex, SLT_Write);
			for (uint32 i = ShaderCodeArchiveStack.Num(); i > 0; i--)
			{
				FShaderLibraryInstance* LibraryInstance = ShaderCodeArchiveStack[i - 1].Get();
				if (LibraryInstance->Library->GetName() == Name)
				{
					ShaderCodeArchiveStack.RemoveAt(i - 1);
					break;
				}
			}
		}

		// Inform the pipeline cache that the state of loaded libraries has changed
		FShaderPipelineCache::ShaderLibraryStateChanged(FShaderPipelineCache::Closed, ShaderPlatform, Name);

#if WITH_EDITOR
		for (uint32 i = 0; i < EShaderPlatform::SP_NumPlatforms; i++)
		{
			if (EditorShaderCodeArchive[i])
			{
				EditorShaderCodeArchive[i]->CloseLibrary(Name);
			}
			if (EditorShaderStableInfo[i])
			{
				EditorShaderStableInfo[i]->CloseLibrary(Name);
			}
		}
#endif
	}

	// At runtime, open shader code collection for specified shader platform
	bool OpenShaderCode(const FString& ShaderCodeDir, EShaderPlatform InShaderPlatform, FString const& Library)
	{
		check(ShaderPlatform == SP_NumPlatforms || InShaderPlatform == ShaderPlatform);

		FShaderLibraryInstance* LibraryInstance = FShaderLibraryInstance::Create(InShaderPlatform, ShaderCodeDir, Library);
		if(!LibraryInstance)
		{
			UE_LOG(LogShaderLibrary, Verbose, TEXT("Cooked Context: No Shared Shader Library for: %s and native library not supported."), *Library);
			return false;
		}

		ShaderPlatform = InShaderPlatform;

		if (LibraryInstance->Library->IsNativeLibrary())
		{
			UE_LOG(LogShaderLibrary, Display, TEXT("Cooked Context: Loaded Native Shared Shader Library %s"), *Library);
		}
		else
		{
			UE_LOG(LogShaderLibrary, Display, TEXT("Cooked Context: Using Shared Shader Library %s"), *Library);
		}

		FRWScopeLock(LibraryMutex, SLT_Write);
		ShaderCodeArchiveStack.Emplace(LibraryInstance);
		ShaderCount += LibraryInstance->GetNumShaders();
		return true;
	}

	uint32 GetShaderCount(void)
	{
		return ShaderCount;
	}

	EShaderPlatform GetRuntimeShaderPlatform(void)
	{
		return ShaderPlatform;
	}

	FShaderLibraryInstance* FindShaderLibraryForShaderMap(const FSHAHash& Hash, int32& OutShaderMapIndex)
	{
		FRWScopeLock(LibraryMutex, SLT_ReadOnly);

		// Search in library opened order
		for (const TUniquePtr<FShaderLibraryInstance>& Instance : ShaderCodeArchiveStack)
		{
			const int32 ShaderMapIndex = Instance->Library->FindShaderMapIndex(Hash);
			if(ShaderMapIndex != INDEX_NONE)
			{
				OutShaderMapIndex = ShaderMapIndex;
				return Instance.Get();
			}
		}
		return nullptr;
	}

	FShaderLibraryInstance* FindShaderLibraryForShader(const FSHAHash& Hash, int32& OutShaderIndex)
	{
		FRWScopeLock(LibraryMutex, SLT_ReadOnly);

		// Search in library opened order
		for (const TUniquePtr<FShaderLibraryInstance>& Instance : ShaderCodeArchiveStack)
		{
			const int32 ShaderIndex = Instance->Library->FindShaderIndex(Hash);
			if (ShaderIndex != INDEX_NONE)
			{
				OutShaderIndex = ShaderIndex;
				return Instance.Get();
			}
		}
		return nullptr;
	}

	TRefCountPtr<FShaderMapResource_SharedCode> LoadResource(const FSHAHash& Hash, FArchive* Ar)
	{
		int32 ShaderMapIndex = INDEX_NONE;
		FShaderLibraryInstance* LibraryInstance = FindShaderLibraryForShaderMap(Hash, ShaderMapIndex);
		if (LibraryInstance)
		{
			SCOPED_LOADTIMER(LoadShaderResource_Internal);

			TRefCountPtr<FShaderMapResource_SharedCode> Resource = LibraryInstance->GetResource(ShaderMapIndex);
			if (!Resource)
			{
				SCOPED_LOADTIMER(LoadShaderResource_AddOrDeleteResource);
				Resource = LibraryInstance->AddOrDeleteResource(new FShaderMapResource_SharedCode(LibraryInstance, ShaderMapIndex));
			}

			if (!GRHILazyShaderCodeLoading)
			{
				LibraryInstance->PreloadShaderMap(ShaderMapIndex, Ar);
			}

			return Resource;
		}

		return TRefCountPtr<FShaderMapResource_SharedCode>();
	}

	TRefCountPtr<FRHIShader> CreateShader(EShaderFrequency Frequency, const FSHAHash& Hash)
	{
		int32 ShaderIndex = INDEX_NONE;
		FShaderLibraryInstance* LibraryInstance = FindShaderLibraryForShader(Hash, ShaderIndex);
		if (LibraryInstance)
		{
			TRefCountPtr<FRHIShader> Shader = LibraryInstance->GetOrCreateShader(ShaderIndex);
			check(Shader->GetFrequency() == Frequency);
			return Shader;
		}
		return TRefCountPtr<FRHIShader>();
	}

	bool PreloadShader(const FSHAHash& Hash, FArchive* Ar)
	{
		int32 ShaderIndex = INDEX_NONE;
		FShaderLibraryInstance* LibraryInstance = FindShaderLibraryForShader(Hash, ShaderIndex);
		if (LibraryInstance)
		{
			LibraryInstance->PreloadShader(ShaderIndex, Ar);
			return true;
		}
		return false;
	}

	bool ContainsShaderCode(const FSHAHash& Hash)
	{
		int32 ShaderIndex = INDEX_NONE;
		FShaderLibraryInstance* LibraryInstance = FindShaderLibraryForShader(Hash, ShaderIndex);
		return LibraryInstance != nullptr;
	}

#if WITH_EDITOR
	void CleanDirectories(TArray<FName> const& ShaderFormats)
	{
		for (FName const& Format : ShaderFormats)
		{
			FString ShaderIntermediateLocation = FPaths::ProjectSavedDir() / TEXT("Shaders") / Format.ToString();
			IFileManager::Get().DeleteDirectory(*ShaderIntermediateLocation, false, true);
		}
	}

	void CookShaderFormats(TArray<FShaderCodeLibrary::FShaderFormatDescriptor> const& ShaderFormats)
	{
		bool bAtLeastOneFormatNeedsDeterminism = false;

		for (const FShaderCodeLibrary::FShaderFormatDescriptor& Descriptor : ShaderFormats)
		{
			FName const& Format = Descriptor.ShaderFormat;

			EShaderPlatform Platform = ShaderFormatToLegacyShaderPlatform(Format);
			FName PossiblyAdjustedFormat = LegacyShaderPlatformToShaderFormat(Platform);	// Vulkan and GL switch between name variants depending on CVars (e.g. see r.Vulkan.UseRealUBs)
			FEditorShaderCodeArchive* CodeArchive = EditorShaderCodeArchive[Platform];
			if (!CodeArchive)
			{
				CodeArchive = new FEditorShaderCodeArchive(PossiblyAdjustedFormat, Descriptor.bNeedsDeterministicOrder);
				EditorShaderCodeArchive[Platform] = CodeArchive;
				EditorArchivePipelines[Platform] = !bNativeFormat;
			}
			check(CodeArchive);

			if (Descriptor.bNeedsDeterministicOrder)
			{
				bAtLeastOneFormatNeedsDeterminism = true;
			}
		}
		for (const FShaderCodeLibrary::FShaderFormatDescriptor& Descriptor : ShaderFormats)
		{
			FName const& Format = Descriptor.ShaderFormat;
			bool bUseStableKeys = Descriptor.bNeedsStableKeys;

			EShaderPlatform Platform = ShaderFormatToLegacyShaderPlatform(Format);
			FName PossiblyAdjustedFormat = LegacyShaderPlatformToShaderFormat(Platform);	// Vulkan and GL switch between name variants depending on CVars (e.g. see r.Vulkan.UseRealUBs)
			FEditorShaderStableInfo* StableArchive = EditorShaderStableInfo[Platform];
			if (!StableArchive && bUseStableKeys)
			{
				StableArchive = new FEditorShaderStableInfo(PossiblyAdjustedFormat);
				EditorShaderStableInfo[Platform] = StableArchive;
				bShaderFormatsThatNeedStableKeys |= (uint64_t(1u) << (uint32_t)Platform);
				static_assert(SP_NumPlatforms < 64u, "ShaderPlatform will no longer fit into bitfield.");
			}
		}

		if (bAtLeastOneFormatNeedsDeterminism)
		{
			LoadFileOpenOrderFiles();
		}
	}

	void LoadFileOpenOrderFiles()
	{
		// attempt to open the open order map file
		FString OrderFile;
		UE_LOG(LogShaderLibrary, Display, TEXT("Shader library set to be deterministic, looking for the order file"));
		// first, use the override, if any
		if (FParse::Value(FCommandLine::Get(), TEXT("FileOpenOrderPrimary="), OrderFile))
		{
			UE_LOG(LogShaderLibrary, Display, TEXT("Using '%s' as a source for the file open order (passed on the command line)"), *OrderFile);
		}
		else
		{
			FString PlatformStr;
			if (FParse::Value(FCommandLine::Get(), TEXT("TARGETPLATFORM="), PlatformStr))
			{
				TArray<FString> PlatformNames;
				if (!(PlatformStr == TEXT("None") || PlatformStr == TEXT("All")))
				{
					PlatformStr.ParseIntoArray(PlatformNames, TEXT("+"), true);
				}

				// only take the first and issue a warning if there's more than one
				if (PlatformNames.Num() > 1)
				{
					UE_LOG(LogShaderLibrary, Warning, TEXT("More than one platform is being targeted, only one of them will be considered for the file open order."));
				}

				for (int32 Platform = 0; Platform < PlatformNames.Num(); ++Platform)
				{
					FString LogFileDirectory = FPaths::Combine(FPlatformMisc::ProjectDir(), TEXT("Build"), *PlatformNames[Platform], TEXT("FileOpenOrder"));
					FString LogFilePath = FPaths::Combine(*LogFileDirectory, TEXT("GameOpenOrder.log"));
					UE_LOG(LogShaderLibrary, Display, TEXT("Checking if '%s' exists..."), *LogFilePath);
					if (FPaths::FileExists(LogFilePath))
					{
						OrderFile = LogFilePath;
						UE_LOG(LogShaderLibrary, Display, TEXT("Using '%s' as a source for the file open order (inferred from -targetplatform switch)"), *OrderFile);
						break;
					}
				}
			}
		}

		if (!OrderFile.IsEmpty())
		{
			OpenOrderMap = new FPakOrderMap;

			if (!OpenOrderMap->ProcessOrderFile(*OrderFile))
			{
				UE_LOG(LogShaderLibrary, Error, TEXT("Unable to use file open order file '%s', the shader library will not be deterministic."), *OrderFile);
				delete OpenOrderMap;
				OpenOrderMap = nullptr;
			}
			else
			{
				// check for a secondary file, if any
				FString OrderFileSecondary;
				if (FParse::Value(FCommandLine::Get(), TEXT("FileOpenOrderSecondary="), OrderFileSecondary))
				{
					UE_LOG(LogShaderLibrary, Display, TEXT("Using '%s' as a secondary source for the file open order (passed on the command line)"), *OrderFileSecondary);

					if (!OpenOrderMap->ProcessOrderFile(*OrderFileSecondary))
					{
						UE_LOG(LogShaderLibrary, Warning, TEXT("Unable to use secondary file open order file '%s', only the primary one will be used."), *OrderFileSecondary);
					}
				}
			}
		}
		else
		{
			UE_LOG(LogShaderLibrary, Error, TEXT("Unable to find any file open order file, the shader library will not be deterministic."));
		}
	}

	bool NeedsShaderStableKeys(EShaderPlatform Platform) 
	{
		if (Platform == EShaderPlatform::SP_NumPlatforms)
		{
			return bShaderFormatsThatNeedStableKeys != 0;
		}
		return (bShaderFormatsThatNeedStableKeys & (uint64_t(1u) << (uint32_t) Platform)) != 0;
	}

	void BeginShaderMap(EShaderPlatform InShaderPlatform, const TArray<FString>& AssociatedAssets, const FName& ShaderMapTypeName)
	{
		// There are some TShaderMap-derived classes that can call this function with a bogus SP - just ignore if not created
		if (InShaderPlatform < UE_ARRAY_COUNT(EditorShaderCodeArchive))
		{
			FScopeLock ScopeLock(&ShaderCodeCS);
			FShaderCodeStats& CodeStats = EditorShaderCodeStats[InShaderPlatform];

			FEditorShaderCodeArchive* CodeArchive = EditorShaderCodeArchive[InShaderPlatform];
			if (CodeArchive)	// see the above comment about a bogus SP
			{
				CodeArchive->BeginShaderMap(AssociatedAssets, ShaderMapTypeName, OpenOrderMap, CodeStats);
			}
		}
	}

	void EndShaderMap(EShaderPlatform InShaderPlatform)
	{
		// There are some TShaderMap-derived classes that can call this function with a bogus SP - just ignore if not created
		if (InShaderPlatform < UE_ARRAY_COUNT(EditorShaderCodeArchive))
		{
			FScopeLock ScopeLock(&ShaderCodeCS);

			FEditorShaderCodeArchive* CodeArchive = EditorShaderCodeArchive[InShaderPlatform];
			if (CodeArchive)	// see the above comment about a bogus SP
			{
				CodeArchive->EndShaderMap();
			}
		}
	}

	void AddShaderCode(EShaderPlatform Platform, const FShaderMapResourceCode* Code)
	{
		FScopeLock ScopeLock(&ShaderCodeCS);
		checkf(Platform < UE_ARRAY_COUNT(EditorShaderCodeStats), TEXT("FShaderCodeLibrary::AddShaderCode can only be called with a valid shader platform (expected no more than %d, passed: %d)"), 
			static_cast<int32>(UE_ARRAY_COUNT(EditorShaderCodeStats)), static_cast<int32>(Platform));
		FShaderCodeStats& CodeStats = EditorShaderCodeStats[Platform];
		CodeStats.NumShaders += Code->ShaderEntries.Num();

		checkf(Platform < UE_ARRAY_COUNT(EditorShaderCodeArchive), TEXT("FShaderCodeLibrary::AddShaderCode can only be called with a valid shader platform (expected no more than %d, passed: %d)"),
			static_cast<int32>(UE_ARRAY_COUNT(EditorShaderCodeArchive)), static_cast<int32>(Platform));
		FEditorShaderCodeArchive* CodeArchive = EditorShaderCodeArchive[Platform];
		check(CodeArchive);

		CodeArchive->AddShaderCode(CodeStats, Code);
	}

	void AddShaderStableKeyValue(EShaderPlatform InShaderPlatform, FStableShaderKeyAndValue& StableKeyValue)
	{
		FEditorShaderStableInfo* StableArchive = EditorShaderStableInfo[InShaderPlatform];
		if (!StableArchive)
		{
			return;
		}

		FScopeLock ScopeLock(&ShaderCodeCS);

		StableKeyValue.ComputeKeyHash();
		StableArchive->AddShader(StableKeyValue);
	}

	bool SaveShaderCode(const FString& ShaderCodeDir, const FString& MetaOutputDir, const TArray<FName>& ShaderFormats, TArray<FString>& OutSCLCSVPath)
	{
		bool bOk = ShaderFormats.Num() > 0;

		FScopeLock ScopeLock(&ShaderCodeCS);

		for (int32 i = 0; i < ShaderFormats.Num(); ++i)
		{
			FName ShaderFormatName = ShaderFormats[i];
			EShaderPlatform SPlatform = ShaderFormatToLegacyShaderPlatform(ShaderFormatName);
			{
				FEditorShaderCodeArchive* CodeArchive = EditorShaderCodeArchive[SPlatform];
				if (CodeArchive)
				{
					bOk &= CodeArchive->Finalize(ShaderCodeDir, MetaOutputDir, bNativeFormat);
				}
			}
			{
				FEditorShaderStableInfo* StableArchive = EditorShaderStableInfo[SPlatform];
				if (StableArchive)
				{
					FString SCLCSVPath;
					bOk &= StableArchive->Finalize(MetaOutputDir, bNativeFormat, SCLCSVPath);
					OutSCLCSVPath.Add(SCLCSVPath);
				}
			}
		}

		return bOk;
	}

	bool PackageNativeShaderLibrary(const FString& ShaderCodeDir, const TArray<FName>& ShaderFormats)
	{
		bool bOK = true;
		for (int32 i = 0; i < ShaderFormats.Num(); ++i)
		{
			FName ShaderFormatName = ShaderFormats[i];
			EShaderPlatform SPlatform = ShaderFormatToLegacyShaderPlatform(ShaderFormatName);
			FEditorShaderCodeArchive* CodeArchive = EditorShaderCodeArchive[SPlatform];

			if (CodeArchive && CodeArchive->GetFormat()->SupportsShaderArchives())
			{
				bOK &= CodeArchive->PackageNativeShaderLibrary(ShaderCodeDir);
			}
		}
		return bOK;
	}

	void DumpShaderCodeStats()
	{
		int32 PlatformId = 0;
		for (const FShaderCodeStats& CodeStats : EditorShaderCodeStats)
		{
			if (CodeStats.NumShaders > 0)
			{
				float UniqueSize = CodeStats.ShadersUniqueSize;
				float UniqueSizeMB = FUnitConversion::Convert(UniqueSize, EUnit::Bytes, EUnit::Megabytes);
				float TotalSize = CodeStats.ShadersSize;
				float TotalSizeMB = FUnitConversion::Convert(TotalSize, EUnit::Bytes, EUnit::Megabytes);

				UE_LOG(LogShaderLibrary, Display, TEXT(""));
				UE_LOG(LogShaderLibrary, Display, TEXT("Shader Code Stats: %s"), *LegacyShaderPlatformToShaderFormat((EShaderPlatform)PlatformId).ToString());
				UE_LOG(LogShaderLibrary, Display, TEXT("================="));
				UE_LOG(LogShaderLibrary, Display, TEXT("Unique Shaders: %d, Total Shaders: %d"), CodeStats.NumUniqueShaders, CodeStats.NumShaders);
				UE_LOG(LogShaderLibrary, Display, TEXT("Unique Shaders Size: %.2fmb, Total Shader Size: %.2fmb"), UniqueSizeMB, TotalSizeMB);
				UE_LOG(LogShaderLibrary, Display, TEXT("-- Layout details --"));
				UE_LOG(LogShaderLibrary, Display, TEXT("Shadermaps: %d, of which belonging to unknown assets %d, unnamed %d"), CodeStats.NumShaderMaps, CodeStats.NumShaderMapsWithUnknownAssets, CodeStats.NumUnnamedShaderMaps);
				UE_LOG(LogShaderLibrary, Display, TEXT("Times we moved shaders closer to the beginning of the file: %d"), CodeStats.TimesShadersMovedCloser);
				UE_LOG(LogShaderLibrary, Display, TEXT("================="));
			}

			PlatformId++;
		}
	}
#endif// WITH_EDITOR
};

static FSharedShaderCodeRequest OnSharedShaderCodeRequest;

FShaderCodeLibraryImpl* FShaderCodeLibraryImpl::Impl = nullptr;

static void FShaderCodeLibraryPluginMountedCallback(IPlugin& Plugin)
{
	if (Plugin.CanContainContent() && Plugin.IsEnabled())
	{
		FShaderCodeLibrary::OpenLibrary(Plugin.GetName(), Plugin.GetBaseDir());
		FShaderCodeLibrary::OpenLibrary(Plugin.GetName(), Plugin.GetContentDir());
	}
}

void FShaderCodeLibrary::InitForRuntime(EShaderPlatform ShaderPlatform)
{
	if (FShaderCodeLibraryImpl::Impl != nullptr)
	{
		//cooked, can't change shader platform on the fly
		check(FShaderCodeLibraryImpl::Impl->GetRuntimeShaderPlatform() == ShaderPlatform);
		return;
	}

	// Cannot be enabled by the server, pointless if we can't ever render and not compatible with cook-on-the-fly
	bool bArchive = false;
	GConfig->GetBool(TEXT("/Script/UnrealEd.ProjectPackagingSettings"), TEXT("bShareMaterialShaderCode"), bArchive, GGameIni);

	// We cannot enable native shader libraries when running with NullRHI, so for consistency all libraries (both native and non-native) are disabled if FApp::CanEverRender() == false
	bool bEnable = !FPlatformProperties::IsServerOnly() && FApp::CanEverRender() && bArchive;
#if !UE_BUILD_SHIPPING
	FString FileHostIP;
	const bool bCookOnTheFly = FParse::Value(FCommandLine::Get(), TEXT("filehostip"), FileHostIP);
	bEnable &= !bCookOnTheFly;
#endif

	if (bEnable)
	{
		FShaderCodeLibraryImpl::Impl = new FShaderCodeLibraryImpl(false);
		if (FShaderCodeLibraryImpl::Impl && FShaderCodeLibraryImpl::Impl->OpenShaderCode(FPaths::ProjectContentDir(), ShaderPlatform, TEXT("Global")))
		{
			IPluginManager::Get().OnNewPluginMounted().AddStatic(&FShaderCodeLibraryPluginMountedCallback);
		
#if !UE_BUILD_SHIPPING
			// support shared cooked builds by also opening the shared cooked build shader code file
			FShaderCodeLibraryImpl::Impl->OpenShaderCode(FPaths::ProjectContentDir(), ShaderPlatform, TEXT("Global_SC"));
#endif

			auto Plugins = IPluginManager::Get().GetEnabledPluginsWithContent();
			for (auto Plugin : Plugins)
			{
				FShaderCodeLibraryPluginMountedCallback(*Plugin);
			}

		}
		else
		{
			Shutdown();
#if !WITH_EDITOR
			if (FPlatformProperties::SupportsWindowedMode())
			{
				FPlatformSplash::Hide();

				UE_LOG(LogShaderLibrary, Error, TEXT("Failed to initialize ShaderCodeLibrary required by the project because part of the Global shader library is missing from %s."), *FPaths::ProjectContentDir());

				FText LocalizedMsg = FText::Format(NSLOCTEXT("MessageDialog", "MissingGlobalShaderLibraryFiles_Body", "Game files required to initialize the global shader library are missing from:\n\n{0}\n\nPlease make sure the game is installed correctly."), FText::FromString(FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir())));
				FPlatformMisc::MessageBoxExt(EAppMsgType::Ok, *LocalizedMsg.ToString(), *NSLOCTEXT("MessageDialog", "MissingGlobalShaderLibraryFiles_Title", "Missing game files").ToString());
			}
			else
			{
				UE_LOG(LogShaderLibrary, Fatal, TEXT("Failed to initialize ShaderCodeLibrary required by the project because part of the Global shader library is missing from %s."), *FPaths::ProjectContentDir());
			}
			FPlatformMisc::RequestExit(true);
#endif // !WITH_EDITOR	
		}
	}
}

void FShaderCodeLibrary::Shutdown()
{
	if (FShaderCodeLibraryImpl::Impl)
	{
#if WITH_EDITOR
		DumpShaderCodeStats();
#endif
		delete FShaderCodeLibraryImpl::Impl;
		FShaderCodeLibraryImpl::Impl = nullptr;
	}
}

bool FShaderCodeLibrary::IsEnabled()
{
	return FShaderCodeLibraryImpl::Impl != nullptr;
}

bool FShaderCodeLibrary::ContainsShaderCode(const FSHAHash& Hash)
{
	if (FShaderCodeLibraryImpl::Impl)
	{
		return FShaderCodeLibraryImpl::Impl->ContainsShaderCode(Hash);
	}
	return false;
}

TRefCountPtr<FShaderMapResource> FShaderCodeLibrary::LoadResource(const FSHAHash& Hash, FArchive* Ar)
{
	if (FShaderCodeLibraryImpl::Impl)
	{
		SCOPED_LOADTIMER(FShaderCodeLibrary_LoadResource);
		OnSharedShaderCodeRequest.Broadcast(Hash, Ar);
		return TRefCountPtr<FShaderMapResource>(FShaderCodeLibraryImpl::Impl->LoadResource(Hash, Ar));
	}
	return TRefCountPtr<FShaderMapResource>();
}

bool FShaderCodeLibrary::PreloadShader(const FSHAHash& Hash, FArchive* Ar)
{
	if (FShaderCodeLibraryImpl::Impl)
	{
		OnSharedShaderCodeRequest.Broadcast(Hash, Ar);
		return FShaderCodeLibraryImpl::Impl->PreloadShader(Hash, Ar);
	}
	return false;
}

FVertexShaderRHIRef FShaderCodeLibrary::CreateVertexShader(EShaderPlatform Platform, const FSHAHash& Hash)
{
	if (FShaderCodeLibraryImpl::Impl)
	{
		return FVertexShaderRHIRef(FShaderCodeLibraryImpl::Impl->CreateShader(SF_Vertex, Hash));
	}
	return nullptr;
}

FPixelShaderRHIRef FShaderCodeLibrary::CreatePixelShader(EShaderPlatform Platform, const FSHAHash& Hash)
{
	if (FShaderCodeLibraryImpl::Impl)
	{
		return FPixelShaderRHIRef(FShaderCodeLibraryImpl::Impl->CreateShader(SF_Pixel, Hash));
	}
	return nullptr;
}

FHullShaderRHIRef FShaderCodeLibrary::CreateHullShader(EShaderPlatform Platform, const FSHAHash& Hash)
{
	if (FShaderCodeLibraryImpl::Impl)
	{
		return FHullShaderRHIRef(FShaderCodeLibraryImpl::Impl->CreateShader(SF_Hull, Hash));
	}
	return nullptr;
}

FDomainShaderRHIRef FShaderCodeLibrary::CreateDomainShader(EShaderPlatform Platform, const FSHAHash& Hash)
{
	if (FShaderCodeLibraryImpl::Impl)
	{
		return FDomainShaderRHIRef(FShaderCodeLibraryImpl::Impl->CreateShader(SF_Domain, Hash));
	}
	return nullptr;
}

FGeometryShaderRHIRef FShaderCodeLibrary::CreateGeometryShader(EShaderPlatform Platform, const FSHAHash& Hash)
{
	if (FShaderCodeLibraryImpl::Impl)
	{
		return FGeometryShaderRHIRef(FShaderCodeLibraryImpl::Impl->CreateShader(SF_Geometry, Hash));
	}
	return nullptr;
}

FComputeShaderRHIRef FShaderCodeLibrary::CreateComputeShader(EShaderPlatform Platform, const FSHAHash& Hash)
{
	if (FShaderCodeLibraryImpl::Impl)
	{
		return FComputeShaderRHIRef(FShaderCodeLibraryImpl::Impl->CreateShader(SF_Compute, Hash));
	}
	return nullptr;
}

uint32 FShaderCodeLibrary::GetShaderCount(void)
{
	uint32 Num = 0;
	if (FShaderCodeLibraryImpl::Impl)
	{
		Num = FShaderCodeLibraryImpl::Impl->GetShaderCount();
	}
	return Num;
}

EShaderPlatform FShaderCodeLibrary::GetRuntimeShaderPlatform(void)
{
	EShaderPlatform Platform = SP_NumPlatforms;
	if (FShaderCodeLibraryImpl::Impl)
	{
		Platform = FShaderCodeLibraryImpl::Impl->GetRuntimeShaderPlatform();
	}
	return Platform;
}

bool FShaderCodeLibrary::OpenLibrary(FString const& Name, FString const& Directory)
{
	bool bResult = false;
	if (FShaderCodeLibraryImpl::Impl)
	{
		bResult = FShaderCodeLibraryImpl::Impl->OpenLibrary(Name, Directory);
	}
	return bResult;
}

void FShaderCodeLibrary::CloseLibrary(FString const& Name)
{
	if (FShaderCodeLibraryImpl::Impl)
	{
		FShaderCodeLibraryImpl::Impl->CloseLibrary(Name);
	}
}

#if WITH_EDITOR
void FShaderCodeLibrary::InitForCooking(bool bNativeFormat)
{
	FShaderCodeLibraryImpl::Impl = new FShaderCodeLibraryImpl(bNativeFormat);
}

void FShaderCodeLibrary::CleanDirectories(TArray<FName> const& ShaderFormats)
{
	if (FShaderCodeLibraryImpl::Impl)
	{
		FShaderCodeLibraryImpl::Impl->CleanDirectories(ShaderFormats);
	}
}

void FShaderCodeLibrary::CookShaderFormats(TArray<FShaderFormatDescriptor> const& ShaderFormats)
{
	if (FShaderCodeLibraryImpl::Impl)
	{
		FShaderCodeLibraryImpl::Impl->CookShaderFormats(ShaderFormats);
	}
}

void FShaderCodeLibrary::BeginShaderMap(EShaderPlatform ShaderPlatform, const TArray<FString>& AssociatedAssets, const FName& ShaderMapTypeName)
{
	if (FShaderCodeLibraryImpl::Impl)
	{
		FShaderCodeLibraryImpl::Impl->BeginShaderMap(ShaderPlatform, AssociatedAssets, ShaderMapTypeName);
	}
}

void FShaderCodeLibrary::EndShaderMap(EShaderPlatform InShaderPlatform)
{
	if (FShaderCodeLibraryImpl::Impl)
	{
		FShaderCodeLibraryImpl::Impl->EndShaderMap(InShaderPlatform);
	}
}

bool FShaderCodeLibrary::AddShaderCode(EShaderPlatform ShaderPlatform, const FShaderMapResourceCode* Code)
{
#if WITH_EDITOR
	if (FShaderCodeLibraryImpl::Impl)
	{
		FShaderCodeLibraryImpl::Impl->AddShaderCode(ShaderPlatform, Code);
		return true;
	}
#endif// WITH_EDITOR

	return false;
}

bool FShaderCodeLibrary::NeedsShaderStableKeys(EShaderPlatform ShaderPlatform)
{
#if WITH_EDITOR
	if (FShaderCodeLibraryImpl::Impl)
	{
		return FShaderCodeLibraryImpl::Impl->NeedsShaderStableKeys(ShaderPlatform);
	}
#endif// WITH_EDITOR
	return false;
}

void FShaderCodeLibrary::AddShaderStableKeyValue(EShaderPlatform ShaderPlatform, FStableShaderKeyAndValue& StableKeyValue)
{
#if WITH_EDITOR
	if (FShaderCodeLibraryImpl::Impl)
	{
		FShaderCodeLibraryImpl::Impl->AddShaderStableKeyValue(ShaderPlatform, StableKeyValue);
	}
#endif// WITH_EDITOR
}

bool FShaderCodeLibrary::SaveShaderCode(const FString& OutputDir, const FString& MetaOutputDir, const TArray<FName>& ShaderFormats, TArray<FString>& OutSCLCSVPath)
{
	if (FShaderCodeLibraryImpl::Impl)
	{
		return FShaderCodeLibraryImpl::Impl->SaveShaderCode(OutputDir, MetaOutputDir, ShaderFormats, OutSCLCSVPath);
	}

	return false;
}

bool FShaderCodeLibrary::PackageNativeShaderLibrary(const FString& ShaderCodeDir, const TArray<FName>& ShaderFormats)
{
	if (FShaderCodeLibraryImpl::Impl)
	{
		return FShaderCodeLibraryImpl::Impl->PackageNativeShaderLibrary(ShaderCodeDir, ShaderFormats);
	}

	return false;
}

void FShaderCodeLibrary::DumpShaderCodeStats()
{
	if (FShaderCodeLibraryImpl::Impl)
	{
		FShaderCodeLibraryImpl::Impl->DumpShaderCodeStats();
	}
}

bool FShaderCodeLibrary::CreatePatchLibrary(TArray<FString> const& OldMetaDataDirs, FString const& NewMetaDataDir, FString const& OutDir, bool bNativeFormat, bool bNeedsDeterministicOrder)
{
	TMap<FName, TSet<FString>> FormatLibraryMap;
	TArray<FString> LibraryFiles;
	IFileManager::Get().FindFiles(LibraryFiles, *(NewMetaDataDir / TEXT("ShaderLibrarySource")), *ShaderExtension);
	
	for (FString const& Path : LibraryFiles)
	{
		FString Name = FPaths::GetBaseFilename(Path);
		if (Name.RemoveFromStart(TEXT("ShaderArchive-")))
		{
			TArray<FString> Components;
			if (Name.ParseIntoArray(Components, TEXT("-")) == 2)
			{
				FName Format(*Components[1]);
				TSet<FString>& Libraries = FormatLibraryMap.FindOrAdd(Format);
				Libraries.Add(Components[0]);
			}
		}
	}
	
	bool bOK = true;
	for (auto const& Entry : FormatLibraryMap)
	{
		for (auto const& Library : Entry.Value)
		{
			bOK |= FEditorShaderCodeArchive::CreatePatchLibrary(Entry.Key, Library, OldMetaDataDirs, NewMetaDataDir, OutDir, bNativeFormat, bNeedsDeterministicOrder);
		}
	}
	return bOK;
}
#endif// WITH_EDITOR

void FShaderCodeLibrary::SafeAssignHash(FRHIShader* InShader, const FSHAHash& Hash)
{
	if (InShader)
	{
		InShader->SetHash(Hash);
	}
}

FDelegateHandle FShaderCodeLibrary::RegisterSharedShaderCodeRequestDelegate_Handle(const FSharedShaderCodeRequest::FDelegate& Delegate)
{
	return OnSharedShaderCodeRequest.Add(Delegate);
}

void FShaderCodeLibrary::UnregisterSharedShaderCodeRequestDelegate_Handle(FDelegateHandle Handle)
{
	OnSharedShaderCodeRequest.Remove(Handle);
}


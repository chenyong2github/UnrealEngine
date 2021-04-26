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
#include "Misc/CoreDelegates.h"

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
#include "PipelineCacheUtilities.h"
#endif

// FORT-93125
#define CHECK_SHADER_CREATION (PLATFORM_XBOXONE && WITH_LEGACY_XDK)

// allow introspection (e.g. dumping the contents) for easier debugging
#define UE_SHADERLIB_WITH_INTROSPECTION			!UE_BUILD_SHIPPING

// In some development-only scenario (e.g. LaunchOn), the library is chunked, but the build isn't pak'd. We need to find the chunk files manually then.
#define UE_SHADERLIB_SUPPORT_CHUNK_DISCOVERY	!UE_BUILD_SHIPPING

DEFINE_LOG_CATEGORY(LogShaderLibrary);

static uint32 GShaderCodeArchiveVersion = 2;
static uint32 GShaderPipelineArchiveVersion = 1;

static FString ShaderExtension = TEXT(".ushaderbytecode");
static FString ShaderAssetInfoExtension = TEXT(".assetinfo.json");
static FString StableExtension = TEXT(".shk");
static FString PipelineExtension = TEXT(".ushaderpipelines");

int32 GShaderCodeLibrarySeperateLoadingCache = 0;
static FAutoConsoleVariableRef CVarShaderCodeLibrarySeperateLoadingCache(
	TEXT("r.ShaderCodeLibrary.SeperateLoadingCache"),
	GShaderCodeLibrarySeperateLoadingCache,
	TEXT("if > 0, each shader code library has it's own loading cache."),
	ECVF_Default
);

class FShaderLibraryInstance;
namespace UE
{
	namespace ShaderLibrary
	{
		namespace Private
		{
			int32 GProduceExtendedStats = 1;
			static FAutoConsoleVariableRef CVarShaderLibraryProduceExtendedStats(
				TEXT("r.ShaderLibrary.PrintExtendedStats"),
				GProduceExtendedStats,
				TEXT("if != 0, shader library will produce extended stats, including the textual representation"),
				ECVF_Default
			);

			/** Helper function shared between the cooker and runtime */
			FString GetShaderLibraryNameForChunk(FString const& BaseName, int32 ChunkId)
			{
				if (ChunkId == INDEX_NONE)
				{
					return BaseName;
				}
				return FString::Printf(TEXT("%s_Chunk%d"), *BaseName, ChunkId);
			}

			// [RCL] TODO 2020-11-20: Separate runtime and editor-only code (tracked as UE-103486)
			/** Descriptor used to pass the pak file information to the library as we cannot store IPakFile ref */
			struct FMountedPakFileInfo
			{
				/** Pak filename (external OS filename) */
				FString PakFilename;
				/** In-game path for the pak content */
				FString MountPoint;
				/** Chunk ID */
				int32 ChunkId;

				// this constructor is used for chunks that we have not yet possibly seen
				FMountedPakFileInfo(int32 InChunkId)
					: PakFilename(TEXT("Fake")),
					ChunkId(InChunkId)
				{
				}

#if UE_SHADERLIB_SUPPORT_CHUNK_DISCOVERY
				FMountedPakFileInfo(const FString& InMountPoint, int32 InChunkId)
					: PakFilename(TEXT("Fake")),
					MountPoint(InMountPoint),
					ChunkId(InChunkId)
				{
				}
#endif // UE_SHADERLIB_SUPPORT_CHUNK_DISCOVERY

				FMountedPakFileInfo(const IPakFile& PakFile)
					: PakFilename(PakFile.PakGetPakFilename()),
					MountPoint(PakFile.PakGetMountPoint()),
					ChunkId(PakFile.PakGetPakchunkIndex())
				{}

				FString ToString() const
				{
					return FString::Printf(TEXT("ChunkID:%d Root:%s File:%s"), ChunkId, *MountPoint, *PakFilename);
				}

				friend uint32 GetTypeHash(const FMountedPakFileInfo& InData)
				{
					return HashCombine(HashCombine(GetTypeHash(InData.PakFilename), GetTypeHash(InData.MountPoint)), ::GetTypeHash(InData.ChunkId));
				}

				friend bool operator==(const FMountedPakFileInfo& A, const FMountedPakFileInfo& B)
				{
					return A.PakFilename == B.PakFilename && A.MountPoint == B.MountPoint && A.ChunkId == B.ChunkId;
				}

				/** Holds a set of all known paks that can be added very early. Each library on Open will traverse that list. */
				static TSet<FMountedPakFileInfo> KnownPakFiles;

				/** Guards access to the list of known pak files*/
				static FCriticalSection KnownPakFilesAccessLock;
			};

			// At runtime, a descriptor of a named library
			struct FNamedShaderLibrary
			{
				/** A name that is passed to Open/CloseLibrary, like "Global", "ShooterGame", "MyPlugin" */
				FString	LogicalName;

				/** Shader platform */
				EShaderPlatform ShaderPlatform;

				/** Base directory for chunk 0 */
				FString BaseDirectory;

				/** Parts of the library corresponding to particular chunk Ids that we have found for this library.
				 *  This is used so we don't try to open a library for the chunk more than once
				 */
				TSet<int32> PresentChunks;

				/** Guards access to components*/
				FRWLock ComponentsMutex;

				/** Even putting aside chunking, each named library can be potentially comprised of multiple files */
				TArray<TUniquePtr<FShaderLibraryInstance>> Components;

				FNamedShaderLibrary(const FString& InLogicalName, const EShaderPlatform InShaderPlatform, const FString& InBaseDirectory)
					: LogicalName(InLogicalName)
					, ShaderPlatform(InShaderPlatform)
					, BaseDirectory(InBaseDirectory)
				{
				}

				int32 GetNumComponents() const
				{
					return Components.Num();
				}

				void OnPakFileMounted(const FMountedPakFileInfo& MountInfo)
				{
					if (!PresentChunks.Contains(MountInfo.ChunkId))
					{
						FString ChunkLibraryName = GetShaderLibraryNameForChunk(LogicalName, MountInfo.ChunkId);

						// Ignore chunk mount point as it's useless in locating the actual library directory. For instance, chunks can
						// have mount points like ../../../ProjectName, while the actual library file is still stored in Content subdirectory.
						// Just use the base directory always and expect the library to be placed in the same location for all chunks
						// (which is the current behavior).
						if (OpenShaderCode(BaseDirectory, ChunkLibraryName))
						{
							PresentChunks.Add(MountInfo.ChunkId);
						}
					}
				}

				bool OpenShaderCode(const FString& ShaderCodeDir, FString const& Library);
				FShaderLibraryInstance* FindShaderLibraryForShaderMap(const FSHAHash& Hash, int32& OutShaderMapIndex);
				FShaderLibraryInstance* FindShaderLibraryForShader(const FSHAHash& Hash, int32& OutShaderIndex);
				uint32 GetShaderCount();
#if UE_SHADERLIB_WITH_INTROSPECTION
				void DumpLibraryContents(const FString& Prefix);
#endif
			};
		}
	}
}

TSet<UE::ShaderLibrary::Private::FMountedPakFileInfo> UE::ShaderLibrary::Private::FMountedPakFileInfo::KnownPakFiles;
FCriticalSection UE::ShaderLibrary::Private::FMountedPakFileInfo::KnownPakFilesAccessLock;

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
	bool bShaderMapPreloaded;
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

static FString GetShaderAssetInfoFilename(const FString& BaseDir, const FString& LibraryName, FName Platform)
{
	return BaseDir / FString::Printf(TEXT("ShaderAssetInfo-%s-"), *LibraryName) + Platform.ToString() + ShaderAssetInfoExtension;
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
	// do not split by '/' as this splits the original FName into per-path components
	UE::String::ParseTokensMultiple(InSrc.TrimStartAndEnd(), {TEXT(' '), TEXT('.'), /*TEXT('/'),*/ TEXT('\t')},
		[&Fields](FStringView Field) { if (!Field.IsEmpty()) { Fields.Add(Field); } });
	if (Fields.Num() == 1 && Fields[0] == TEXT("empty"_SV))
	{
		Fields.Empty();
	}
	// fix up old format that removed the leading '/'
	else if (Fields.Num() == 3 && Fields[1][0] != TEXT('/'))
	{
		ObjectClassAndPath.Empty(3);
		ObjectClassAndPath.Emplace(Fields[0]);
		FString Fixup(TEXT("/"));
		Fixup += Fields[1];
		ObjectClassAndPath.Emplace(Fixup);
		ObjectClassAndPath.Emplace(Fields[2]);
	}
	else
	{
		ObjectClassAndPath.Empty(Fields.Num());
		for (const FStringView& Item : Fields)
		{
			ObjectClassAndPath.Emplace(Item);
		}
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
				BeginReleaseResource(Resource);
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
		FGraphEventArray PreloadCompletionEvents;
		Library->PreloadShader(ShaderIndex, PreloadCompletionEvents);
		if (Ar && PreloadCompletionEvents.Num() > 0)
		{
			FExternalReadCallback ExternalReadCallback = [this, PreloadCompletionEvents = MoveTemp(PreloadCompletionEvents)](double ReaminingTime)
			{
				return this->OnExternalReadCallback(PreloadCompletionEvents, ReaminingTime);
			};
			Ar->AttachExternalReadDependency(ExternalReadCallback);
		}
	}

	TRefCountPtr<FShaderMapResource_SharedCode> GetResource(int32 ShaderMapIndex)
	{
		FRWScopeLock Locker(ResourceLock, SLT_ReadOnly);
		return Resources[ShaderMapIndex];
	}

	TRefCountPtr<FShaderMapResource_SharedCode> AddOrDeleteResource(FShaderMapResource_SharedCode* Resource, FArchive* Ar)
	{
		const int32 ShaderMapIndex = Resource->ShaderMapIndex;
		TRefCountPtr<FShaderMapResource_SharedCode> OutResource(Resource);
		bool bPreload = false;
		{
			FRWScopeLock Locker(ResourceLock, SLT_Write);
			FShaderMapResource_SharedCode* PrevResource = Resources[ShaderMapIndex];
			if (!PrevResource)
			{
				Resources[ShaderMapIndex] = Resource;
				bPreload = !GRHILazyShaderCodeLoading;
			}
			else
			{
				OutResource = PrevResource;
			}
		}

		if (bPreload)
		{
			SCOPED_LOADTIMER(FShaderLibraryInstance_PreloadShaderMap);
			FGraphEventArray PreloadCompletionEvents;
			Resource->bShaderMapPreloaded = Library->PreloadShaderMap(ShaderMapIndex, PreloadCompletionEvents);
			if (Ar && PreloadCompletionEvents.Num() > 0)
			{
				FExternalReadCallback ExternalReadCallback = [this, PreloadCompletionEvents = MoveTemp(PreloadCompletionEvents)](double ReaminingTime)
				{
					return this->OnExternalReadCallback(PreloadCompletionEvents, ReaminingTime);
				};
				Ar->AttachExternalReadDependency(ExternalReadCallback);
			}
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

	bool OnExternalReadCallback(const FGraphEventArray& Events, double RemainingTime)
	{
		if (Events.Num())
		{
			if (RemainingTime < 0.0)
			{
				for (const FGraphEventRef& Event : Events)
				{
					if (!Event->IsComplete()) return false;
				}
				return true;
			}
			FTaskGraphInterface::Get().WaitUntilTasksComplete(Events);
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
	, bShaderMapPreloaded(false)
{
}

FShaderMapResource_SharedCode::~FShaderMapResource_SharedCode()
{
	
}

TRefCountPtr<FRHIShader> FShaderMapResource_SharedCode::CreateRHIShader(int32 ShaderIndex)
{
	SCOPED_LOADTIMER(FShaderMapResource_SharedCode_InitRHI);

	const int32 LibraryShaderIndex = LibraryInstance->Library->GetShaderIndex(ShaderMapIndex, ShaderIndex);
	TRefCountPtr<FRHIShader> ShaderRHI = LibraryInstance->GetOrCreateShader(LibraryShaderIndex);
	if (bShaderMapPreloaded && ShaderRHI)
	{
		// Release our preload, once we've created the shader
		LibraryInstance->Library->ReleasePreloadedShader(LibraryShaderIndex);
	}
	return ShaderRHI;
}

void FShaderMapResource_SharedCode::ReleaseRHI()
{
	const int32 NumShaders = GetNumShaders();
	for (int32 i = 0; i < NumShaders; ++i)
	{
		const int32 LibraryShaderIndex = LibraryInstance->Library->GetShaderIndex(ShaderMapIndex, i);
		if (HasShader(i))
		{
			LibraryInstance->ReleaseShader(LibraryShaderIndex);
		}
		else if (bShaderMapPreloaded)
		{
			// Release the preloaded memory if it was preloaded, but not created yet
			LibraryInstance->Library->ReleasePreloadedShader(LibraryShaderIndex);
		}
	}

	bShaderMapPreloaded = false;

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
	int32 NumShaderMaps;
};

struct FEditorShaderCodeArchive
{
	FEditorShaderCodeArchive(FName InFormat, bool bInNeedsDeterministicOrder)
		: FormatName(InFormat)
		, Format(nullptr)
		, bNeedsDeterministicOrder(bInNeedsDeterministicOrder)
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

	bool IsEmpty() const
	{
		return SerializedShaders.GetNumShaders() == 0;
	}

	int32 AddShaderCode(FShaderCodeStats& CodeStats, const FShaderMapResourceCode* Code, const FShaderMapAssetPaths& AssociatedAssets)
	{
		int32 ShaderMapIndex = INDEX_NONE;

		if (AssociatedAssets.Num() == 0 && LibraryName != TEXT("Global"))
		{
			UE_LOG(LogShaderLibrary, Warning, TEXT("Shadermap %s does not have assets associated with it, library layout may be inconsistent between builds"), *Code->ResourceHash.ToString());
		}

		if (SerializedShaders.FindOrAddShaderMap(Code->ResourceHash, ShaderMapIndex, &AssociatedAssets))
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
				}
				CodeStats.ShadersSize += Code->ShaderEntries[i].Code.Num();
				SerializedShaders.ShaderIndices[ShaderMapEntry.ShaderIndicesOffset + i] = ShaderIndex;
			}

			// for total shaders, only count shaders when we're adding a new shadermap. AddShaderCode() for the same shadermap can be called several times during
			// the cook because of serialization path being reused for other purposes than actual saving, so counting them every time artificially inflates number of shaders.
			CodeStats.NumShaders += Code->ShaderEntries.Num();
			CodeStats.NumShaderMaps++;
		}
		return ShaderMapIndex;
	}

	/** Produces another archive that contains the code only for these assets */
	FEditorShaderCodeArchive* CreateChunk(int ChunkId, const TSet<FName>& PackagesInChunk)
	{
		FEditorShaderCodeArchive* NewChunk = new FEditorShaderCodeArchive(FormatName, bNeedsDeterministicOrder);
		NewChunk->OpenLibrary(UE::ShaderLibrary::Private::GetShaderLibraryNameForChunk(LibraryName, ChunkId));

		TArray<int32> ShaderCodeEntriesNeeded;	// this array is filled with the indices from the existing ShaderCode that will need to be taken
		NewChunk->SerializedShaders.CreateAsChunkFrom(SerializedShaders, PackagesInChunk, ShaderCodeEntriesNeeded);
		// extra integrity check
		checkf(ShaderCodeEntriesNeeded.Num() == NewChunk->SerializedShaders.ShaderHashes.Num(), TEXT("FSerializedShaderArchive for the new chunk did not create a valid shader code mapping"));
		checkf(ShaderCodeEntriesNeeded.Num() == NewChunk->SerializedShaders.ShaderEntries.Num(), TEXT("FSerializedShaderArchive for the new chunk did not create a valid shader code mapping"));

		// copy the shader code
		NewChunk->ShaderCode.Empty();
		for (int32 NewArchiveIdx = 0, NumIndices = ShaderCodeEntriesNeeded.Num(); NewArchiveIdx < NumIndices; ++NewArchiveIdx)
		{
			NewChunk->ShaderCode.Add(ShaderCode[ShaderCodeEntriesNeeded[NewArchiveIdx]]);
		}

		return NewChunk;
	}

	int32 AddShaderCode(int32 OtherShaderMapIndex, const FEditorShaderCodeArchive& OtherArchive)
	{
		int32 ShaderMapIndex = 0;
		if (SerializedShaders.FindOrAddShaderMap(OtherArchive.SerializedShaders.ShaderMapHashes[OtherShaderMapIndex], ShaderMapIndex, 
				OtherArchive.SerializedShaders.ShaderCodeToAssets.Find(OtherArchive.SerializedShaders.ShaderMapHashes[OtherShaderMapIndex])))
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
		if (SerializedShaders.FindOrAddShaderMap(OtherShaders.ShaderMapHashes[OtherShaderMapIndex], ShaderMapIndex, 
				OtherShaders.ShaderCodeToAssets.Find(OtherShaders.ShaderMapHashes[OtherShaderMapIndex])))
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

	void AddExistingShaderCodeLibrary(FString const& OutputDir)
	{
		check(LibraryName.Len() > 0);

		const FString ShaderIntermediateLocation = FPaths::ProjectSavedDir() / TEXT("Shaders") / FormatName.ToString();

		TArray<FString> ShaderFiles;
		IFileManager::Get().FindFiles(ShaderFiles, *ShaderIntermediateLocation, *ShaderExtension);

		for (const FString& ShaderFileName : ShaderFiles)
		{
			if (ShaderFileName.Contains(LibraryName + TEXT("-") + FormatName.ToString() + TEXT(".")))
			{
				FArchive* PrevCookedAr = IFileManager::Get().CreateFileReader(*GetCodeArchiveFilename(OutputDir, LibraryName, FormatName));

				if (PrevCookedAr)
				{
					uint32 Version = 0;
					*PrevCookedAr << Version;

					if (Version == GShaderCodeArchiveVersion)
					{
						FSerializedShaderArchive PrevCookedShaders;

						*PrevCookedAr << PrevCookedShaders;

						// check if it also contains the asset info file
						if (PrevCookedShaders.LoadAssetInfo(GetShaderAssetInfoFilename(OutputDir, LibraryName, FormatName)))
						{
							UE_LOG(LogShaderLibrary, Display, TEXT("Loaded asset info %s for the shader library %s: %d entries"),
								*GetShaderAssetInfoFilename(OutputDir, LibraryName, FormatName),
								*GetCodeArchiveFilename(OutputDir, LibraryName, FormatName),
								PrevCookedShaders.ShaderCodeToAssets.Num()
								);
						}
						else
						{
							UE_LOG(LogShaderLibrary, Warning, TEXT("Could not find or load asset info %s for the shader library %s"),
								*GetShaderAssetInfoFilename(OutputDir, LibraryName, FormatName),
								*GetCodeArchiveFilename(OutputDir, LibraryName, FormatName)
							);
						}

						int64 PrevCookedShadersCodeStart = PrevCookedAr->Tell();
						for (int32 PrevShaderMapIndex = 0; PrevShaderMapIndex < PrevCookedShaders.ShaderMapEntries.Num(); ++PrevShaderMapIndex)
						{
							AddShaderCode(PrevShaderMapIndex, PrevCookedShaders, PrevCookedShadersCodeStart, *PrevCookedAr);
						}
					}

					PrevCookedAr->Close();
					delete PrevCookedAr;
				}
			}
		}
	}

	bool Finalize(FString OutputDir, const FString& MetaOutputDir, bool bSaveOnlyAssetInfo = false, TArray<FString>* OutputFilenames = nullptr)
	{
		check(LibraryName.Len() > 0);

		AddExistingShaderCodeLibrary(OutputDir);

		bool bSuccess = IFileManager::Get().MakeDirectory(*OutputDir, true);

		auto CopyFile = [this](const FString& DestinationPath, const FString& SourcePath, TArray<FString>* OutputFilenames) -> bool
		{
			uint32 Result = IFileManager::Get().Copy(*DestinationPath, *SourcePath, true, true);
			if (Result != COPY_OK)
			{
				UE_LOG(LogShaderLibrary, Error, TEXT("FEditorShaderCodeArchive copying %s to %s failed. Failed to finalize Shared Shader Library %s with format %s"),
					*SourcePath, *DestinationPath, *LibraryName, *FormatName.ToString());
				return false;
			}

			if (OutputFilenames)
			{
				OutputFilenames->Add(DestinationPath);
			}
			return true;
		};

		// Shader library
		if (bSuccess && SerializedShaders.GetNumShaderMaps() > 0)
		{
			// Write to a intermediate file
			FString IntermediateFormatPath = GetShaderCodeFilename(FPaths::ProjectSavedDir() / TEXT("Shaders") / FormatName.ToString(), LibraryName, FormatName);
			FString AssetInfoIntermediatePath = GetShaderAssetInfoFilename(FPaths::ProjectSavedDir() / TEXT("Shaders") / FormatName.ToString(), LibraryName, FormatName);

			// save the actual shader code code
			if (!bSaveOnlyAssetInfo)
			{
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

					// Copy to output location - support for iterative native library cooking
					if (!CopyFile(GetCodeArchiveFilename(OutputDir, LibraryName, FormatName), IntermediateFormatPath, OutputFilenames))
					{
						bSuccess = false;
					}
					else if (MetaOutputDir.Len())
					{
						if (!CopyFile(GetCodeArchiveFilename(MetaOutputDir / TEXT("../ShaderLibrarySource"), LibraryName, FormatName), IntermediateFormatPath, nullptr))
						{
							bSuccess = false;
						}
					}
				}
			}

			// save asset info
			{
				FArchive* AssetInfoWriter = IFileManager::Get().CreateFileWriter(*AssetInfoIntermediatePath, FILEWRITE_NoFail);
				if (AssetInfoWriter)
				{
					SerializedShaders.SaveAssetInfo(*AssetInfoWriter);
					AssetInfoWriter->Close();
					delete AssetInfoWriter;

					if (!CopyFile(GetShaderAssetInfoFilename(OutputDir, LibraryName, FormatName), AssetInfoIntermediatePath, nullptr))
					{
						bSuccess = false;
					}
					else if (MetaOutputDir.Len())
					{
						// copy asset info as well for debugging
						if (!CopyFile(GetShaderAssetInfoFilename(MetaOutputDir / TEXT("../ShaderLibrarySource"), LibraryName, FormatName), AssetInfoIntermediatePath, nullptr))
						{
							bSuccess = false;
						}
					}
				}
			}
		}

		return bSuccess;
	}

	bool PackageNativeShaderLibrary(const FString& ShaderCodeDir, TArray<FString>* OutputFilenames = nullptr)
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
		const bool bOK = Format->CreateShaderArchive(LibraryName, FormatName, TempPath, ShaderCodeDir, IntermediateFormatPath, SerializedShaders, ShaderCode, OutputFilenames);

		if (bOK)
		{
			// Delete Shader code library / pipelines as we now have native versions
			// [RCL] 2020-12-02 FIXME: check if this doesn't ruin iterative cooking during Launch On
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
				bOK = OutLibrary.Finalize(OutDir, Empty);
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

	void DumpStatsAndDebugInfo()
	{
		bool bUseExtendedDebugInfo = UE::ShaderLibrary::Private::GProduceExtendedStats != 0;

		UE_LOG(LogShaderLibrary, Display, TEXT(""));
		UE_LOG(LogShaderLibrary, Display, TEXT("Shader Library '%s' (%s) Stats:"), *LibraryName, *FormatName.ToString());
		UE_LOG(LogShaderLibrary, Display, TEXT("================="));

		FSerializedShaderArchive::FDebugStats Stats;
		FSerializedShaderArchive::FExtendedDebugStats ExtendedStats;
		SerializedShaders.CollectStatsAndDebugInfo(Stats, bUseExtendedDebugInfo ? &ExtendedStats : nullptr);

		UE_LOG(LogShaderLibrary, Display, TEXT("Assets: %d, Unique Shadermaps: %d (%.2f%%)"), 
			Stats.NumAssets, Stats.NumShaderMaps, (Stats.NumAssets > 0) ? 100.0 * static_cast<double>(Stats.NumShaderMaps) / static_cast<double>(Stats.NumAssets) : 0.0);
		UE_LOG(LogShaderLibrary, Display, TEXT("Total Shaders: %d, Unique Shaders: %d (%.2f%%)"), 
			Stats.NumShaders, Stats.NumUniqueShaders, (Stats.NumShaders > 0) ? 100.0 * static_cast<double>(Stats.NumUniqueShaders) / static_cast<double>(Stats.NumShaders) : 0.0);
		UE_LOG(LogShaderLibrary, Display, TEXT("Total Shader Size: %.2fmb, Unique Shaders Size: %.2fmb (%.2f%%)"), 
			FUnitConversion::Convert(static_cast<double>(Stats.ShadersSize), EUnit::Bytes, EUnit::Megabytes), 
			FUnitConversion::Convert(static_cast<double>(Stats.ShadersUniqueSize), EUnit::Bytes, EUnit::Megabytes),
			(Stats.ShadersSize > 0) ? 100.0 * static_cast<double>(Stats.ShadersUniqueSize) / static_cast<double>(Stats.ShadersSize) : 0.0
			);

		if (bUseExtendedDebugInfo)
		{
			UE_LOG(LogShaderLibrary, Display, TEXT("=== Extended info:"));
			UE_LOG(LogShaderLibrary, Display, TEXT("Minimum number of shaders in shadermap: %d"), ExtendedStats.MinNumberOfShadersPerSM);
			UE_LOG(LogShaderLibrary, Display, TEXT("Median number of shaders in shadermap: %d"), ExtendedStats.MedianNumberOfShadersPerSM);
			UE_LOG(LogShaderLibrary, Display, TEXT("Maximum number of shaders in shadermap: %d"), ExtendedStats.MaxNumberofShadersPerSM);
			if (ExtendedStats.TopShaderUsages.Num() > 0)
			{
				FString UsageString;
				UE_LOG(LogShaderLibrary, Display, TEXT("Number of shadermaps referencing top %d most shared shaders:"), ExtendedStats.TopShaderUsages.Num());
				for (int IdxUsage = 0; IdxUsage < ExtendedStats.TopShaderUsages.Num() - 1; ++IdxUsage)
				{
					UsageString += FString::Printf(TEXT("%d, "), ExtendedStats.TopShaderUsages[IdxUsage]);
				}
				UE_LOG(LogShaderLibrary, Display, TEXT("    %s%d"), *UsageString, ExtendedStats.TopShaderUsages[ExtendedStats.TopShaderUsages.Num() - 1]);
			}
			else
			{
				UE_LOG(LogShaderLibrary, Display, TEXT("No shader usage info is provided"));
			}

			FString DebugLibFolder = GetShaderDebugFolder(FPaths::ProjectSavedDir() / TEXT("Shaders") / FormatName.ToString(), LibraryName, FormatName);
			IFileManager::Get().MakeDirectory(*DebugLibFolder, true);

			{
				FString DumpFile = DebugLibFolder / TEXT("Dump.txt");
				TUniquePtr<FArchive> DumpAr(IFileManager::Get().CreateFileWriter(*DumpFile));
				FTCHARToUTF8 Converter(*ExtendedStats.TextualRepresentation);
				DumpAr->Serialize(const_cast<char*>(Converter.Get()), Converter.Length());
				UE_LOG(LogShaderLibrary, Display, TEXT("Textual dump saved to '%s'"), *DumpFile);
			}
#if 0 // creating a graphviz graph - maybe one day we'll return to this
			FString DebugGraphFolder = GetShaderDebugFolder(FPaths::ProjectSavedDir() / TEXT("Shaders") / FormatName.ToString(), LibraryName, FormatName);
			FString DebugGraphFile = DebugGraphFolder / TEXT("RelationshipGraph.gv");

			IFileManager::Get().MakeDirectory(*DebugGraphFolder, true);
			TUniquePtr<FArchive> GraphVizAr(IFileManager::Get().CreateFileWriter(*DebugGraphFile));

			TAnsiStringBuilder<512> LineBuffer;
			LineBuffer << "digraph ShaderLibrary {\n";
			GraphVizAr->Serialize(const_cast<ANSICHAR*>(LineBuffer.ToString()), LineBuffer.Len() * sizeof(ANSICHAR));
			for (TTuple<FString, FString>& Edge : RelationshipGraph)
			{
				LineBuffer.Reset();
				LineBuffer << "\t \"";
				LineBuffer << TCHAR_TO_UTF8(*Edge.Key);
				LineBuffer << "\" -> \"";
				LineBuffer << TCHAR_TO_UTF8(*Edge.Value);
				LineBuffer << "\";\n";
				GraphVizAr->Serialize(const_cast<ANSICHAR*>(LineBuffer.ToString()), LineBuffer.Len() * sizeof(ANSICHAR));
			}
			LineBuffer.Reset();
			LineBuffer << "}\n";
			GraphVizAr->Serialize(const_cast<ANSICHAR*>(LineBuffer.ToString()), LineBuffer.Len() * sizeof(ANSICHAR));
#endif//
		}

		UE_LOG(LogShaderLibrary, Display, TEXT("================="));
	}

private:
	FName FormatName;
	FString LibraryName;
	FSerializedShaderArchive SerializedShaders;
	TArray<TArray<uint8>> ShaderCode;
	const IShaderFormat* Format;
	bool bNeedsDeterministicOrder;
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

	void AddExistingShaderCodeLibrary(FString const& OutputDir)
	{
		check(LibraryName.Len() > 0);

		TMap<uint32, FName> NameCache;
		NameCache.Reserve(2048);

		const FString ShaderIntermediateLocation = FPaths::ProjectSavedDir() / TEXT("Shaders") / FormatName.ToString();

		TArray<FString> ShaderFiles;
		IFileManager::Get().FindFiles(ShaderFiles, *ShaderIntermediateLocation, *ShaderExtension);

		for (const FString& ShaderFileName : ShaderFiles)
		{
			if (ShaderFileName.Contains(LibraryName + TEXT("-") + FormatName.ToString() + TEXT(".")))
			{
				TArray<FStableShaderKeyAndValue> StableKeys;
				if (UE::PipelineCacheUtilities::LoadStableKeysFile(GetStableInfoArchiveFilename(OutputDir, LibraryName, FormatName), StableKeys))
				{
					for (FStableShaderKeyAndValue& Item : StableKeys)
					{
						AddShader(Item);
					}
				}
			}
		}
	}

	bool Finalize(FString OutputDir, FString& OutSCLCSVPath)
	{
		check(LibraryName.Len() > 0);
		OutSCLCSVPath = FString();

		AddExistingShaderCodeLibrary(OutputDir);

		bool bSuccess = IFileManager::Get().MakeDirectory(*OutputDir, true);

		EShaderPlatform Platform = ShaderFormatToLegacyShaderPlatform(FormatName);

		// Shader library
		if (bSuccess && StableMap.Num() > 0)
		{
			// Write to a intermediate file
			FString IntermediateFormatPath = GetStableInfoArchiveFilename(FPaths::ProjectSavedDir() / TEXT("Shaders") / FormatName.ToString(), LibraryName, FormatName);

			// Write directly to the file
			{
				if (!UE::PipelineCacheUtilities::SaveStableKeysFile(IntermediateFormatPath, StableMap))
				{
					UE_LOG(LogShaderLibrary, Error, TEXT("Could not save stable map to file '%s'"), *IntermediateFormatPath);
				}

				// check that it works in a Debug build pm;u
				if (UE_BUILD_DEBUG)
				{
					TArray<FStableShaderKeyAndValue> LoadedBack;
					if (!UE::PipelineCacheUtilities::LoadStableKeysFile(IntermediateFormatPath, LoadedBack))
					{
						UE_LOG(LogShaderLibrary, Error, TEXT("Saved stable map could not be loaded back (from file '%s')"), *IntermediateFormatPath);
					}
					else
					{
						if (LoadedBack.Num() != StableMap.Num())
						{
							UE_LOG(LogShaderLibrary, Error, TEXT("Loaded stable map has a different number of entries (%d) than a saved one (%d)"), LoadedBack.Num(), StableMap.Num());
						}
						else
						{
							for (FStableShaderKeyAndValue& Value : LoadedBack)
							{
								Value.ComputeKeyHash();
								if (!StableMap.Contains(Value))
								{
									UE_LOG(LogShaderLibrary, Error, TEXT("Loaded stable map has an entry that is not present in the saved one"));
									UE_LOG(LogShaderLibrary, Error, TEXT("  %s"), *Value.ToString());
								}
							}
						}
					}
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

class FShaderLibrariesCollection
{
	/** At runtime, this is set to the valid shader platform in use. At cook time, this value is SP_NumPlatforms. */
	EShaderPlatform ShaderPlatform;

	/** At runtime, shader code collection for current shader platform */
	TMap<FString, TUniquePtr<UE::ShaderLibrary::Private::FNamedShaderLibrary>> NamedLibrariesStack;

	/** Mutex that guards the access to the above stack. */
	FRWLock NamedLibrariesMutex;

#if UE_SHADERLIB_WITH_INTROSPECTION
	IConsoleObject* DumpLibraryContentsCmd;
#endif

#if WITH_EDITOR
	FCriticalSection ShaderCodeCS;
	// At cook time, shader code collection for each shader platform
	FEditorShaderCodeArchive* EditorShaderCodeArchive[EShaderPlatform::SP_NumPlatforms];
	// At cook time, whether we saved the shader code archive via SaveShaderLibraryChunk, so we can avoid saving it again in the end.
	// [RCL] FIXME 2020-11-25: this tracking is not perfect as the code in the asset registry performs chunking by ITargetPlatform, whereas if two platforms
	// share the same shader format (e.g. Vulkan on Linux and Windows), we cannot make such a distinction. However, as of now it is a very hypothetical case 
	// as the project settings don't allow disabling chunking for a particular platform.
	TSet<int32> ChunksSaved[EShaderPlatform::SP_NumPlatforms];
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

	/** This function only exists because I'm not able yet to untangle editor and non-editor usage (or rather cooking and not cooking). */
	inline bool IsLibraryInitializedForRuntime() const
	{
#if WITH_EDITOR
		return ShaderPlatform != SP_NumPlatforms;
#else
		// to make it a faster check, for games assume this function is no-op
		checkf(ShaderPlatform != SP_NumPlatforms, TEXT("Shader library has not been properly initialized for a cooked game"));
		return true;
#endif
	}

public:
	static FShaderLibrariesCollection* Impl;

	FShaderLibrariesCollection(EShaderPlatform InShaderPlatform, bool bInNativeFormat)
		: ShaderPlatform(InShaderPlatform)
#if UE_SHADERLIB_WITH_INTROSPECTION
		, DumpLibraryContentsCmd(nullptr)
#endif
		, bSupportsPipelines(false)
		, bNativeFormat(bInNativeFormat)
	{
#if WITH_EDITOR
		FMemory::Memzero(EditorShaderCodeArchive);
		FMemory::Memzero(EditorShaderStableInfo);
		FMemory::Memzero(EditorShaderCodeStats);
		FMemory::Memzero(EditorArchivePipelines);
		for (int32 Idx = 0; Idx < UE_ARRAY_COUNT(ChunksSaved); ++Idx)
		{
			ChunksSaved[Idx].Empty();
		}

		OpenOrderMap = nullptr;
#endif

#if UE_SHADERLIB_WITH_INTROSPECTION
		DumpLibraryContentsCmd = IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("r.ShaderLibrary.Dump"),
			TEXT("Dumps shader library map."),
			FConsoleCommandDelegate::CreateStatic(DumpLibraryContentsStatic),
			ECVF_Default
			);
#endif
	}

	~FShaderLibrariesCollection()
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

#if UE_SHADERLIB_WITH_INTROSPECTION
		if (DumpLibraryContentsCmd)
		{
			IConsoleManager::Get().UnregisterConsoleObject(DumpLibraryContentsCmd);
		}
#endif
	}

	bool OpenLibrary(FString const& Name, FString const& Directory)
	{
		using namespace UE::ShaderLibrary::Private;

		bool bResult = false;

		if (IsLibraryInitializedForRuntime())
		{
			LLM_SCOPE(ELLMTag::Shaders);
			FRWScopeLock WriteLock(NamedLibrariesMutex, SLT_Write);

			// create a named library if one didn't exist
			TUniquePtr<FNamedShaderLibrary>* LibraryPtr = NamedLibrariesStack.Find(Name);
			FNamedShaderLibrary* Library = LibraryPtr ? LibraryPtr->Get() : nullptr;
			const bool bAddNewNamedLibrary(Library == nullptr);
			if (bAddNewNamedLibrary)
			{
				Library = new FNamedShaderLibrary(Name, ShaderPlatform, Directory);
			}

			// if we're able to open the library by name, it's not chunked
			if (Library->OpenShaderCode(Directory, Name))
			{
				bResult = true;

				// Attempt to open the shared-cooked override code library if there is one.
				// This is probably not ideal, but it should get shared-cooks working.
				Library->OpenShaderCode(Directory, Name + TEXT("_SC"));
			}
			else // attempt to open a chunked library
			{
				int32 PrevNumComponents = Library->GetNumComponents();

				{
					FScopeLock KnownPakFilesLocker(&FMountedPakFileInfo::KnownPakFilesAccessLock);
					for (TSet<FMountedPakFileInfo>::TConstIterator Iter(FMountedPakFileInfo::KnownPakFiles); Iter; ++Iter)
					{
						Library->OnPakFileMounted(*Iter);
					}
				}

				bResult = (Library->GetNumComponents() > PrevNumComponents);

#if UE_SHADERLIB_SUPPORT_CHUNK_DISCOVERY
				if (!bResult)
				{
					// Some deployment flows (e.g. Launch on) avoid pak files despite project packaging settings. 
					// In case we run under such circumstances, we need to discover the components ourselves
					if (FPlatformFileManager::Get().FindPlatformFile(TEXT("PakFile")) == nullptr)
					{
						UE_LOG(LogShaderLibrary, Display, TEXT("Running without a pakfile and did not find a monolithic library '%s' - attempting disk search for its chunks"), *Name, Library->GetNumComponents());

						TArray<FString> UshaderbytecodeFiles;
						FString SearchMask = Directory / FString::Printf(TEXT("ShaderArchive-*%s*.ushaderbytecode"), *Name);
						IFileManager::Get().FindFiles(UshaderbytecodeFiles, *SearchMask, true, false);

						if (UshaderbytecodeFiles.Num() > 0)
						{
							UE_LOG(LogShaderLibrary, Display, TEXT("   ....  found %d files"), UshaderbytecodeFiles.Num());
							for (const FString& Filename : UshaderbytecodeFiles)
							{
								const TCHAR* ChunkSubstring = TEXT("_Chunk");
								const int kChunkSubstringSize = 6; // stlren(ChunkSubstring)
								int32 ChunkSuffix = Filename.Find(ChunkSubstring, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
								if (ChunkSuffix != INDEX_NONE && ChunkSuffix + kChunkSubstringSize < Filename.Len())
								{
									const TCHAR* ChunkIDString = &Filename[ChunkSuffix + kChunkSubstringSize];
									int32 ChunkID = FCString::Atoi(ChunkIDString);
									if (ChunkID >= 0)
									{
										// create a fake FPakFileMountedInfo
										FMountedPakFileInfo PakFileInfo(Directory, ChunkID);
										Library->OnPakFileMounted(PakFileInfo);
									}
								}
							}

							bResult = (Library->GetNumComponents() > PrevNumComponents);
						}
						else
						{
							UE_LOG(LogShaderLibrary, Display, TEXT("   ....  not found"));
						}
					}
				}
#endif // UE_SHADERLIB_SUPPORT_CHUNK_DISCOVERY
			}

			if (bResult)
			{
				if (bAddNewNamedLibrary)
				{
					UE_LOG(LogShaderLibrary, Display, TEXT("Logical shader library '%s' has been created, components %d"), *Name, Library->GetNumComponents());
					NamedLibrariesStack.Emplace(Name, Library);
				}

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
		if (IsLibraryInitializedForRuntime())
		{
			FRWScopeLock WriteLock(NamedLibrariesMutex, SLT_Write);
			TUniquePtr<UE::ShaderLibrary::Private::FNamedShaderLibrary> RemovedLibrary = nullptr;
			NamedLibrariesStack.RemoveAndCopyValue(Name, RemovedLibrary);
			if (RemovedLibrary)
			{
				UE_LOG(LogShaderLibrary, Display, TEXT("Closing logical shader library '%s' with %d components"), *Name, RemovedLibrary->GetNumComponents());
				RemovedLibrary = nullptr;
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
			ChunksSaved[i].Empty();
		}
#endif
	}

	void OnPakFileMounted(const UE::ShaderLibrary::Private::FMountedPakFileInfo& MountInfo)
	{		
		if (IsLibraryInitializedForRuntime())
		{
			FRWScopeLock WriteLock(NamedLibrariesMutex, SLT_Write);
			for (TTuple<FString, TUniquePtr<UE::ShaderLibrary::Private::FNamedShaderLibrary>>& NamedLibraryPair : NamedLibrariesStack)
			{
				NamedLibraryPair.Value->OnPakFileMounted(MountInfo);
			}
		}
	}

	uint32 GetShaderCount(void)
	{
		FRWScopeLock ReadLock(NamedLibrariesMutex, SLT_ReadOnly);

		int32 ShaderCount = 0;
		for (TTuple<FString, TUniquePtr<UE::ShaderLibrary::Private::FNamedShaderLibrary>>& NamedLibraryPair : NamedLibrariesStack)
		{
			ShaderCount += NamedLibraryPair.Value->GetShaderCount();
		}
		return ShaderCount;
	}

#if UE_SHADERLIB_WITH_INTROSPECTION
	static void DumpLibraryContentsStatic()
	{
		if (FShaderLibrariesCollection::Impl)
		{
			FShaderLibrariesCollection::Impl->DumpLibraryContents();
		}
	}

	void DumpLibraryContents()
	{
		FRWScopeLock ReadLock(NamedLibrariesMutex, SLT_ReadOnly);

		UE_LOG(LogShaderLibrary, Display, TEXT("==== Dumping shader library contents ===="));
		UE_LOG(LogShaderLibrary, Display, TEXT("Shader platform (EShaderPlatform) is %d"), static_cast<int32>(ShaderPlatform));
		UE_LOG(LogShaderLibrary, Display, TEXT("%d named libraries open with %d shaders total"), NamedLibrariesStack.Num(), GetShaderCount());
		int32 LibraryIdx = 0;
		for (TTuple<FString, TUniquePtr<UE::ShaderLibrary::Private::FNamedShaderLibrary>>& NamedLibraryPair : NamedLibrariesStack)
		{
			UE_LOG(LogShaderLibrary, Display, TEXT("%d: Name='%s' Shaders %d Components %d"), 
				LibraryIdx, *NamedLibraryPair.Key, NamedLibraryPair.Value->GetShaderCount(), NamedLibraryPair.Value->GetNumComponents());

			NamedLibraryPair.Value->DumpLibraryContents(TEXT("  "));

			++LibraryIdx;
		}
		UE_LOG(LogShaderLibrary, Display, TEXT("==== End of shader library dump ===="));
	}
#endif

	EShaderPlatform GetRuntimeShaderPlatform(void)
	{
		return ShaderPlatform;
	}

	FShaderLibraryInstance* FindShaderLibraryForShaderMap(const FSHAHash& Hash, int32& OutShaderMapIndex)
	{
		FRWScopeLock ReadLock(NamedLibrariesMutex, SLT_ReadOnly);

		for (TTuple<FString, TUniquePtr<UE::ShaderLibrary::Private::FNamedShaderLibrary>>& NamedLibraryPair : NamedLibrariesStack)
		{
			FShaderLibraryInstance* Instance = NamedLibraryPair.Value->FindShaderLibraryForShaderMap(Hash, OutShaderMapIndex);
			if (Instance)
			{
				return Instance;
			}
		}
		return nullptr;
	}

	FShaderLibraryInstance* FindShaderLibraryForShader(const FSHAHash& Hash, int32& OutShaderIndex)
	{
		FRWScopeLock ReadLock(NamedLibrariesMutex, SLT_ReadOnly);

		for (TTuple<FString, TUniquePtr<UE::ShaderLibrary::Private::FNamedShaderLibrary>>& NamedLibraryPair : NamedLibrariesStack)
		{
			FShaderLibraryInstance* Instance = NamedLibraryPair.Value->FindShaderLibraryForShader(Hash, OutShaderIndex);
			if (Instance)
			{
				return Instance;
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
				Resource = LibraryInstance->AddOrDeleteResource(new FShaderMapResource_SharedCode(LibraryInstance, ShaderMapIndex), Ar);
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

	void CookShaderFormats(TArray<FShaderLibraryCooker::FShaderFormatDescriptor> const& ShaderFormats)
	{
		bool bAtLeastOneFormatNeedsDeterminism = false;

		for (const FShaderLibraryCooker::FShaderFormatDescriptor& Descriptor : ShaderFormats)
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
		for (const FShaderLibraryCooker::FShaderFormatDescriptor& Descriptor : ShaderFormats)
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
					FString LogFileDirectory = FPaths::Combine(FPlatformMisc::ProjectDir(), TEXT("Platforms"), *PlatformNames[Platform], TEXT("Build"), TEXT("FileOpenOrder"));
					if (!FPaths::DirectoryExists(LogFileDirectory))
					{
						LogFileDirectory = FPaths::Combine(FPlatformMisc::ProjectDir(), TEXT("Build"), *PlatformNames[Platform], TEXT("FileOpenOrder"));
					}
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

	void AddShaderCode(EShaderPlatform Platform, const FShaderMapResourceCode* Code, const FShaderMapAssetPaths& AssociatedAssets)
	{
		FScopeLock ScopeLock(&ShaderCodeCS);
		checkf(Platform < UE_ARRAY_COUNT(EditorShaderCodeStats), TEXT("FShaderCodeLibrary::AddShaderCode can only be called with a valid shader platform (expected no more than %d, passed: %d)"), 
			static_cast<int32>(UE_ARRAY_COUNT(EditorShaderCodeStats)), static_cast<int32>(Platform));
		FShaderCodeStats& CodeStats = EditorShaderCodeStats[Platform];

		checkf(Platform < UE_ARRAY_COUNT(EditorShaderCodeArchive), TEXT("FShaderCodeLibrary::AddShaderCode can only be called with a valid shader platform (expected no more than %d, passed: %d)"),
			static_cast<int32>(UE_ARRAY_COUNT(EditorShaderCodeArchive)), static_cast<int32>(Platform));
		FEditorShaderCodeArchive* CodeArchive = EditorShaderCodeArchive[Platform];
		checkf(CodeArchive, TEXT("EditorShaderCodeArchive for (EShaderPlatform)%d is null!"), (int32)Platform);

		CodeArchive->AddShaderCode(CodeStats, Code, AssociatedAssets);
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

			FEditorShaderCodeArchive* CodeArchive = EditorShaderCodeArchive[SPlatform];
			if (CodeArchive)
			{
				// If we saved the shader code while generating the chunk, do not save a single consolidated library as it should not be used and
				// will only bloat the build.
				// Still save the full asset info for debugging
				if (ChunksSaved[SPlatform].Num() == 0)
				{
					// always save shaders in our format even if the platform will use native one. This is needed for iterative cooks (Launch On et al)
					// to reload previously cooked shaders
					bOk = CodeArchive->Finalize(ShaderCodeDir, MetaOutputDir) && bOk;

					bool bShouldWriteInNativeFormat = bOk && bNativeFormat && CodeArchive->GetFormat()->SupportsShaderArchives();
					if (bShouldWriteInNativeFormat)
					{
						bOk = CodeArchive->PackageNativeShaderLibrary(ShaderCodeDir) && bOk;
					}

					if (bOk)
					{
						CodeArchive->DumpStatsAndDebugInfo();
					}
				}
				else
				{
					// save asset info only, for debugging
					bOk = CodeArchive->Finalize(ShaderCodeDir, MetaOutputDir, true) && bOk;
				}
			}
			// Stable shader info is not saved per-chunk (it is not needed runtime), so save it always
			{
				FEditorShaderStableInfo* StableArchive = EditorShaderStableInfo[SPlatform];
				if (StableArchive)
				{
					FString SCLCSVPath;
					bOk &= StableArchive->Finalize(MetaOutputDir, SCLCSVPath);
					OutSCLCSVPath.Add(SCLCSVPath);
				}
			}
		}

		return bOk;
	}

	bool SaveShaderCodeChunk(int32 ChunkId, const TSet<FName>& InPackagesInChunk, const TArray<FName>& ShaderFormats, const FString& SandboxDestinationPath, const FString& SandboxMetadataPath, TArray<FString>& OutChunkFilenames)
	{
		bool bOk = ShaderFormats.Num() > 0;

		FScopeLock ScopeLock(&ShaderCodeCS);

		for (int32 i = 0; i < ShaderFormats.Num(); ++i)
		{
			FName ShaderFormatName = ShaderFormats[i];
			EShaderPlatform SPlatform = ShaderFormatToLegacyShaderPlatform(ShaderFormatName);

			// we may get duplicate calls for the same Chunk Id because the cooker sometimes calls asset registry SaveManifests twice.
			if (ChunksSaved[SPlatform].Contains(ChunkId))
			{
				continue;
			}

			FEditorShaderCodeArchive* OrginalCodeArchive = EditorShaderCodeArchive[SPlatform];
			if (!OrginalCodeArchive)
			{
				bOk = false;
				break;
			}

			FEditorShaderCodeArchive* PerChunkArchive = OrginalCodeArchive->CreateChunk(ChunkId, InPackagesInChunk);
			if (!PerChunkArchive)
			{
				bOk = false;
				break;
			}

			// skip saving if no shaders are actually stored
			if (!PerChunkArchive->IsEmpty())
			{

				// always save shaders in our format even if the platform will use native one. This is needed for iterative cooks (Launch On et al)
				// to reload previously cooked shaders
				bOk = PerChunkArchive->Finalize(SandboxDestinationPath, SandboxMetadataPath, false, &OutChunkFilenames) && bOk;

				bool bShouldWriteInNativeFormat = bOk && bNativeFormat && PerChunkArchive->GetFormat()->SupportsShaderArchives();
				if (bShouldWriteInNativeFormat)
				{
					bOk = PerChunkArchive->PackageNativeShaderLibrary(SandboxDestinationPath, &OutChunkFilenames) && bOk;
				}

				if (bOk)
				{
					PerChunkArchive->DumpStatsAndDebugInfo();
					ChunksSaved[SPlatform].Add(ChunkId);
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
				UE_LOG(LogShaderLibrary, Display, TEXT("Unique Shaders: %d, Total Shaders: %d, Unique Shadermaps: %d"), CodeStats.NumUniqueShaders, CodeStats.NumShaders, CodeStats.NumShaderMaps);
				UE_LOG(LogShaderLibrary, Display, TEXT("Unique Shaders Size: %.2fmb, Total Shader Size: %.2fmb"), UniqueSizeMB, TotalSizeMB);
				UE_LOG(LogShaderLibrary, Display, TEXT("================="));
			}

			PlatformId++;
		}
	}
#endif// WITH_EDITOR
};

static FSharedShaderCodeRequest OnSharedShaderCodeRequest;

FShaderLibrariesCollection* FShaderLibrariesCollection::Impl = nullptr;

static void FShaderCodeLibraryPluginMountedCallback(IPlugin& Plugin)
{
	if (Plugin.CanContainContent() && Plugin.IsEnabled())
	{
		FShaderCodeLibrary::OpenLibrary(Plugin.GetName(), Plugin.GetBaseDir());
		FShaderCodeLibrary::OpenLibrary(Plugin.GetName(), Plugin.GetContentDir());
	}
}

static void FShaderLibraryPakFileMountedCallback(const IPakFile& PakFile)
{
	using namespace UE::ShaderLibrary::Private;

	int32 NewChunk = PakFile.PakGetPakchunkIndex();
	UE_LOG(LogShaderLibrary, Display, TEXT("ShaderCodeLibraryPakFileMountedCallback: PakFile '%s' (chunk index %d, root '%s') mounted"), *PakFile.PakGetPakFilename(), PakFile.PakGetPakchunkIndex(), *PakFile.PakGetMountPoint());

	FMountedPakFileInfo PakFileInfo(PakFile);
	{
		FScopeLock PakFilesLocker(&FMountedPakFileInfo::KnownPakFilesAccessLock);
		FMountedPakFileInfo::KnownPakFiles.Add(PakFileInfo);
	}

	// if shaderlibrary has not yet been initialized, add the chunk as pending
	if (FShaderLibrariesCollection::Impl)
	{
		FShaderLibrariesCollection::Impl->OnPakFileMounted(PakFileInfo);
	}
	else
	{
		UE_LOG(LogShaderLibrary, Display, TEXT("ShaderCodeLibraryPakFileMountedCallback: pending pak file info (%s)"), *PakFileInfo.ToString());
	}
}

void FShaderCodeLibrary::PreInit()
{
	// add a callback for opening later chunks
	FCoreDelegates::OnPakFileMounted2.AddStatic(&FShaderLibraryPakFileMountedCallback);
}

void FShaderCodeLibrary::InitForRuntime(EShaderPlatform ShaderPlatform)
{
	if (FShaderLibrariesCollection::Impl != nullptr)
	{
		//cooked, can't change shader platform on the fly
		check(FShaderLibrariesCollection::Impl->GetRuntimeShaderPlatform() == ShaderPlatform);
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
		FShaderLibrariesCollection::Impl = new FShaderLibrariesCollection(ShaderPlatform, false);
		if (FShaderLibrariesCollection::Impl->OpenLibrary(TEXT("Global"), FPaths::ProjectContentDir()))
		{
			IPluginManager::Get().OnNewPluginMounted().AddStatic(&FShaderCodeLibraryPluginMountedCallback);
		
#if !UE_BUILD_SHIPPING
			// support shared cooked builds by also opening the shared cooked build shader code file
			FShaderLibrariesCollection::Impl->OpenLibrary(TEXT("Global_SC"), FPaths::ProjectContentDir());
#endif

			// mount shader library from the plugins as they may also have global shaders
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
	if (FShaderLibrariesCollection::Impl)
	{
		delete FShaderLibrariesCollection::Impl;
		FShaderLibrariesCollection::Impl = nullptr;
	}

	FScopeLock PakFilesLocker(&UE::ShaderLibrary::Private::FMountedPakFileInfo::KnownPakFilesAccessLock);
	UE::ShaderLibrary::Private::FMountedPakFileInfo::KnownPakFiles.Empty();
}

bool FShaderCodeLibrary::IsEnabled()
{
	return FShaderLibrariesCollection::Impl != nullptr;
}

bool FShaderCodeLibrary::ContainsShaderCode(const FSHAHash& Hash)
{
	if (FShaderLibrariesCollection::Impl)
	{
		return FShaderLibrariesCollection::Impl->ContainsShaderCode(Hash);
	}
	return false;
}

TRefCountPtr<FShaderMapResource> FShaderCodeLibrary::LoadResource(const FSHAHash& Hash, FArchive* Ar)
{
	if (FShaderLibrariesCollection::Impl)
	{
		SCOPED_LOADTIMER(FShaderCodeLibrary_LoadResource);
		OnSharedShaderCodeRequest.Broadcast(Hash, Ar);
		return TRefCountPtr<FShaderMapResource>(FShaderLibrariesCollection::Impl->LoadResource(Hash, Ar));
	}
	return TRefCountPtr<FShaderMapResource>();
}

bool FShaderCodeLibrary::PreloadShader(const FSHAHash& Hash, FArchive* Ar)
{
	if (FShaderLibrariesCollection::Impl)
	{
		OnSharedShaderCodeRequest.Broadcast(Hash, Ar);
		return FShaderLibrariesCollection::Impl->PreloadShader(Hash, Ar);
	}
	return false;
}

FVertexShaderRHIRef FShaderCodeLibrary::CreateVertexShader(EShaderPlatform Platform, const FSHAHash& Hash)
{
	if (FShaderLibrariesCollection::Impl)
	{
		return FVertexShaderRHIRef(FShaderLibrariesCollection::Impl->CreateShader(SF_Vertex, Hash));
	}
	return nullptr;
}

FPixelShaderRHIRef FShaderCodeLibrary::CreatePixelShader(EShaderPlatform Platform, const FSHAHash& Hash)
{
	if (FShaderLibrariesCollection::Impl)
	{
		return FPixelShaderRHIRef(FShaderLibrariesCollection::Impl->CreateShader(SF_Pixel, Hash));
	}
	return nullptr;
}

FHullShaderRHIRef FShaderCodeLibrary::CreateHullShader(EShaderPlatform Platform, const FSHAHash& Hash)
{
	if (FShaderLibrariesCollection::Impl)
	{
		return FHullShaderRHIRef(FShaderLibrariesCollection::Impl->CreateShader(SF_Hull, Hash));
	}
	return nullptr;
}

FDomainShaderRHIRef FShaderCodeLibrary::CreateDomainShader(EShaderPlatform Platform, const FSHAHash& Hash)
{
	if (FShaderLibrariesCollection::Impl)
	{
		return FDomainShaderRHIRef(FShaderLibrariesCollection::Impl->CreateShader(SF_Domain, Hash));
	}
	return nullptr;
}

FGeometryShaderRHIRef FShaderCodeLibrary::CreateGeometryShader(EShaderPlatform Platform, const FSHAHash& Hash)
{
	if (FShaderLibrariesCollection::Impl)
	{
		return FGeometryShaderRHIRef(FShaderLibrariesCollection::Impl->CreateShader(SF_Geometry, Hash));
	}
	return nullptr;
}

FComputeShaderRHIRef FShaderCodeLibrary::CreateComputeShader(EShaderPlatform Platform, const FSHAHash& Hash)
{
	if (FShaderLibrariesCollection::Impl)
	{
		return FComputeShaderRHIRef(FShaderLibrariesCollection::Impl->CreateShader(SF_Compute, Hash));
	}
	return nullptr;
}

FRayTracingShaderRHIRef FShaderCodeLibrary::CreateRayTracingShader(EShaderPlatform Platform, const FSHAHash& Hash, EShaderFrequency Frequency)
{
	if (FShaderLibrariesCollection::Impl)
	{
		check(Frequency >= SF_RayGen && Frequency <= SF_RayCallable);
		return FRayTracingShaderRHIRef(FShaderLibrariesCollection::Impl->CreateShader(Frequency, Hash));
	}
	return nullptr;
}

uint32 FShaderCodeLibrary::GetShaderCount(void)
{
	uint32 Num = 0;
	if (FShaderLibrariesCollection::Impl)
	{
		Num = FShaderLibrariesCollection::Impl->GetShaderCount();
	}
	return Num;
}

EShaderPlatform FShaderCodeLibrary::GetRuntimeShaderPlatform(void)
{
	EShaderPlatform Platform = SP_NumPlatforms;
	if (FShaderLibrariesCollection::Impl)
	{
		Platform = FShaderLibrariesCollection::Impl->GetRuntimeShaderPlatform();
	}
	return Platform;
}

void FShaderCodeLibrary::AddKnownChunkIDs(const int32* IDs, const int32 NumChunkIDs)
{
	using namespace UE::ShaderLibrary::Private;

	checkf(IDs, TEXT("Invalid pointer to chunk IDs passed"));
	UE_LOG(LogShaderLibrary, Display, TEXT("AddKnownChunkIDs: adding %d chunk IDs"), NumChunkIDs);

	for (int32 IdxChunkId = 0; IdxChunkId < NumChunkIDs; ++IdxChunkId)
	{
		FMountedPakFileInfo PakFileInfo(IDs[IdxChunkId]);
		{
			FScopeLock PakFilesLocker(&FMountedPakFileInfo::KnownPakFilesAccessLock);
			FMountedPakFileInfo::KnownPakFiles.Add(PakFileInfo);
		}

		// if shaderlibrary has not yet been initialized, add the chunk as pending
		if (FShaderLibrariesCollection::Impl)
		{
			FShaderLibrariesCollection::Impl->OnPakFileMounted(PakFileInfo);
		}
		else
		{
			UE_LOG(LogShaderLibrary, Display, TEXT("AddKnownChunkIDs: pending pak file info (%s)"), *PakFileInfo.ToString());
		}
	}
}

bool FShaderCodeLibrary::OpenLibrary(FString const& Name, FString const& Directory)
{
	bool bResult = false;
	if (FShaderLibrariesCollection::Impl)
	{
		bResult = FShaderLibrariesCollection::Impl->OpenLibrary(Name, Directory);
	}
	return bResult;
}

void FShaderCodeLibrary::CloseLibrary(FString const& Name)
{
	if (FShaderLibrariesCollection::Impl)
	{
		FShaderLibrariesCollection::Impl->CloseLibrary(Name);
	}
}

#if WITH_EDITOR
// for now a lot of FShaderLibraryCooker code is aliased with the runtime code, but this will be refactored (UE-103486)
void FShaderLibraryCooker::InitForCooking(bool bNativeFormat)
{
	FShaderLibrariesCollection::Impl = new FShaderLibrariesCollection(SP_NumPlatforms, bNativeFormat);
}

void FShaderLibraryCooker::Shutdown()
{
	if (FShaderLibrariesCollection::Impl)
	{
		//DumpShaderCodeStats();

		delete FShaderLibrariesCollection::Impl;
		FShaderLibrariesCollection::Impl = nullptr;
	}
}

void FShaderLibraryCooker::CleanDirectories(TArray<FName> const& ShaderFormats)
{
	if (FShaderLibrariesCollection::Impl)
	{
		FShaderLibrariesCollection::Impl->CleanDirectories(ShaderFormats);
	}
}

bool FShaderLibraryCooker::BeginCookingLibrary(FString const& Name)
{
	// for now this is aliased with the runtime code, but this will be refactored (UE-103486)
	bool bResult = false;
	if (FShaderLibrariesCollection::Impl)
	{
		bResult = FShaderLibrariesCollection::Impl->OpenLibrary(Name, TEXT(""));
	}
	return bResult;
}

void FShaderLibraryCooker::EndCookingLibrary(FString const& Name)
{
	// for now this is aliased with the runtime code, but this will be refactored (UE-103486)
	if (FShaderLibrariesCollection::Impl)
	{
		FShaderLibrariesCollection::Impl->CloseLibrary(Name);
	}
}

bool FShaderLibraryCooker::IsShaderLibraryEnabled()
{
	return FShaderLibrariesCollection::Impl != nullptr;
}

void FShaderLibraryCooker::CookShaderFormats(TArray<FShaderFormatDescriptor> const& ShaderFormats)
{
	if (FShaderLibrariesCollection::Impl)
	{
		FShaderLibrariesCollection::Impl->CookShaderFormats(ShaderFormats);
	}
}

bool FShaderLibraryCooker::AddShaderCode(EShaderPlatform ShaderPlatform, const FShaderMapResourceCode* Code, const FShaderMapAssetPaths& AssociatedAssets)
{
	if (FShaderLibrariesCollection::Impl)
	{
		FShaderLibrariesCollection::Impl->AddShaderCode(ShaderPlatform, Code, AssociatedAssets);
		return true;
	}
	return false;
}

bool FShaderLibraryCooker::NeedsShaderStableKeys(EShaderPlatform ShaderPlatform)
{
	if (FShaderLibrariesCollection::Impl)
	{
		return FShaderLibrariesCollection::Impl->NeedsShaderStableKeys(ShaderPlatform);
	}
	return false;
}

void FShaderLibraryCooker::AddShaderStableKeyValue(EShaderPlatform ShaderPlatform, FStableShaderKeyAndValue& StableKeyValue)
{
	if (FShaderLibrariesCollection::Impl)
	{
		FShaderLibrariesCollection::Impl->AddShaderStableKeyValue(ShaderPlatform, StableKeyValue);
	}
}

void FShaderLibraryCooker::DumpShaderCodeStats()
{
	if (FShaderLibrariesCollection::Impl)
	{
		FShaderLibrariesCollection::Impl->DumpShaderCodeStats();
	}
}

bool FShaderLibraryCooker::CreatePatchLibrary(TArray<FString> const& OldMetaDataDirs, FString const& NewMetaDataDir, FString const& OutDir, bool bNativeFormat, bool bNeedsDeterministicOrder)
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

bool FShaderLibraryCooker::SaveShaderLibraryWithoutChunking(const ITargetPlatform* TargetPlatform, FString const& Name, FString const& SandboxDestinationPath, FString const& SandboxMetadataPath, TArray<FString>& PlatformSCLCSVPaths, FString& OutErrorMessage)
{
	const FString ActualName = Name;
	const FString ShaderCodeDir = SandboxDestinationPath;
	const FString MetaDataPath = SandboxMetadataPath;

	checkf(FShaderLibrariesCollection::Impl != nullptr, TEXT("FShaderLibraryCooker was not initialized properly"));
	checkf(TargetPlatform, TEXT("A valid TargetPlatform is expected"));

	// note that shader formats can be shared across the target platforms
	TArray<FName> ShaderFormats;
	TargetPlatform->GetAllTargetedShaderFormats(ShaderFormats);
	if (ShaderFormats.Num() > 0)
	{
		FString TargetPlatformName = TargetPlatform->PlatformName();
		bool bSaved = FShaderLibrariesCollection::Impl->SaveShaderCode(ShaderCodeDir, MetaDataPath, ShaderFormats, PlatformSCLCSVPaths);

		if (UNLIKELY(!bSaved))
		{
			OutErrorMessage = FString::Printf(TEXT("Saving shared material shader code library failed for %s."), *TargetPlatformName);
			return false;
		}
	}

	return true;
}

bool FShaderLibraryCooker::SaveShaderLibraryChunk(int32 ChunkId, const TSet<FName>& InPackagesInChunk, const ITargetPlatform* TargetPlatform, const FString& SandboxDestinationPath, const FString& SandboxMetadataPath, TArray<FString>& OutChunkFilenames)
{
	checkf(FShaderLibrariesCollection::Impl != nullptr, TEXT("FShaderLibraryCooker was not initialized properly"));
	checkf(TargetPlatform, TEXT("A valid TargetPlatform is expected"));
	bool bResult = true;

	TArray<FName> ShaderFormats;
	TargetPlatform->GetAllTargetedShaderFormats(ShaderFormats);
	if (ShaderFormats.Num() > 0)
	{
		bResult = FShaderLibrariesCollection::Impl->SaveShaderCodeChunk(ChunkId, InPackagesInChunk, ShaderFormats, SandboxDestinationPath, SandboxMetadataPath, OutChunkFilenames);
	}
	return bResult;
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

// FNamedShaderLibrary methods

// At runtime, open shader code collection for specified shader platform
bool UE::ShaderLibrary::Private::FNamedShaderLibrary::OpenShaderCode(const FString& ShaderCodeDir, FString const& Library)
{
	FShaderLibraryInstance* LibraryInstance = FShaderLibraryInstance::Create(ShaderPlatform, ShaderCodeDir, Library);
	if (!LibraryInstance)
	{
		UE_LOG(LogShaderLibrary, Verbose, TEXT("Cooked Context: No Shared Shader Library for: %s and native library not supported."), *Library);
		return false;
	}

	if (LibraryInstance->Library->IsNativeLibrary())
	{
		UE_LOG(LogShaderLibrary, Display, TEXT("Cooked Context: Loaded Native Shared Shader Library %s"), *Library);
	}
	else
	{
		UE_LOG(LogShaderLibrary, Display, TEXT("Cooked Context: Using Shared Shader Library %s"), *Library);
	}

	FRWScopeLock WriteLock(ComponentsMutex, SLT_Write);
	Components.Emplace(LibraryInstance);
	return true;
}

FShaderLibraryInstance* UE::ShaderLibrary::Private::FNamedShaderLibrary::FindShaderLibraryForShaderMap(const FSHAHash& Hash, int32& OutShaderMapIndex)
{
	FRWScopeLock ReadLock(ComponentsMutex, SLT_ReadOnly);

	// Search in library opened order
	for (const TUniquePtr<FShaderLibraryInstance>& Instance : Components)
	{
		const int32 ShaderMapIndex = Instance->Library->FindShaderMapIndex(Hash);
		if (ShaderMapIndex != INDEX_NONE)
		{
			OutShaderMapIndex = ShaderMapIndex;
			return Instance.Get();
		}
	}
	return nullptr;
}

FShaderLibraryInstance* UE::ShaderLibrary::Private::FNamedShaderLibrary::FindShaderLibraryForShader(const FSHAHash& Hash, int32& OutShaderIndex)
{
	FRWScopeLock ReadLock(ComponentsMutex, SLT_ReadOnly);

	// Search in library opened order
	for (const TUniquePtr<FShaderLibraryInstance>& Instance : Components)
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

uint32 UE::ShaderLibrary::Private::FNamedShaderLibrary::GetShaderCount(void)
{
	FRWScopeLock ReadLock(ComponentsMutex, SLT_ReadOnly);

	int ShaderCount = 0;
	for (const TUniquePtr<FShaderLibraryInstance>& Instance : Components)
	{
		ShaderCount += Instance->Library->GetNumShaders();
	}
	return ShaderCount;
}

#if UE_SHADERLIB_WITH_INTROSPECTION
void UE::ShaderLibrary::Private::FNamedShaderLibrary::DumpLibraryContents(const FString& Prefix)
{
	FRWScopeLock ReadLock(ComponentsMutex, SLT_ReadOnly);

	int32 ComponentIdx = 0;
	for (const TUniquePtr<FShaderLibraryInstance>& Instance : Components)
	{
		UE_LOG(LogShaderLibrary, Display, TEXT("%sComponent %d: Native=%s Shaders: %d Name: %s"),
			*Prefix, ComponentIdx, Instance->Library->IsNativeLibrary() ? TEXT("yes") : TEXT("no"), Instance->GetNumShaders(), *Instance->Library->GetName() );
		++ComponentIdx;
	}
}
#endif

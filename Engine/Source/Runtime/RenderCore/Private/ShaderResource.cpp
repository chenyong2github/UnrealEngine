// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	ShaderResource.cpp: ShaderResource implementation.
=============================================================================*/

#include "Shader.h"
#include "Misc/CoreMisc.h"
#include "Misc/StringBuilder.h"
#include "Stats/StatsMisc.h"
#include "Serialization/MemoryWriter.h"
#include "VertexFactory.h"
#include "ProfilingDebugging/DiagnosticTable.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Interfaces/IShaderFormat.h"
#include "ShaderCodeLibrary.h"
#include "ShaderCore.h"
#include "RenderUtils.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/ScopeLock.h"
#include "UObject/RenderingObjectVersion.h"
#include "UObject/FortniteMainBranchObjectVersion.h"
#include "ProfilingDebugging/LoadTimeTracker.h"
#include "Misc/MemStack.h"
#include "ShaderCompilerCore.h"

#if WITH_EDITORONLY_DATA
#include "Interfaces/IShaderFormat.h"
#endif

static const FName ShaderCompressionFormat = NAME_LZ4;

bool FShaderMapResource::ArePlatformsCompatible(EShaderPlatform CurrentPlatform, EShaderPlatform TargetPlatform)
{
	bool bFeatureLevelCompatible = CurrentPlatform == TargetPlatform;

	if (!bFeatureLevelCompatible && IsPCPlatform(CurrentPlatform) && IsPCPlatform(TargetPlatform))
	{
		bFeatureLevelCompatible = GetMaxSupportedFeatureLevel(CurrentPlatform) >= GetMaxSupportedFeatureLevel(TargetPlatform);

		bool const bIsTargetD3D = TargetPlatform == SP_PCD3D_SM5 ||
			TargetPlatform == SP_PCD3D_ES3_1;

		bool const bIsCurrentPlatformD3D = CurrentPlatform == SP_PCD3D_SM5 ||
			TargetPlatform == SP_PCD3D_ES3_1;

		// For Metal in Editor we can switch feature-levels, but not in cooked projects when using Metal shader librariss.
		bool const bIsCurrentMetal = IsMetalPlatform(CurrentPlatform);
		bool const bIsTargetMetal = IsMetalPlatform(TargetPlatform);
		bool const bIsMetalCompatible = (bIsCurrentMetal == bIsTargetMetal)
#if !WITH_EDITOR	// Static analysis doesn't like (|| WITH_EDITOR)
			&& (!IsMetalPlatform(CurrentPlatform) || (CurrentPlatform == TargetPlatform))
#endif
			;

		bool const bIsCurrentOpenGL = IsOpenGLPlatform(CurrentPlatform);
		bool const bIsTargetOpenGL = IsOpenGLPlatform(TargetPlatform);

		bFeatureLevelCompatible = bFeatureLevelCompatible && (bIsCurrentPlatformD3D == bIsTargetD3D && bIsMetalCompatible && bIsCurrentOpenGL == bIsTargetOpenGL);
	}

	return bFeatureLevelCompatible;
}

#if RHI_RAYTRACING
static TArray<uint32> GlobalUnusedIndicies;
static TArray<FRHIRayTracingShader*> GlobalRayTracingMaterialLibrary;
static FCriticalSection GlobalRayTracingMaterialLibraryCS;

void FShaderMapResource::GetRayTracingMaterialLibrary(TArray<FRHIRayTracingShader*>& RayTracingMaterials, FRHIRayTracingShader* DefaultShader)
{
	FScopeLock Lock(&GlobalRayTracingMaterialLibraryCS);
	RayTracingMaterials = GlobalRayTracingMaterialLibrary;

	for (uint32 Index : GlobalUnusedIndicies)
	{
		RayTracingMaterials[Index] = DefaultShader;
	}
}

static uint32 AddToRayTracingLibrary(FRHIRayTracingShader* Shader)
{
	FScopeLock Lock(&GlobalRayTracingMaterialLibraryCS);

	if (GlobalUnusedIndicies.Num() != 0)
	{
		uint32 Index = GlobalUnusedIndicies.Pop(false);
		checkSlow(GlobalRayTracingMaterialLibrary[Index] == nullptr);
		GlobalRayTracingMaterialLibrary[Index] = Shader;
		return Index;
	}
	else
	{
		GlobalRayTracingMaterialLibrary.Add(Shader);
		return GlobalRayTracingMaterialLibrary.Num() - 1;
	}
}

static void RemoveFromRayTracingLibrary(uint32 Index)
{
	if (Index != ~0u)
	{
		FScopeLock Lock(&GlobalRayTracingMaterialLibraryCS);
		GlobalUnusedIndicies.Push(Index);
		GlobalRayTracingMaterialLibrary[Index] = nullptr;
	}
}
#endif // RHI_RAYTRACING

static void ApplyResourceStats(FShaderMapResourceCode& Resource)
{
#if STATS
	INC_DWORD_STAT_BY(STAT_Shaders_ShaderResourceMemory, Resource.GetSizeBytes());
	for (const FShaderMapResourceCode::FShaderEntry& Shader : Resource.ShaderEntries)
	{
		INC_DWORD_STAT_BY_FName(GetMemoryStatType(Shader.Frequency).GetName(), Shader.Code.Num());
	}
#endif // STATS
}

static void RemoveResourceStats(FShaderMapResourceCode& Resource)
{
#if STATS
	DEC_DWORD_STAT_BY(STAT_Shaders_ShaderResourceMemory, Resource.GetSizeBytes());
	for (const FShaderMapResourceCode::FShaderEntry& Shader : Resource.ShaderEntries)
	{
		DEC_DWORD_STAT_BY_FName(GetMemoryStatType(Shader.Frequency).GetName(), Shader.Code.Num());
	}
#endif // STATS
}

FShaderMapResourceCode::~FShaderMapResourceCode()
{
	RemoveResourceStats(*this);
}

void FShaderMapResourceCode::Finalize()
{
	FSHA1 Hasher;
	Hasher.Update((uint8*)ShaderHashes.GetData(), ShaderHashes.Num() * sizeof(FSHAHash));
	Hasher.Final();
	Hasher.GetHash(ResourceHash.Hash);
	ApplyResourceStats(*this);
}

uint32 FShaderMapResourceCode::GetSizeBytes() const
{
	uint32 Size = sizeof(*this) + ShaderHashes.GetAllocatedSize() + ShaderEntries.GetAllocatedSize();
	for (const FShaderEntry& Entry : ShaderEntries)
	{
		Size += Entry.Code.GetAllocatedSize();
	}
	return Size;
}

int32 FShaderMapResourceCode::FindShaderIndex(const FSHAHash& InHash) const
{
	return Algo::BinarySearch(ShaderHashes, InHash);
}

void FShaderMapResourceCode::AddShaderCompilerOutput(const FShaderCompilerOutput& Output)
{
#if WITH_EDITORONLY_DATA
	AddPlatformDebugData(Output.PlatformDebugData);
#endif
	AddShaderCode(Output.Target.GetFrequency(), Output.OutputHash, Output.ShaderCode.GetReadAccess());
}

void FShaderMapResourceCode::AddShaderCode(EShaderFrequency InFrequency, const FSHAHash& InHash, TConstArrayView<uint8> InCode)
{
	const int32 Index = Algo::LowerBound(ShaderHashes, InHash);
	if (Index >= ShaderHashes.Num() || ShaderHashes[Index] != InHash)
	{
		ShaderHashes.Insert(InHash, Index);

		FShaderEntry& Entry = ShaderEntries.InsertDefaulted_GetRef(Index);
		Entry.Frequency = InFrequency;
		Entry.UncompressedSize = InCode.Num();

		bool bAllowShaderCompression = true;
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		static const IConsoleVariable* CVarSkipCompression = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Shaders.SkipCompression"));
		bAllowShaderCompression = CVarSkipCompression ? CVarSkipCompression->GetInt() == 0 : true;
#endif

		int32 CompressedSize = InCode.Num();
		Entry.Code.AddUninitialized(CompressedSize);

		if (bAllowShaderCompression && FCompression::CompressMemory(ShaderCompressionFormat, Entry.Code.GetData(), CompressedSize, InCode.GetData(), InCode.Num()))
		{
			// resize to fit reduced compressed size, but don't reallocate memory
			Entry.Code.SetNum(CompressedSize, false);
		}
		else
		{
			FMemory::Memcpy(Entry.Code.GetData(), InCode.GetData(), InCode.Num());
		}
	}
}

#if WITH_EDITORONLY_DATA
void FShaderMapResourceCode::AddPlatformDebugData(TConstArrayView<uint8> InPlatformDebugData)
{
	if (InPlatformDebugData.Num() == 0)
	{
		return;
	}

	FSHAHash Hash;
	{
		FSHA1 Hasher;
		Hasher.Update(InPlatformDebugData.GetData(), InPlatformDebugData.Num());
		Hasher.Final();
		Hasher.GetHash(Hash.Hash);
	}

	const int32 Index = Algo::LowerBound(PlatformDebugDataHashes, Hash);
	if (Index >= PlatformDebugDataHashes.Num() || PlatformDebugDataHashes[Index] != Hash)
	{
		PlatformDebugDataHashes.Insert(Hash, Index);
		PlatformDebugData.EmplaceAt(Index, InPlatformDebugData.GetData(), InPlatformDebugData.Num());
	}
}
#endif // WITH_EDITORONLY_DATA

void FShaderMapResourceCode::ToString(FStringBuilderBase& OutString) const
{
	OutString.Appendf(TEXT("Shaders: Num=%d\n"), ShaderHashes.Num());
	for (int32 i = 0; i < ShaderHashes.Num(); ++i)
	{
		const FShaderEntry& Entry = ShaderEntries[i];
		OutString.Appendf(TEXT("    [%d]: { Hash: %s, Freq: %s, Size: %d, UncompressedSize: %d }\n"),
			i, *ShaderHashes[i].ToString(), GetShaderFrequencyString(Entry.Frequency), Entry.Code.Num(), Entry.UncompressedSize);
	}
}

void FShaderMapResourceCode::Serialize(FArchive& Ar, bool bLoadedByCookedMaterial)
{
	Ar << ResourceHash;
	Ar << ShaderHashes;
	Ar << ShaderEntries;
	check(ShaderEntries.Num() == ShaderHashes.Num());
#if WITH_EDITORONLY_DATA
	const bool bSerializePlatformData = !bLoadedByCookedMaterial && (!Ar.IsCooking() || Ar.CookingTarget()->HasEditorOnlyData());
	if (bSerializePlatformData)
	{
		Ar << PlatformDebugDataHashes;
		Ar << PlatformDebugData;
	}
#endif // WITH_EDITORONLY_DATA
	ApplyResourceStats(*this);
}

#if WITH_EDITORONLY_DATA
void FShaderMapResourceCode::NotifyShadersCooked(const ITargetPlatform* TargetPlatform)
{
#if WITH_ENGINE
	// Notify the platform shader format that this particular shader is being used in the cook.
	// We discard this data in cooked builds unless Ar.CookingTarget()->HasEditorOnlyData() is true.
	check(TargetPlatform);
	if (PlatformDebugData.Num())
	{
		TArray<FName> ShaderFormatNames;
		TargetPlatform->GetAllTargetedShaderFormats(ShaderFormatNames);
		for (FName FormatName : ShaderFormatNames)
		{
			const IShaderFormat* ShaderFormat = GetTargetPlatformManagerRef().FindShaderFormat(FormatName);
			if (ShaderFormat)
			{
				for (const auto& Entry : PlatformDebugData)
				{
					ShaderFormat->NotifyShaderCooked(Entry, FormatName);
				}
			}
		}
	}
#endif // WITH_ENGINE
}
#endif // WITH_EDITORONLY_DATA

FShaderMapResource::FShaderMapResource(EShaderPlatform InPlatform, int32 NumShaders)
	: NumRHIShaders(NumShaders)
	, Platform(InPlatform)
	, NumRefs(0)
{
	RHIShaders = MakeUnique<std::atomic<FRHIShader*>[]>(NumRHIShaders); // this MakeUnique() zero-initializes the array
#if RHI_RAYTRACING
	if (GRHISupportsRayTracing)
	{
		RayTracingMaterialLibraryIndices.AddUninitialized(NumShaders);
		FMemory::Memset(RayTracingMaterialLibraryIndices.GetData(), 0xff, NumShaders * RayTracingMaterialLibraryIndices.GetTypeSize());
	}
#endif // RHI_RAYTRACING
}

FShaderMapResource::~FShaderMapResource()
{
	ReleaseShaders();
	check(NumRefs == 0);
}

void FShaderMapResource::AddRef()
{
	FPlatformAtomics::InterlockedIncrement((volatile int32*)&NumRefs);
}

void FShaderMapResource::Release()
{
	check(NumRefs > 0);
	if (FPlatformAtomics::InterlockedDecrement((volatile int32*)&NumRefs) == 0 && TryRelease())
	{
		// Send a release message to the rendering thread when the shader loses its last reference.
		BeginReleaseResource(this);
		BeginCleanup(this);

		DEC_DWORD_STAT_BY(STAT_Shaders_ShaderResourceMemory, GetSizeBytes());
	}
}

void FShaderMapResource::ReleaseShaders()
{
	if (RHIShaders)
	{
		for (int32 Idx = 0; Idx < NumRHIShaders; ++Idx)
		{
			if (FRHIShader* Shader = RHIShaders[Idx].load(std::memory_order_acquire))
			{
				Shader->Release();
			}
		}
		RHIShaders = nullptr;
		NumRHIShaders = 0;
	}
}


void FShaderMapResource::ReleaseRHI()
{
#if RHI_RAYTRACING
	for (int32 Index : RayTracingMaterialLibraryIndices)
	{
		RemoveFromRayTracingLibrary(Index);
	}
	RayTracingMaterialLibraryIndices.Empty();
#endif // RHI_RAYTRACING

	ReleaseShaders();
}

void FShaderMapResource::BeginCreateAllShaders()
{
	FShaderMapResource* Resource = this;
	ENQUEUE_RENDER_COMMAND(InitCommand)(
		[Resource](FRHICommandListImmediate& RHICmdList)
	{
		for (int32 ShaderIndex = 0; ShaderIndex < Resource->GetNumShaders(); ++ShaderIndex)
		{
			Resource->GetShader(ShaderIndex);
		}
	});
}

FRHIShader* FShaderMapResource::CreateShader(int32 ShaderIndex)
{
	check(IsInParallelRenderingThread());
	check(!RHIShaders[ShaderIndex].load(std::memory_order_acquire));

	TRefCountPtr<FRHIShader> RHIShader = CreateRHIShader(ShaderIndex);
#if RHI_RAYTRACING
	if (GRHISupportsRayTracing && RHIShader.IsValid() && RHIShader->GetFrequency() == SF_RayHitGroup)
	{
		RayTracingMaterialLibraryIndices[ShaderIndex] = AddToRayTracingLibrary(static_cast<FRHIRayTracingShader*>(RHIShader.GetReference()));
	}
#endif // RHI_RAYTRACING

	// keep the reference alive (the caller will release)
	if (RHIShader.IsValid())
	{
		RHIShader->AddRef();
	}
	return RHIShader.GetReference();
}

TRefCountPtr<FRHIShader> FShaderMapResource_InlineCode::CreateRHIShader(int32 ShaderIndex)
{
	// we can't have this called on the wrong platform's shaders
	if (!ArePlatformsCompatible(GMaxRHIShaderPlatform, GetPlatform()))
	{
		if (FPlatformProperties::RequiresCookedData())
		{
			UE_LOG(LogShaders, Fatal, TEXT("FShaderMapResource_InlineCode::InitRHI got platform %s but it is not compatible with %s"),
				*LegacyShaderPlatformToShaderFormat(GetPlatform()).ToString(), *LegacyShaderPlatformToShaderFormat(GMaxRHIShaderPlatform).ToString());
		}
		return TRefCountPtr<FRHIShader>();
	}

	FMemStackBase& MemStack = FMemStack::Get();
	const FShaderMapResourceCode::FShaderEntry& ShaderEntry = Code->ShaderEntries[ShaderIndex];
	const uint8* ShaderCode = ShaderEntry.Code.GetData();

	FMemMark Mark(MemStack);
	if (ShaderEntry.Code.Num() != ShaderEntry.UncompressedSize)
	{
		void* UncompressedCode = MemStack.Alloc(ShaderEntry.UncompressedSize, 16);
		auto bSucceed = FCompression::UncompressMemory(ShaderCompressionFormat, UncompressedCode, ShaderEntry.UncompressedSize, ShaderCode, ShaderEntry.Code.Num());
		check(bSucceed);
		ShaderCode = (uint8*)UncompressedCode;
	}

	const auto ShaderCodeView = MakeArrayView(ShaderCode, ShaderEntry.UncompressedSize);
	const FSHAHash& ShaderHash = Code->ShaderHashes[ShaderIndex];
	const EShaderFrequency Frequency = ShaderEntry.Frequency;

	TRefCountPtr<FRHIShader> RHIShader;
	switch (Frequency)
	{
	case SF_Vertex: RHIShader = RHICreateVertexShader(ShaderCodeView, ShaderHash); break;
	case SF_Pixel: RHIShader = RHICreatePixelShader(ShaderCodeView, ShaderHash); break;
	case SF_Hull: RHIShader = RHICreateHullShader(ShaderCodeView, ShaderHash); break;
	case SF_Domain: RHIShader = RHICreateDomainShader(ShaderCodeView, ShaderHash); break;
	case SF_Geometry: RHIShader = RHICreateGeometryShader(ShaderCodeView, ShaderHash); break;
	case SF_Compute: RHIShader = RHICreateComputeShader(ShaderCodeView, ShaderHash); break;
	case SF_RayGen: case SF_RayMiss: case SF_RayHitGroup: case SF_RayCallable:
#if RHI_RAYTRACING
		if (GRHISupportsRayTracing)
		{
			RHIShader = RHICreateRayTracingShader(ShaderCodeView, ShaderHash, Frequency);
		}
#endif // RHI_RAYTRACING
		break;
	default:
		checkNoEntry();
		break;
	}

	if (RHIShader)
	{
		RHIShader->SetHash(ShaderHash);
	}
	return RHIShader;
}

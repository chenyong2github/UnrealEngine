// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	ShaderResource.cpp: ShaderResource implementation.
=============================================================================*/

#include "Shader.h"
#include "Misc/CoreMisc.h"
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
	for (FShaderMapResourceCode::FShaderEntry& Shader : Resource.ShaderEntries)
	{
		INC_DWORD_STAT_BY_FName(GetMemoryStatType(Shader.Frequency).GetName(), Shader.CompressedSize);
	}
#endif // STATS
}

static void RemoveResourceStats(FShaderMapResourceCode& Resource)
{
#if STATS
	DEC_DWORD_STAT_BY(STAT_Shaders_ShaderResourceMemory, Resource.GetSizeBytes());
	for (FShaderMapResourceCode::FShaderEntry& Shader : Resource.ShaderEntries)
	{
		DEC_DWORD_STAT_BY_FName(GetMemoryStatType(Shader.Frequency).GetName(), Shader.CompressedSize);
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

void FShaderMapResourceCode::Serialize(FArchive& Ar)
{
	Ar << ResourceHash;
	Ar << ShaderHashes;
	Ar << ShaderEntries;
	Ar << ShaderCode;
	check(ShaderEntries.Num() == ShaderHashes.Num());
	ApplyResourceStats(*this);
}

FShaderMapResourceBuilder::FShaderMapResourceBuilder(FShaderMapResourceCode* InCode) : ShaderHashTable(1024, 256), Code(InCode)
{
	check(InCode->ShaderEntries.Num() == InCode->ShaderHashes.Num());
	for(int32 ShaderIndex = 0; ShaderIndex < InCode->ShaderHashes.Num(); ++ShaderIndex)
	{
		const uint32 Key = GetTypeHash(InCode->ShaderHashes[ShaderIndex]);
		ShaderHashTable.Add(Key, ShaderIndex);
	}
}

int32 FShaderMapResourceBuilder::FindCode(const FSHAHash& InHash, uint32 InKey) const
{
	for (int32 Index = ShaderHashTable.First(InKey); ShaderHashTable.IsValid(Index); Index = ShaderHashTable.Next(Index))
	{
		if (Code->ShaderHashes[Index] == InHash)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

int32 FShaderMapResourceBuilder::FindOrAddCode(EShaderFrequency InFrequency, const FSHAHash& InHash, const TConstArrayView<uint8>& InCode)
{
	const uint32 Key = GetTypeHash(InHash);
	int32 Index = FindCode(InHash, Key);
	if(Index == INDEX_NONE)
	{
		bool bAllowShaderCompression = true;

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		static const IConsoleVariable* CVarSkipCompression = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Shaders.SkipCompression"));
		bAllowShaderCompression = CVarSkipCompression ? CVarSkipCompression->GetInt() == 0 : true;
#endif

		int32 CompressedSize = InCode.Num();
		TArray<uint8> CompressedCode;
		CompressedCode.AddUninitialized(CompressedSize);

		if (bAllowShaderCompression && FCompression::CompressMemory(ShaderCompressionFormat, CompressedCode.GetData(), CompressedSize, InCode.GetData(), InCode.Num()))
		{
			CompressedCode.SetNum(CompressedSize, false);
		}
		else
		{
			CompressedCode = InCode;
		}

		Index = Code->ShaderEntries.Num();
		ShaderHashTable.Add(Key, Index);

		Code->ShaderHashes.Add(InHash);
		FShaderMapResourceCode::FShaderEntry& Entry = Code->ShaderEntries.AddDefaulted_GetRef();
		Entry.CompressedSize = CompressedSize;
		Entry.UncompressedSize = InCode.Num();
		Entry.Frequency = InFrequency;
		Entry.Offset = Code->ShaderCode.Num();

		Code->ShaderCode.Append(CompressedCode);
	}

	return Index;
}

FShaderMapResource::FShaderMapResource(EShaderPlatform InPlatform, int32 NumShaders)
	: Platform(InPlatform)
	, NumRefs(0)
{
	RHIShaders.AddZeroed(NumShaders);
#if RHI_RAYTRACING
	RayTracingMaterialLibraryIndices.AddUninitialized(NumShaders);
	FMemory::Memset(RayTracingMaterialLibraryIndices.GetData(), 0xff, NumShaders * RayTracingMaterialLibraryIndices.GetTypeSize());
#endif // RHI_RAYTRACING
}

FShaderMapResource::~FShaderMapResource()
{
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

void FShaderMapResource::ReleaseRHI()
{
#if RHI_RAYTRACING
	for (int32 Index : RayTracingMaterialLibraryIndices)
	{
		RemoveFromRayTracingLibrary(Index);
	}
	RayTracingMaterialLibraryIndices.Empty();
#endif // RHI_RAYTRACING

	RHIShaders.Empty();
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

void FShaderMapResource::CreateShader(int32 ShaderIndex)
{
	check(IsInParallelRenderingThread());
	check(!RHIShaders[ShaderIndex]);

	TRefCountPtr<FRHIShader> RHIShader = CreateRHIShader(ShaderIndex);
#if RHI_RAYTRACING
	if (GRHISupportsRayTracing && RHIShader.IsValid() && RHIShader->GetFrequency() == SF_RayHitGroup)
	{
		RayTracingMaterialLibraryIndices[ShaderIndex] = AddToRayTracingLibrary(static_cast<FRHIRayTracingShader*>(RHIShader.GetReference()));
	}
#endif // RHI_RAYTRACING
	RHIShaders[ShaderIndex] = MoveTemp(RHIShader);
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
	const uint8* ShaderCode = Code->ShaderCode.GetData() + ShaderEntry.Offset;

	FMemMark Mark(MemStack);
	if (ShaderEntry.CompressedSize != ShaderEntry.UncompressedSize)
	{
		void* UncompressedCode = MemStack.Alloc(ShaderEntry.UncompressedSize, 16);
		auto bSucceed = FCompression::UncompressMemory(ShaderCompressionFormat, UncompressedCode, ShaderEntry.UncompressedSize, ShaderCode, ShaderEntry.CompressedSize);
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
#if RHI_RAYTRACING
	case SF_RayGen: case SF_RayMiss: case SF_RayHitGroup: case SF_RayCallable:
		if (GRHISupportsRayTracing)
		{
			RHIShader = RHICreateRayTracingShader(ShaderCodeView, ShaderHash, Frequency);
		}
		break;
#endif // RHI_RAYTRACING
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

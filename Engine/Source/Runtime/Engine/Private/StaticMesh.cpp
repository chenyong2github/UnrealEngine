// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	StaticMesh.cpp: Static mesh class implementation.
=============================================================================*/

#include "Engine/StaticMesh.h"
#include "Serialization/MemoryWriter.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/ScopedSlowTask.h"
#include "UObject/FrameworkObjectVersion.h"
#include "Misc/App.h"
#include "Modules/ModuleManager.h"
#include "UObject/UObjectAnnotation.h"
#include "RenderingThread.h"
#include "VertexFactory.h"
#include "LocalVertexFactory.h"
#include "RawIndexBuffer.h"
#include "Engine/TextureStreamingTypes.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Serialization/MemoryReader.h"
#include "UObject/EditorObjectVersion.h"
#include "UObject/RenderingObjectVersion.h"
#include "UObject/Package.h"
#include "EngineUtils.h"
#include "Engine/AssetUserData.h"
#include "StaticMeshResources.h"
#include "StaticMeshVertexData.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshDescription.h"
#include "Interfaces/ITargetPlatform.h"
#include "SpeedTreeWind.h"
#include "DistanceFieldAtlas.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "PhysicsEngine/BodySetup.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Engine/Engine.h"
#include "EngineGlobals.h"
#include "HAL/LowLevelMemTracker.h"
#include "DynamicMeshBuilder.h"
#include "Model.h"
#include "SplineMeshSceneProxy.h"
#include "Templates/UniquePtr.h"

#if WITH_EDITOR
#include "Async/ParallelFor.h"
#include "RawMesh.h"
#include "Settings/EditorExperimentalSettings.h"
#include "MeshBuilder.h"
#include "MeshUtilities.h"
#include "MeshUtilitiesCommon.h"
#include "DerivedDataCacheInterface.h"
#include "PlatformInfo.h"
#include "ScopedTransaction.h"
#include "IMeshBuilderModule.h"
#include "MeshDescriptionOperations.h"
#include "IMeshReductionManagerModule.h"
#include "IMeshReductionInterfaces.h"
#include "TessellationRendering.h"
#include "Misc/MessageDialog.h"

#endif // #if WITH_EDITOR

#include "Engine/StaticMeshSocket.h"
#include "EditorFramework/AssetImportData.h"
#include "AI/Navigation/NavCollisionBase.h"
#include "AI/NavigationSystemBase.h"
#include "AI/NavigationSystemHelpers.h"
#include "ProfilingDebugging/CookStats.h"
#include "UObject/ReleaseObjectVersion.h"
#include "Streaming/UVChannelDensity.h"
#include "Logging/MessageLog.h"
#include "Misc/UObjectToken.h"
#include "UObject/CoreRedirects.h"
#include "HAL/FileManager.h"
#include "ContentStreaming.h"
#include "Streaming/StaticMeshUpdate.h"

#define LOCTEXT_NAMESPACE "StaticMesh"
DEFINE_LOG_CATEGORY(LogStaticMesh);	

DECLARE_MEMORY_STAT( TEXT( "StaticMesh Total Memory" ), STAT_StaticMeshTotalMemory2, STATGROUP_MemoryStaticMesh );
DECLARE_MEMORY_STAT( TEXT( "StaticMesh Vertex Memory" ), STAT_StaticMeshVertexMemory, STATGROUP_MemoryStaticMesh );
DECLARE_MEMORY_STAT( TEXT( "StaticMesh VxColor Resource Mem" ), STAT_ResourceVertexColorMemory, STATGROUP_MemoryStaticMesh );
DECLARE_MEMORY_STAT( TEXT( "StaticMesh Index Memory" ), STAT_StaticMeshIndexMemory, STATGROUP_MemoryStaticMesh );
DECLARE_MEMORY_STAT( TEXT( "StaticMesh Distance Field Memory" ), STAT_StaticMeshDistanceFieldMemory, STATGROUP_MemoryStaticMesh );
DECLARE_MEMORY_STAT( TEXT( "StaticMesh Occluder Memory" ), STAT_StaticMeshOccluderMemory, STATGROUP_MemoryStaticMesh );

DECLARE_MEMORY_STAT( TEXT( "StaticMesh Total Memory" ), STAT_StaticMeshTotalMemory, STATGROUP_Memory );

/** Package name, that if set will cause only static meshes in that package to be rebuilt based on SM version. */
ENGINE_API FName GStaticMeshPackageNameToRebuild = NAME_None;

#if WITH_EDITORONLY_DATA
int32 GUpdateMeshLODGroupSettingsAtLoad = 0;
static FAutoConsoleVariableRef CVarStaticMeshUpdateMeshLODGroupSettingsAtLoad(
	TEXT("r.StaticMesh.UpdateMeshLODGroupSettingsAtLoad"),
	GUpdateMeshLODGroupSettingsAtLoad,
	TEXT("If set, LODGroup settings for static meshes will be applied at load time."));
#endif

static TAutoConsoleVariable<int32> CVarStripMinLodDataDuringCooking(
	TEXT("r.StaticMesh.StripMinLodDataDuringCooking"),
	0,
	TEXT("If non-zero, data for Static Mesh LOD levels below MinLOD will be discarded at cook time"));

int32 GForceStripMeshAdjacencyDataDuringCooking = 0;
static FAutoConsoleVariableRef CVarForceStripMeshAdjacencyDataDuringCooking(
	TEXT("r.ForceStripAdjacencyDataDuringCooking"),
	GForceStripMeshAdjacencyDataDuringCooking,
	TEXT("If set, adjacency data will be stripped for all static and skeletal meshes during cooking (acting like the target platform did not support tessellation)."));

static TAutoConsoleVariable<int32> CVarSupportDepthOnlyIndexBuffers(
	TEXT("r.SupportDepthOnlyIndexBuffers"),
	1,
	TEXT("Enables depth-only index buffers. Saves a little time at the expense of doubling the size of index buffers."),
	ECVF_ReadOnly | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarSupportReversedIndexBuffers(
	TEXT("r.SupportReversedIndexBuffers"),
	1,
	TEXT("Enables reversed index buffers. Saves a little time at the expense of doubling the size of index buffers."),
	ECVF_ReadOnly | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarStripDistanceFieldDataDuringLoad(
	TEXT("r.StaticMesh.StripDistanceFieldDataDuringLoad"),
	0,
	TEXT("If non-zero, data for distance fields will be discarded on load. TODO: change to discard during cook!."),
	ECVF_ReadOnly | ECVF_RenderThreadSafe);

extern bool TrackRenderAssetEvent(struct FStreamingRenderAsset* StreamingRenderAsset, UStreamableRenderAsset* RenderAsset, bool bForceMipLevelsToBeResident, const FRenderAssetStreamingManager* Manager);

#if ENABLE_COOK_STATS
namespace StaticMeshCookStats
{
	static FCookStats::FDDCResourceUsageStats UsageStats;
	static FCookStatsManager::FAutoRegisterCallback RegisterCookStats([](FCookStatsManager::AddStatFuncRef AddStat)
	{
		UsageStats.LogStats(AddStat, TEXT("StaticMesh.Usage"), TEXT(""));
	});
}
#endif


#if WITH_EDITOR
static void FillMaterialName(const TArray<FStaticMaterial>& StaticMaterials, TMap<int32, FName>& OutMaterialMap)
{
	OutMaterialMap.Empty(StaticMaterials.Num());

	for (int32 MaterialIndex = 0; MaterialIndex < StaticMaterials.Num(); ++MaterialIndex)
	{
		FName MaterialName = StaticMaterials[MaterialIndex].ImportedMaterialSlotName;
		if (MaterialName == NAME_None)
		{
			MaterialName = *(TEXT("MaterialSlot_") + FString::FromInt(MaterialIndex));
		}
		OutMaterialMap.Add(MaterialIndex, MaterialName);
	}
}
#endif


/*-----------------------------------------------------------------------------
	FStaticMeshSectionAreaWeightedTriangleSamplerBuffer
-----------------------------------------------------------------------------*/

FStaticMeshSectionAreaWeightedTriangleSamplerBuffer::FStaticMeshSectionAreaWeightedTriangleSamplerBuffer()
{
}

FStaticMeshSectionAreaWeightedTriangleSamplerBuffer::~FStaticMeshSectionAreaWeightedTriangleSamplerBuffer()
{
}

void FStaticMeshSectionAreaWeightedTriangleSamplerBuffer::InitRHI()
{
	ReleaseRHI();

	if (Samplers && Samplers->Num() > 0)
	{
		FRHIResourceCreateInfo CreateInfo;
		void* BufferData = nullptr;

		// Count triangle count for all sections and required memory
		const uint32 AllSectionCount = Samplers->Num();
		uint32 TriangleCount = 0;
		for (uint32 i = 0; i < AllSectionCount; ++i)
		{
			TriangleCount += (*Samplers)[i].GetNumEntries();
		}
		uint32 SizeByte = TriangleCount * sizeof(SectionTriangleInfo);

		BufferSectionTriangleRHI = RHICreateAndLockVertexBuffer(SizeByte, BUF_Static | BUF_ShaderResource, CreateInfo, BufferData);

		// Now compute the alias look up table for unifor; distribution for all section and all triangles
		SectionTriangleInfo* SectionTriangleInfoBuffer = (SectionTriangleInfo*)BufferData;
		for (uint32 i = 0; i < AllSectionCount; ++i)
		{
			FStaticMeshSectionAreaWeightedTriangleSampler& sampler = (*Samplers)[i];
			const TArray<float>& ProbTris = sampler.GetProb();
			const TArray<int32>& AliasTris = sampler.GetAlias();
			const uint32 NumTriangle = sampler.GetNumEntries();

			for (uint32 t = 0; t < NumTriangle; ++t)
			{
				SectionTriangleInfo NewTriangleInfo = { ProbTris[t], (uint32)AliasTris[t], 0, 0 };
				*SectionTriangleInfoBuffer = NewTriangleInfo;
				SectionTriangleInfoBuffer++;
			}
		}
		RHIUnlockVertexBuffer(BufferSectionTriangleRHI);

		BufferSectionTriangleSRV = RHICreateShaderResourceView(BufferSectionTriangleRHI, sizeof(SectionTriangleInfo), PF_R32G32B32A32_UINT);
	}
}

void FStaticMeshSectionAreaWeightedTriangleSamplerBuffer::ReleaseRHI()
{
	BufferSectionTriangleSRV.SafeRelease();
	BufferSectionTriangleRHI.SafeRelease();
}


/*-----------------------------------------------------------------------------
	FStaticMeshLODResources
-----------------------------------------------------------------------------*/

FArchive& operator<<(FArchive& Ar, FStaticMeshSection& Section)
{
	Ar << Section.MaterialIndex;
	Ar << Section.FirstIndex;
	Ar << Section.NumTriangles;
	Ar << Section.MinVertexIndex;
	Ar << Section.MaxVertexIndex;
	Ar << Section.bEnableCollision;
	Ar << Section.bCastShadow;

#if WITH_EDITORONLY_DATA
	if((!Ar.IsCooking() && !Ar.IsFilterEditorOnly()) || (Ar.IsCooking() && Ar.CookingTarget()->HasEditorOnlyData()))
	{
		for (int32 UVIndex = 0; UVIndex < MAX_STATIC_TEXCOORDS; ++UVIndex)
		{
			Ar << Section.UVDensities[UVIndex];
			Ar << Section.Weights[UVIndex];
		}
	}
#endif

	return Ar;
}

int32 FStaticMeshLODResources::GetPlatformMinLODIdx(const ITargetPlatform* TargetPlatform, UStaticMesh* StaticMesh)
{
#if WITH_EDITOR
	check(TargetPlatform && StaticMesh);
	return StaticMesh->MinLOD.GetValueForPlatformIdentifiers(
		TargetPlatform->GetPlatformInfo().PlatformGroupName,
		TargetPlatform->GetPlatformInfo().VanillaPlatformName);
#else
	return 0;
#endif
}

uint8 FStaticMeshLODResources::GenerateClassStripFlags(FArchive& Ar, UStaticMesh* OwnerStaticMesh, int32 Index)
{
#if WITH_EDITOR
	// Defined class flags for possible stripping
	const uint8 AdjacencyDataStripFlag = CDSF_AdjacencyData;
	const uint8 MinLodDataStripFlag = CDSF_MinLodData;
	const uint8 ReversedIndexBufferStripFlag = CDSF_ReversedIndexBuffer;

	const bool bWantToStripTessellation = Ar.IsCooking()
		&& ((GForceStripMeshAdjacencyDataDuringCooking != 0) || !Ar.CookingTarget()->SupportsFeature(ETargetPlatformFeatures::Tessellation));
	const bool bWantToStripLOD = Ar.IsCooking()
		&& (CVarStripMinLodDataDuringCooking.GetValueOnAnyThread() != 0)
		&& OwnerStaticMesh
		&& GetPlatformMinLODIdx(Ar.CookingTarget(), OwnerStaticMesh) > Index;

	return (bWantToStripTessellation ? AdjacencyDataStripFlag : 0) |
		(bWantToStripLOD ? MinLodDataStripFlag : 0);
#else
	return 0;
#endif
}

bool FStaticMeshLODResources::IsLODCookedOut(const ITargetPlatform* TargetPlatform, UStaticMesh* StaticMesh, bool bIsBelowMinLOD)
{
	check(StaticMesh);
#if WITH_EDITOR
	if (!bIsBelowMinLOD)
	{
		return false;
	}

	if (!TargetPlatform)
	{
		TargetPlatform = GetTargetPlatformManagerRef().GetRunningTargetPlatform();
	}
	check(TargetPlatform);

	// If LOD streaming is supported, LODs below MinLOD are stored to optional paks and thus never cooked out
	const FStaticMeshLODGroup& LODGroupSettings = TargetPlatform->GetStaticMeshLODSettings().GetLODGroup(StaticMesh->LODGroup);
	return !TargetPlatform->SupportsFeature(ETargetPlatformFeatures::MeshLODStreaming) || !LODGroupSettings.IsLODStreamingSupported();
#else
	return false;
#endif
}

bool FStaticMeshLODResources::IsLODInlined(const ITargetPlatform* TargetPlatform, UStaticMesh* StaticMesh, int32 LODIdx, bool bIsBelowMinLOD)
{
	check(StaticMesh);
#if WITH_EDITOR
	if (!TargetPlatform)
	{
		TargetPlatform = GetTargetPlatformManagerRef().GetRunningTargetPlatform();
	}
	check(TargetPlatform);

	const FStaticMeshLODGroup& LODGroupSettings = TargetPlatform->GetStaticMeshLODSettings().GetLODGroup(StaticMesh->LODGroup);
	if (!TargetPlatform->SupportsFeature(ETargetPlatformFeatures::MeshLODStreaming) || !LODGroupSettings.IsLODStreamingSupported())
	{
		return true;
	}

	if (bIsBelowMinLOD)
	{
		return false;
	}

	int32 MaxNumStreamedLODs = 0;
	const int32 NumStreamedLODsOverride = StaticMesh->NumStreamedLODs.GetValueForPlatformIdentifiers(
		TargetPlatform->GetPlatformInfo().PlatformGroupName,
		TargetPlatform->GetPlatformInfo().VanillaPlatformName);
	if (NumStreamedLODsOverride >= 0)
	{
		MaxNumStreamedLODs = NumStreamedLODsOverride;
	}
	else
	{
		MaxNumStreamedLODs = LODGroupSettings.GetDefaultMaxNumStreamedLODs();
	}
	
	const int32 NumLODs = StaticMesh->GetNumLODs();
	const int32 NumStreamedLODs = FMath::Min(MaxNumStreamedLODs, NumLODs - 1);
	const int32 InlinedLODStartIdx = NumStreamedLODs;
	return LODIdx >= InlinedLODStartIdx;
#else
	return false;
#endif
}

int32 FStaticMeshLODResources::GetNumOptionalLODsAllowed(const ITargetPlatform* TargetPlatform, UStaticMesh* StaticMesh)
{
#if WITH_EDITOR
	check(TargetPlatform && StaticMesh);
	const FStaticMeshLODGroup& LODGroupSettings = TargetPlatform->GetStaticMeshLODSettings().GetLODGroup(StaticMesh->LODGroup);
	return LODGroupSettings.GetDefaultMaxNumOptionalLODs();
#else
	return 0;
#endif
}

void FStaticMeshLODResources::AccumVertexBuffersSize(const FStaticMeshVertexBuffers& VertexBuffers, uint32& OutSize)
{
#if (WITH_EDITOR || DO_CHECK)
	const FPositionVertexBuffer& Pos = VertexBuffers.PositionVertexBuffer;
	const FStaticMeshVertexBuffer& TanTex = VertexBuffers.StaticMeshVertexBuffer;
	const FColorVertexBuffer& Color = VertexBuffers.ColorVertexBuffer;
	OutSize += Pos.GetNumVertices() * Pos.GetStride();
	OutSize += TanTex.GetResourceSize();
	OutSize += Color.GetNumVertices() * Color.GetStride();
#endif
}

void FStaticMeshLODResources::AccumIndexBufferSize(const FRawStaticIndexBuffer& IndexBuffer, uint32& OutSize)
{
#if (WITH_EDITOR || DO_CHECK)
	OutSize += IndexBuffer.GetIndexDataSize();
#endif
}

uint32 FStaticMeshLODResources::FStaticMeshBuffersSize::CalcBuffersSize() const
{
	// Assumes these two cvars don't change at runtime
	const bool bEnableDepthOnlyIndexBuffer = !!CVarSupportDepthOnlyIndexBuffers.GetValueOnAnyThread();
	const bool bEnableReversedIndexBuffer = !!CVarSupportReversedIndexBuffers.GetValueOnAnyThread();
	return SerializedBuffersSize
		- (bEnableDepthOnlyIndexBuffer ? 0 : DepthOnlyIBSize)
		- (bEnableReversedIndexBuffer ? 0 : ReversedIBsSize);
}

void FStaticMeshLODResources::SerializeBuffers(FArchive& Ar, UStaticMesh* OwnerStaticMesh, uint8 InStripFlags, FStaticMeshBuffersSize& OutBuffersSize)
{
	bool bEnableDepthOnlyIndexBuffer = (CVarSupportDepthOnlyIndexBuffers.GetValueOnAnyThread() == 1);
	bool bEnableReversedIndexBuffer = (CVarSupportReversedIndexBuffers.GetValueOnAnyThread() == 1);

	// See if the mesh wants to keep resources CPU accessible
	bool bMeshCPUAcces = OwnerStaticMesh ? OwnerStaticMesh->bAllowCPUAccess : false;

	// Note: this is all derived data, native versioning is not needed, but be sure to bump STATICMESH_DERIVEDDATA_VER when modifying!

	// On cooked platforms we never need the resource data.
	// TODO: Not needed in uncooked games either after PostLoad!
	bool bNeedsCPUAccess = !FPlatformProperties::RequiresCookedData() || bMeshCPUAcces;

	if (FPlatformProperties::RequiresCookedData())
	{
		if (bNeedsCPUAccess && OwnerStaticMesh)
		{
			UE_LOG(LogStaticMesh, Log, TEXT("[%s] Mesh is marked for CPU read."), *OwnerStaticMesh->GetName());
		}
	}

	bHasWireframeIndices = false;
	bHasAdjacencyInfo = false;
	bHasDepthOnlyIndices = false;
	bHasReversedIndices = false;
	bHasReversedDepthOnlyIndices = false;
	bHasColorVertexData = false;
	DepthOnlyNumTriangles = 0;

	FStripDataFlags StripFlags(Ar, InStripFlags);

	VertexBuffers.PositionVertexBuffer.Serialize(Ar, bNeedsCPUAccess);
	VertexBuffers.StaticMeshVertexBuffer.Serialize(Ar, bNeedsCPUAccess);
	VertexBuffers.ColorVertexBuffer.Serialize(Ar, bNeedsCPUAccess);
	OutBuffersSize.Clear();
	AccumVertexBuffersSize(VertexBuffers, OutBuffersSize.SerializedBuffersSize);

	IndexBuffer.Serialize(Ar, bNeedsCPUAccess);
	AccumIndexBufferSize(IndexBuffer, OutBuffersSize.SerializedBuffersSize);

	const bool bSerializeReversedIndexBuffer = !StripFlags.IsClassDataStripped(CDSF_ReversedIndexBuffer);
	const bool bSerializeAdjacencyDataIndexBuffer = !StripFlags.IsClassDataStripped(CDSF_AdjacencyData);
	const bool bSerializeWireframeIndexBuffer = !StripFlags.IsEditorDataStripped();

	FAdditionalStaticMeshIndexBuffers DummyBuffers;
	FAdditionalStaticMeshIndexBuffers* SerializedAdditionalIndexBuffers = &DummyBuffers;
	if ((bEnableDepthOnlyIndexBuffer || bEnableReversedIndexBuffer) && (bSerializeReversedIndexBuffer || bSerializeAdjacencyDataIndexBuffer || bSerializeWireframeIndexBuffer || bEnableDepthOnlyIndexBuffer))
	{
		if (AdditionalIndexBuffers == nullptr)
		{
			AdditionalIndexBuffers = new FAdditionalStaticMeshIndexBuffers();
		}
		SerializedAdditionalIndexBuffers = AdditionalIndexBuffers;
	}

	if (bSerializeReversedIndexBuffer)
	{
		SerializedAdditionalIndexBuffers->ReversedIndexBuffer.Serialize(Ar, bNeedsCPUAccess);
		AccumIndexBufferSize(SerializedAdditionalIndexBuffers->ReversedIndexBuffer, OutBuffersSize.ReversedIBsSize);
		AccumIndexBufferSize(SerializedAdditionalIndexBuffers->ReversedIndexBuffer, OutBuffersSize.SerializedBuffersSize);
		if (!bEnableReversedIndexBuffer)
		{
			SerializedAdditionalIndexBuffers->ReversedIndexBuffer.Discard();
		}
	}

	DepthOnlyIndexBuffer.Serialize(Ar, bNeedsCPUAccess);
	AccumIndexBufferSize(DepthOnlyIndexBuffer, OutBuffersSize.DepthOnlyIBSize);
	AccumIndexBufferSize(DepthOnlyIndexBuffer, OutBuffersSize.SerializedBuffersSize);
	if (!bEnableDepthOnlyIndexBuffer)
	{
		DepthOnlyIndexBuffer.Discard();
	}

	if (bSerializeReversedIndexBuffer)
	{
		SerializedAdditionalIndexBuffers->ReversedDepthOnlyIndexBuffer.Serialize(Ar, bNeedsCPUAccess);
		AccumIndexBufferSize(SerializedAdditionalIndexBuffers->ReversedDepthOnlyIndexBuffer, OutBuffersSize.ReversedIBsSize);
		AccumIndexBufferSize(SerializedAdditionalIndexBuffers->ReversedDepthOnlyIndexBuffer, OutBuffersSize.SerializedBuffersSize);
		if (!bEnableReversedIndexBuffer)
		{
			SerializedAdditionalIndexBuffers->ReversedDepthOnlyIndexBuffer.Discard();
		}
	}

	if (bSerializeWireframeIndexBuffer)
	{
		SerializedAdditionalIndexBuffers->WireframeIndexBuffer.Serialize(Ar, bNeedsCPUAccess);
		AccumIndexBufferSize(SerializedAdditionalIndexBuffers->WireframeIndexBuffer, OutBuffersSize.SerializedBuffersSize);
		bHasWireframeIndices = AdditionalIndexBuffers && SerializedAdditionalIndexBuffers->WireframeIndexBuffer.GetNumIndices() != 0;
	}

	if (bSerializeAdjacencyDataIndexBuffer)
	{
		SerializedAdditionalIndexBuffers->AdjacencyIndexBuffer.Serialize(Ar, bNeedsCPUAccess);
		AccumIndexBufferSize(SerializedAdditionalIndexBuffers->AdjacencyIndexBuffer, OutBuffersSize.SerializedBuffersSize);
		bHasAdjacencyInfo = AdditionalIndexBuffers && SerializedAdditionalIndexBuffers->AdjacencyIndexBuffer.GetNumIndices() != 0;
	}

	// Needs to be done now because on cooked platform, indices are discarded after RHIInit.
	bHasDepthOnlyIndices = DepthOnlyIndexBuffer.GetNumIndices() != 0;
	bHasReversedIndices = AdditionalIndexBuffers && bSerializeReversedIndexBuffer && SerializedAdditionalIndexBuffers->ReversedIndexBuffer.GetNumIndices() != 0;
	bHasReversedDepthOnlyIndices = AdditionalIndexBuffers && bSerializeReversedIndexBuffer && SerializedAdditionalIndexBuffers->ReversedDepthOnlyIndexBuffer.GetNumIndices() != 0;
	bHasColorVertexData = VertexBuffers.ColorVertexBuffer.GetNumVertices() > 0;
	DepthOnlyNumTriangles = DepthOnlyIndexBuffer.GetNumIndices() / 3;

	AreaWeightedSectionSamplers.SetNum(Sections.Num());
	for (FStaticMeshSectionAreaWeightedTriangleSampler& Sampler : AreaWeightedSectionSamplers)
	{
		Sampler.Serialize(Ar);
	}
	AreaWeightedSampler.Serialize(Ar);
}

void FStaticMeshLODResources::SerializeAvailabilityInfo(FArchive& Ar)
{
	const bool bEnableDepthOnlyIndexBuffer = !!CVarSupportDepthOnlyIndexBuffers.GetValueOnAnyThread();
	const bool bEnableReversedIndexBuffer = !!CVarSupportReversedIndexBuffers.GetValueOnAnyThread();

	Ar << DepthOnlyNumTriangles;
	uint32 Packed;
#if WITH_EDITOR
	if (Ar.IsSaving())
	{
		Packed = bHasAdjacencyInfo
			| (bHasDepthOnlyIndices << 1u)
			| (bHasReversedIndices << 2u)
			| (bHasReversedDepthOnlyIndices << 3u)
			| (bHasColorVertexData << 4u)
			| (bHasWireframeIndices << 5u);
		Ar << Packed;
	}
	else
#endif
	{
		Ar << Packed;
		DepthOnlyNumTriangles *= static_cast<uint32>(bEnableDepthOnlyIndexBuffer);
		bHasAdjacencyInfo = Packed & 1u;
		bHasDepthOnlyIndices = bEnableDepthOnlyIndexBuffer && !!(Packed & 2u);
		bHasReversedIndices = bEnableReversedIndexBuffer && !!(Packed & 4u);
		bHasReversedDepthOnlyIndices = bEnableReversedIndexBuffer && !!(Packed & 8u);
		bHasColorVertexData = (Packed >> 4u) & 1u;
		bHasWireframeIndices = (Packed >> 5u) & 1u;
	}

	VertexBuffers.StaticMeshVertexBuffer.SerializeMetaData(Ar);
	VertexBuffers.PositionVertexBuffer.SerializeMetaData(Ar);
	VertexBuffers.ColorVertexBuffer.SerializeMetaData(Ar);
	IndexBuffer.SerializeMetaData(Ar);

	FAdditionalStaticMeshIndexBuffers DummyBuffers;
	FAdditionalStaticMeshIndexBuffers* SerializedAdditionalIndexBuffers = &DummyBuffers;
	if ((bEnableDepthOnlyIndexBuffer || bEnableReversedIndexBuffer) && (bHasReversedIndices || bHasAdjacencyInfo || bHasWireframeIndices || bHasDepthOnlyIndices))
	{
		if (AdditionalIndexBuffers == nullptr)
		{
			AdditionalIndexBuffers = new FAdditionalStaticMeshIndexBuffers();
		}
		SerializedAdditionalIndexBuffers = AdditionalIndexBuffers;
	}

	SerializedAdditionalIndexBuffers->ReversedIndexBuffer.SerializeMetaData(Ar);
	if (!bHasReversedIndices)
	{
		// Reversed indices are either stripped during cook or will be stripped on load.
		// In either case, clear CachedNumIndices to show that the buffer will be empty after actual loading
		SerializedAdditionalIndexBuffers->ReversedIndexBuffer.Discard();
	}
	DepthOnlyIndexBuffer.SerializeMetaData(Ar);
	if (!bHasDepthOnlyIndices)
	{
		DepthOnlyIndexBuffer.Discard();
	}
	SerializedAdditionalIndexBuffers->ReversedDepthOnlyIndexBuffer.SerializeMetaData(Ar);
	if (!bHasReversedDepthOnlyIndices)
	{
		SerializedAdditionalIndexBuffers->ReversedDepthOnlyIndexBuffer.Discard();
	}
	SerializedAdditionalIndexBuffers->WireframeIndexBuffer.SerializeMetaData(Ar);
	if (!bHasWireframeIndices)
	{
		SerializedAdditionalIndexBuffers->WireframeIndexBuffer.Discard();
	}
	SerializedAdditionalIndexBuffers->AdjacencyIndexBuffer.SerializeMetaData(Ar);
	if (!bHasAdjacencyInfo)
	{
		SerializedAdditionalIndexBuffers->AdjacencyIndexBuffer.Discard();
	}
}

void FStaticMeshLODResources::ClearAvailabilityInfo()
{
	DepthOnlyNumTriangles = 0;
	bHasAdjacencyInfo = false;
	bHasDepthOnlyIndices = false;
	bHasReversedIndices = false;
	bHasReversedDepthOnlyIndices = false;
	bHasColorVertexData = false;
	bHasWireframeIndices = false;
	VertexBuffers.StaticMeshVertexBuffer.ClearMetaData();
	VertexBuffers.PositionVertexBuffer.ClearMetaData();
	VertexBuffers.ColorVertexBuffer.ClearMetaData();
	delete AdditionalIndexBuffers;
	AdditionalIndexBuffers = nullptr;
}

void FStaticMeshLODResources::Serialize(FArchive& Ar, UObject* Owner, int32 Index)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("FStaticMeshLODResources::Serialize"), STAT_StaticMeshLODResources_Serialize, STATGROUP_LoadTime);

	bool bUsingCookedEditorData = false;
#if WITH_EDITORONLY_DATA
	bUsingCookedEditorData = Owner->GetOutermost()->bIsCookedForEditor;
#endif

	UStaticMesh* OwnerStaticMesh = Cast<UStaticMesh>(Owner);
	// Actual flags used during serialization
	const uint8 ClassDataStripFlags = GenerateClassStripFlags(Ar, OwnerStaticMesh, Index);
	FStripDataFlags StripFlags(Ar, ClassDataStripFlags);

	Ar << Sections;
	Ar << MaxDeviation;

#if WITH_EDITORONLY_DATA
	if ((!Ar.IsCooking() && !Ar.IsFilterEditorOnly()) || (Ar.IsCooking() && Ar.CookingTarget()->HasEditorOnlyData()))
	{
		Ar << WedgeMap;
	}
#endif // #if WITH_EDITORONLY_DATA

	const bool bIsBelowMinLOD = StripFlags.IsClassDataStripped(CDSF_MinLodData);
	bool bIsLODCookedOut = IsLODCookedOut(Ar.CookingTarget(), OwnerStaticMesh, bIsBelowMinLOD);
	Ar << bIsLODCookedOut;

	bool bInlined = bIsLODCookedOut || IsLODInlined(Ar.CookingTarget(), OwnerStaticMesh, Index, bIsBelowMinLOD);
	Ar << bInlined;
	bBuffersInlined = bInlined;

	if (!StripFlags.IsDataStrippedForServer() && !bIsLODCookedOut)
	{
		FStaticMeshBuffersSize TmpBuffersSize;
		TArray<uint8> TmpBuff;

		if (bInlined)
		{
			SerializeBuffers(Ar, OwnerStaticMesh, ClassDataStripFlags, TmpBuffersSize);
			Ar << TmpBuffersSize;
			BuffersSize = TmpBuffersSize.CalcBuffersSize();
		}
		else if (FPlatformProperties::RequiresCookedData() || Ar.IsCooking() || bUsingCookedEditorData)
		{
			uint32 BulkDataSize = 0;
#if WITH_EDITOR
			if (Ar.IsSaving())
			{
				const int32 MaxNumOptionalLODs = GetNumOptionalLODsAllowed(Ar.CookingTarget(), OwnerStaticMesh);
				const int32 OptionalLODIdx = GetPlatformMinLODIdx(Ar.CookingTarget(), OwnerStaticMesh) - Index;
				const bool bDiscardBulkData = OptionalLODIdx > MaxNumOptionalLODs;

				if (!bDiscardBulkData)
				{
					FMemoryWriter MemWriter(TmpBuff, true);
					MemWriter.SetCookingTarget(Ar.CookingTarget());
					MemWriter.SetByteSwapping(Ar.IsByteSwapping());
					SerializeBuffers(MemWriter, OwnerStaticMesh, ClassDataStripFlags, TmpBuffersSize);
				}

				bIsOptionalLOD = bIsBelowMinLOD;
				const uint32 BulkDataFlags = (bDiscardBulkData ? 0 : BULKDATA_Force_NOT_InlinePayload)
					| (bIsOptionalLOD ? BULKDATA_OptionalPayload : 0);
				const uint32 OldBulkDataFlags = BulkData.GetBulkDataFlags();
				BulkData.ClearBulkDataFlags(0xffffffffu);
				BulkData.SetBulkDataFlags(BulkDataFlags);
				if (TmpBuff.Num() > 0)
				{
					BulkData.Lock(LOCK_READ_WRITE);
					void* BulkDataMem = BulkData.Realloc(TmpBuff.Num());
					FMemory::Memcpy(BulkDataMem, TmpBuff.GetData(), TmpBuff.Num());
					BulkData.Unlock();
				}
				BulkData.Serialize(Ar, Owner, Index);
				BulkData.ClearBulkDataFlags(0xffffffffu);
				BulkData.SetBulkDataFlags(OldBulkDataFlags);
			}
			else
#endif
			{
#if USE_BULKDATA_STREAMING_TOKEN
				FByteBulkData TmpBulkData;
				TmpBulkData.Serialize(Ar, Owner, Index, false);
				bIsOptionalLOD = TmpBulkData.IsOptional();

				StreamingBulkData = TmpBulkData.CreateStreamingToken();			
#else
				StreamingBulkData.Serialize(Ar, Owner, Index, false);
				bIsOptionalLOD = StreamingBulkData.IsOptional();
#endif
				BulkDataSize = (uint32)StreamingBulkData.GetBulkDataSize();

#if WITH_EDITORONLY_DATA
				// Streaming CPU data in editor build isn't supported yet because tools and utils need access
				if (bUsingCookedEditorData && BulkDataSize > 0)
				{
					TmpBuff.Empty(BulkDataSize);
					TmpBuff.AddUninitialized(BulkDataSize);
					void* Dest = TmpBuff.GetData();
					TmpBulkData.GetCopy(&Dest);
				}
#endif
			}

			SerializeAvailabilityInfo(Ar);

			Ar << TmpBuffersSize;
			BuffersSize = TmpBuffersSize.CalcBuffersSize();

			if (Ar.IsLoading() && bIsOptionalLOD)
			{
				ClearAvailabilityInfo();
			}

#if WITH_EDITORONLY_DATA
			if (Ar.IsLoading() && bUsingCookedEditorData && BulkDataSize > 0)
			{
				ClearAvailabilityInfo();
				FMemoryReader MemReader(TmpBuff, true);
				MemReader.SetByteSwapping(Ar.IsByteSwapping());
				SerializeBuffers(MemReader, OwnerStaticMesh, ClassDataStripFlags, TmpBuffersSize);
			}
#endif
		}
	}
}

int32 FStaticMeshLODResources::GetNumTriangles() const
{
	int32 NumTriangles = 0;
	for(int32 SectionIndex = 0;SectionIndex < Sections.Num();SectionIndex++)
	{
		NumTriangles += Sections[SectionIndex].NumTriangles;
	}
	return NumTriangles;
}

int32 FStaticMeshLODResources::GetNumVertices() const
{
	return VertexBuffers.StaticMeshVertexBuffer.GetNumVertices();
}

int32 FStaticMeshLODResources::GetNumTexCoords() const
{
	return VertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords();
}

void FStaticMeshVertexFactories::InitVertexFactory(
	const FStaticMeshLODResources& LodResources,
	FLocalVertexFactory& InOutVertexFactory,
	uint32 LODIndex,
	const UStaticMesh* InParentMesh,
	bool bInOverrideColorVertexBuffer
	)
{
	check( InParentMesh != NULL );

	struct InitStaticMeshVertexFactoryParams
	{
		FLocalVertexFactory* VertexFactory;
		const FStaticMeshLODResources* LODResources;
		bool bOverrideColorVertexBuffer;
		uint32 LightMapCoordinateIndex;
		uint32 LODIndex;
	} Params;

	uint32 LightMapCoordinateIndex = (uint32)InParentMesh->LightMapCoordinateIndex;
	LightMapCoordinateIndex = LightMapCoordinateIndex < LodResources.VertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords() ? LightMapCoordinateIndex : LodResources.VertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords() - 1;

	Params.VertexFactory = &InOutVertexFactory;
	Params.LODResources = &LodResources;
	Params.bOverrideColorVertexBuffer = bInOverrideColorVertexBuffer;
	Params.LightMapCoordinateIndex = LightMapCoordinateIndex;
	Params.LODIndex = LODIndex;

	// Initialize the static mesh's vertex factory.
	ENQUEUE_RENDER_COMMAND(InitStaticMeshVertexFactory)(
		[Params](FRHICommandListImmediate& RHICmdList)
		{
			FLocalVertexFactory::FDataType Data;

			Params.LODResources->VertexBuffers.PositionVertexBuffer.BindPositionVertexBuffer(Params.VertexFactory, Data);
			Params.LODResources->VertexBuffers.StaticMeshVertexBuffer.BindTangentVertexBuffer(Params.VertexFactory, Data);
			Params.LODResources->VertexBuffers.StaticMeshVertexBuffer.BindPackedTexCoordVertexBuffer(Params.VertexFactory, Data);
			Params.LODResources->VertexBuffers.StaticMeshVertexBuffer.BindLightMapVertexBuffer(Params.VertexFactory, Data, Params.LightMapCoordinateIndex);

			// bOverrideColorVertexBuffer means we intend to override the color later.  We must construct the vertexfactory such that it believes a proper stride (not 0) is set for
			// the color stream so that the real stream works later.
			if(Params.bOverrideColorVertexBuffer)
			{ 
				FColorVertexBuffer::BindDefaultColorVertexBuffer(Params.VertexFactory, Data, FColorVertexBuffer::NullBindStride::FColorSizeForComponentOverride);
			}
			//otherwise just bind the incoming buffer directly.
			else
			{
				Params.LODResources->VertexBuffers.ColorVertexBuffer.BindColorVertexBuffer(Params.VertexFactory, Data);
			}

			Data.LODLightmapDataIndex = Params.LODIndex;
			Params.VertexFactory->SetData(Data);
			Params.VertexFactory->InitResource();
		});
}

void FStaticMeshVertexFactories::InitResources(const FStaticMeshLODResources& LodResources, uint32 LODIndex, const UStaticMesh* Parent)
{
	InitVertexFactory(LodResources, VertexFactory, LODIndex, Parent, false);
	BeginInitResource(&VertexFactory);

	InitVertexFactory(LodResources, VertexFactoryOverrideColorVertexBuffer, LODIndex, Parent, true);
	BeginInitResource(&VertexFactoryOverrideColorVertexBuffer);
}

void FStaticMeshVertexFactories::ReleaseResources()
{
	// Release the vertex factories.
	BeginReleaseResource(&VertexFactory);
	BeginReleaseResource(&VertexFactoryOverrideColorVertexBuffer);

	if (SplineVertexFactory)
	{
		BeginReleaseResource(SplineVertexFactory);		
	}
	if (SplineVertexFactoryOverrideColorVertexBuffer)
	{
		BeginReleaseResource(SplineVertexFactoryOverrideColorVertexBuffer);		
	}
}

FStaticMeshVertexFactories::~FStaticMeshVertexFactories()
{
	delete SplineVertexFactory;
	delete SplineVertexFactoryOverrideColorVertexBuffer;
}

FStaticMeshSectionAreaWeightedTriangleSampler::FStaticMeshSectionAreaWeightedTriangleSampler()
	: Owner(nullptr)
	, SectionIdx(INDEX_NONE)
{
}

void FStaticMeshSectionAreaWeightedTriangleSampler::Init(FStaticMeshLODResources* InOwner, int32 InSectionIdx)
{
	Owner = InOwner;
	SectionIdx = InSectionIdx;
	Initialize();
}

float FStaticMeshSectionAreaWeightedTriangleSampler::GetWeights(TArray<float>& OutWeights)
{
	//If these hit, you're trying to get weights on a sampler that's not been initialized.
	check(Owner);
	check(SectionIdx != INDEX_NONE);
	check(Owner->Sections.IsValidIndex(SectionIdx));
	FIndexArrayView Indicies = Owner->IndexBuffer.GetArrayView();
	FStaticMeshSection& Section = Owner->Sections[SectionIdx];

	int32 First = Section.FirstIndex;
	int32 Last = First + Section.NumTriangles * 3;
	float Total = 0.0f;
	OutWeights.Empty(Indicies.Num() / 3);
	for (int32 i = First; i < Last; i+=3)
	{
		FVector V0 = Owner->VertexBuffers.PositionVertexBuffer.VertexPosition(Indicies[i]);
		FVector V1 = Owner->VertexBuffers.PositionVertexBuffer.VertexPosition(Indicies[i + 1]);
		FVector V2 = Owner->VertexBuffers.PositionVertexBuffer.VertexPosition(Indicies[i + 2]);

		float Area = ((V1 - V0) ^ (V2 - V0)).Size() * 0.5f;
		OutWeights.Add(Area);
		Total += Area;
	}
	return Total;
}

FStaticMeshAreaWeightedSectionSampler::FStaticMeshAreaWeightedSectionSampler()
	: Owner(nullptr)
{
}

void FStaticMeshAreaWeightedSectionSampler::Init(FStaticMeshLODResources* InOwner)
{
	Owner = InOwner;
	Initialize();
}

float FStaticMeshAreaWeightedSectionSampler::GetWeights(TArray<float>& OutWeights)
{
	//If this hits, you're trying to get weights on a sampler that's not been initialized.
	check(Owner);
	float Total = 0.0f;
	OutWeights.Empty(Owner->Sections.Num());
	for (int32 i = 0; i < Owner->Sections.Num(); ++i)
	{
		float T = Owner->AreaWeightedSectionSamplers[i].GetTotalWeight();
		OutWeights.Add(T);
		Total += T;
	}
	return Total;
}

static inline void InitOrUpdateResource(FRenderResource* Resource)
{
	if (!Resource->IsInitialized())
	{
		Resource->InitResource();
	}
	else
	{
		Resource->UpdateRHI();
	}
}

void FStaticMeshVertexBuffers::InitModelBuffers(TArray<FModelVertex>& Vertices)
{
	if (Vertices.Num())
	{
		PositionVertexBuffer.Init(Vertices.Num());
		StaticMeshVertexBuffer.SetUseFullPrecisionUVs(true);
		StaticMeshVertexBuffer.Init(Vertices.Num(), 2);

		for (int32 i = 0; i < Vertices.Num(); i++)
		{
			const FModelVertex& Vertex = Vertices[i];

			PositionVertexBuffer.VertexPosition(i) = Vertex.Position;
			StaticMeshVertexBuffer.SetVertexTangents(i, Vertex.TangentX, Vertex.GetTangentY(), Vertex.TangentZ);
			StaticMeshVertexBuffer.SetVertexUV(i, 0, Vertex.TexCoord);
			StaticMeshVertexBuffer.SetVertexUV(i, 1, Vertex.ShadowTexCoord);
		}
	}
	else
	{
		PositionVertexBuffer.Init(1);
		StaticMeshVertexBuffer.Init(1, 2);

		PositionVertexBuffer.VertexPosition(0) = FVector(0, 0, 0);
		StaticMeshVertexBuffer.SetVertexTangents(0, FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1));
		StaticMeshVertexBuffer.SetVertexUV(0, 0, FVector2D(0, 0));
		StaticMeshVertexBuffer.SetVertexUV(0, 1, FVector2D(0, 0));
	}
}

void FStaticMeshVertexBuffers::InitModelVF(FLocalVertexFactory* VertexFactory)
{
	FStaticMeshVertexBuffers* Self = this;
	ENQUEUE_RENDER_COMMAND(StaticMeshVertexBuffersLegacyBspInit)(
		[VertexFactory, Self](FRHICommandListImmediate& RHICmdList)
	{
		check(Self->PositionVertexBuffer.IsInitialized());
		check(Self->StaticMeshVertexBuffer.IsInitialized());

		FLocalVertexFactory::FDataType Data;
		Self->PositionVertexBuffer.BindPositionVertexBuffer(VertexFactory, Data);
		Self->StaticMeshVertexBuffer.BindTangentVertexBuffer(VertexFactory, Data);
		Self->StaticMeshVertexBuffer.BindPackedTexCoordVertexBuffer(VertexFactory, Data);
		Self->StaticMeshVertexBuffer.BindLightMapVertexBuffer(VertexFactory, Data, 1);
		FColorVertexBuffer::BindDefaultColorVertexBuffer(VertexFactory, Data, FColorVertexBuffer::NullBindStride::ZeroForDefaultBufferBind);
		VertexFactory->SetData(Data);

		InitOrUpdateResource(VertexFactory);
	});
}

void FStaticMeshVertexBuffers::InitWithDummyData(FLocalVertexFactory* VertexFactory, uint32 NumVerticies, uint32 NumTexCoords, uint32 LightMapIndex)
{
	check(NumVerticies);
	check(NumTexCoords < MAX_STATIC_TEXCOORDS && NumTexCoords > 0);
	check(LightMapIndex < NumTexCoords);

	PositionVertexBuffer.Init(NumVerticies);
	StaticMeshVertexBuffer.Init(NumVerticies, NumTexCoords);
	ColorVertexBuffer.Init(NumVerticies);

	FStaticMeshVertexBuffers* Self = this;
	ENQUEUE_RENDER_COMMAND(StaticMeshVertexBuffersLegacyInit)(
		[VertexFactory, Self, LightMapIndex](FRHICommandListImmediate& RHICmdList)
	{
		InitOrUpdateResource(&Self->PositionVertexBuffer);
		InitOrUpdateResource(&Self->StaticMeshVertexBuffer);
		InitOrUpdateResource(&Self->ColorVertexBuffer);

		FLocalVertexFactory::FDataType Data;
		Self->PositionVertexBuffer.BindPositionVertexBuffer(VertexFactory, Data);
		Self->StaticMeshVertexBuffer.BindTangentVertexBuffer(VertexFactory, Data);
		Self->StaticMeshVertexBuffer.BindPackedTexCoordVertexBuffer(VertexFactory, Data);
		Self->StaticMeshVertexBuffer.BindLightMapVertexBuffer(VertexFactory, Data, LightMapIndex);
		Self->ColorVertexBuffer.BindColorVertexBuffer(VertexFactory, Data);
		VertexFactory->SetData(Data);

		InitOrUpdateResource(VertexFactory);
	});
}

void FStaticMeshVertexBuffers::InitFromDynamicVertex(FLocalVertexFactory* VertexFactory, TArray<FDynamicMeshVertex>& Vertices, uint32 NumTexCoords, uint32 LightMapIndex)
{
	check(NumTexCoords < MAX_STATIC_TEXCOORDS && NumTexCoords > 0);
	check(LightMapIndex < NumTexCoords);

	if (Vertices.Num())
	{
		PositionVertexBuffer.Init(Vertices.Num());
		StaticMeshVertexBuffer.Init(Vertices.Num(), NumTexCoords);
		ColorVertexBuffer.Init(Vertices.Num());

		for (int32 i = 0; i < Vertices.Num(); i++)
		{
			const FDynamicMeshVertex& Vertex = Vertices[i];

			PositionVertexBuffer.VertexPosition(i) = Vertex.Position;
			StaticMeshVertexBuffer.SetVertexTangents(i, Vertex.TangentX.ToFVector(), Vertex.GetTangentY(), Vertex.TangentZ.ToFVector());
			for (uint32 j = 0; j < NumTexCoords; j++)
			{
				StaticMeshVertexBuffer.SetVertexUV(i, j, Vertex.TextureCoordinate[j]);
			}
			ColorVertexBuffer.VertexColor(i) = Vertex.Color;
		}
	}
	else
	{
		PositionVertexBuffer.Init(1);
		StaticMeshVertexBuffer.Init(1, 1);
		ColorVertexBuffer.Init(1);

		PositionVertexBuffer.VertexPosition(0) = FVector(0, 0, 0);
		StaticMeshVertexBuffer.SetVertexTangents(0, FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1));
		StaticMeshVertexBuffer.SetVertexUV(0, 0, FVector2D(0, 0));
		ColorVertexBuffer.VertexColor(0) = FColor(1,1,1,1);
		NumTexCoords = 1;
		LightMapIndex = 0;
	}

	FStaticMeshVertexBuffers* Self = this;
	ENQUEUE_RENDER_COMMAND(StaticMeshVertexBuffersLegacyInit)(
		[VertexFactory, Self, LightMapIndex](FRHICommandListImmediate& RHICmdList)
		{
			InitOrUpdateResource(&Self->PositionVertexBuffer);
			InitOrUpdateResource(&Self->StaticMeshVertexBuffer);
			InitOrUpdateResource(&Self->ColorVertexBuffer);

			FLocalVertexFactory::FDataType Data;
			Self->PositionVertexBuffer.BindPositionVertexBuffer(VertexFactory, Data);
			Self->StaticMeshVertexBuffer.BindTangentVertexBuffer(VertexFactory, Data);
			Self->StaticMeshVertexBuffer.BindPackedTexCoordVertexBuffer(VertexFactory, Data);
			Self->StaticMeshVertexBuffer.BindLightMapVertexBuffer(VertexFactory, Data, LightMapIndex);
			Self->ColorVertexBuffer.BindColorVertexBuffer(VertexFactory, Data);
			VertexFactory->SetData(Data);

			InitOrUpdateResource(VertexFactory);
		});
};

FStaticMeshLODResources::FStaticMeshLODResources()
	: AdditionalIndexBuffers(nullptr)
	, DistanceFieldData(nullptr)
	, MaxDeviation(0.0f)
	, bHasAdjacencyInfo(false)
	, bHasDepthOnlyIndices(false)
	, bHasReversedIndices(false)
	, bHasReversedDepthOnlyIndices(false)
	, bHasColorVertexData(false)
	, bHasWireframeIndices(false)
	, bBuffersInlined(false)
	, bIsOptionalLOD(false)
	, DepthOnlyNumTriangles(0)
	, BuffersSize(0)
#if STATS
	, StaticMeshIndexMemory(0)
#endif
{
}

FStaticMeshLODResources::~FStaticMeshLODResources()
{
	delete DistanceFieldData;
	delete AdditionalIndexBuffers;
}

void FStaticMeshLODResources::ConditionalForce16BitIndexBuffer(EShaderPlatform MaxShaderPlatform, UStaticMesh* Parent)
{
	// Initialize the vertex and index buffers.
	// All platforms supporting Metal also support 32-bit indices.
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	if (IsES2Platform(MaxShaderPlatform) && !IsMetalPlatform(MaxShaderPlatform))
	{
		if (IndexBuffer.Is32Bit())
		{
			//TODO: Show this as an error in the static mesh editor when doing a Mobile preview so gets fixed in content
			TArray<uint32> Indices;
			IndexBuffer.GetCopy(Indices);
			IndexBuffer.SetIndices(Indices, EIndexBufferStride::Force16Bit);
			UE_LOG(LogStaticMesh, Warning, TEXT("[%s] Mesh has more that 65535 vertices, incompatible with mobile; forcing 16-bit (will probably cause rendering issues)."), *Parent->GetName());
		}
	}
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

template <bool bIncrement>
void FStaticMeshLODResources::UpdateIndexMemoryStats()
{
#if STATS
	if (bIncrement)
	{
		StaticMeshIndexMemory += IndexBuffer.GetAllocatedSize();
		StaticMeshIndexMemory += DepthOnlyIndexBuffer.GetAllocatedSize();

		if (AdditionalIndexBuffers)
		{
			StaticMeshIndexMemory += AdditionalIndexBuffers->WireframeIndexBuffer.GetAllocatedSize();
			StaticMeshIndexMemory += AdditionalIndexBuffers->ReversedIndexBuffer.GetAllocatedSize();
			StaticMeshIndexMemory += AdditionalIndexBuffers->ReversedDepthOnlyIndexBuffer.GetAllocatedSize();
			StaticMeshIndexMemory += AdditionalIndexBuffers->AdjacencyIndexBuffer.GetAllocatedSize();
		}

		INC_DWORD_STAT_BY(STAT_StaticMeshIndexMemory, StaticMeshIndexMemory);
	}
	else
	{
		DEC_DWORD_STAT_BY(STAT_StaticMeshIndexMemory, StaticMeshIndexMemory);
	}
#endif
}

template <bool bIncrement>
void FStaticMeshLODResources::UpdateVertexMemoryStats() const
{
#if STATS
	const uint32 StaticMeshVertexMemory =
		VertexBuffers.StaticMeshVertexBuffer.GetResourceSize() +
		VertexBuffers.PositionVertexBuffer.GetStride() * VertexBuffers.PositionVertexBuffer.GetNumVertices();
	const uint32 ResourceVertexColorMemory = VertexBuffers.ColorVertexBuffer.GetStride() * VertexBuffers.ColorVertexBuffer.GetNumVertices();

	if (bIncrement)
	{
		INC_DWORD_STAT_BY(STAT_StaticMeshVertexMemory, StaticMeshVertexMemory);
		INC_DWORD_STAT_BY(STAT_ResourceVertexColorMemory, ResourceVertexColorMemory);
	}
	else
	{
		DEC_DWORD_STAT_BY(STAT_StaticMeshVertexMemory, StaticMeshVertexMemory);
		DEC_DWORD_STAT_BY(STAT_ResourceVertexColorMemory, ResourceVertexColorMemory);
	}
#endif
}

void FStaticMeshLODResources::InitResources(UStaticMesh* Parent)
{
	ConditionalForce16BitIndexBuffer(GMaxRHIShaderPlatform, Parent);
	UpdateIndexMemoryStats<true>();

	BeginInitResource(&IndexBuffer);
	if(bHasWireframeIndices)
	{
		BeginInitResource(&AdditionalIndexBuffers->WireframeIndexBuffer);
	}
	BeginInitResource(&VertexBuffers.StaticMeshVertexBuffer);
	BeginInitResource(&VertexBuffers.PositionVertexBuffer);
	if(bHasColorVertexData)
	{
		BeginInitResource(&VertexBuffers.ColorVertexBuffer);
	}

	if (bHasReversedIndices)
	{
		BeginInitResource(&AdditionalIndexBuffers->ReversedIndexBuffer);
	}

	if (bHasDepthOnlyIndices)
	{
		BeginInitResource(&DepthOnlyIndexBuffer);
	}

	if (bHasReversedDepthOnlyIndices)
	{
		BeginInitResource(&AdditionalIndexBuffers->ReversedDepthOnlyIndexBuffer);
	}

	if (bHasAdjacencyInfo && RHISupportsTessellation(GMaxRHIShaderPlatform))
	{
		BeginInitResource(&AdditionalIndexBuffers->AdjacencyIndexBuffer);
	}

	if (Parent->bSupportGpuUniformlyDistributedSampling && Parent->bSupportUniformlyDistributedSampling && Parent->bAllowCPUAccess)
	{
		BeginInitResource(&AreaWeightedSectionSamplersBuffer);
	}

#if RHI_RAYTRACING
	if (IsRayTracingEnabled())
	{
		ENQUEUE_RENDER_COMMAND(InitStaticMeshRayTracingGeometry)(
			[this](FRHICommandListImmediate& RHICmdList)
			{
				FRayTracingGeometryInitializer Initializer;
				
				Initializer.IndexBuffer = IndexBuffer.IndexBufferRHI;
				Initializer.TotalPrimitiveCount = 0; // This is calculated below based on static mesh section data
				Initializer.GeometryType = RTGT_Triangles;
				Initializer.bFastBuild = false;
				
				TArray<FRayTracingGeometrySegment> GeometrySections;
				GeometrySections.Reserve(Sections.Num());
				for (const FStaticMeshSection& Section : Sections)
				{
					FRayTracingGeometrySegment Segment;
					Segment.VertexBuffer = VertexBuffers.PositionVertexBuffer.VertexBufferRHI;
					Segment.VertexBufferElementType = VET_Float3;
					Segment.VertexBufferStride = VertexBuffers.PositionVertexBuffer.GetStride();
					Segment.VertexBufferOffset = 0;
					Segment.FirstPrimitive = Section.FirstIndex / 3;
					Segment.NumPrimitives = Section.NumTriangles;
					GeometrySections.Add(Segment);
					Initializer.TotalPrimitiveCount += Section.NumTriangles;
				}
				Initializer.Segments = GeometrySections;
				
				RayTracingGeometry.SetInitializer(Initializer);
				RayTracingGeometry.InitResource();
			}
		);
	}
#endif // RHI_RAYTRACING

	if (DistanceFieldData)
	{
		DistanceFieldData->VolumeTexture.Initialize(Parent);
		INC_DWORD_STAT_BY( STAT_StaticMeshDistanceFieldMemory, DistanceFieldData->GetResourceSizeBytes() );
	}

#if STATS
	ENQUEUE_RENDER_COMMAND(UpdateMemoryStats)(
		[this](FRHICommandListImmediate&)
	{
		UpdateVertexMemoryStats<true>();
	});
#endif
}

void FStaticMeshLODResources::ReleaseResources()
{
	UpdateVertexMemoryStats<false>();
	UpdateIndexMemoryStats<false>();

	// Release the vertex and index buffers.
	


	BeginReleaseResource(&IndexBuffer);
	
	BeginReleaseResource(&VertexBuffers.StaticMeshVertexBuffer);
	BeginReleaseResource(&VertexBuffers.PositionVertexBuffer);
	BeginReleaseResource(&VertexBuffers.ColorVertexBuffer);
	BeginReleaseResource(&DepthOnlyIndexBuffer);
	BeginReleaseResource(&AreaWeightedSectionSamplersBuffer);

	if (AdditionalIndexBuffers)
	{
		// AdjacencyIndexBuffer may not be initialized at this time, but it is safe to release it anyway.
		// The bInitialized flag will be safely checked in the render thread.
		// This avoids a race condition regarding releasing this resource.
		BeginReleaseResource(&AdditionalIndexBuffers->AdjacencyIndexBuffer);
		BeginReleaseResource(&AdditionalIndexBuffers->ReversedIndexBuffer);
		BeginReleaseResource(&AdditionalIndexBuffers->WireframeIndexBuffer);
		BeginReleaseResource(&AdditionalIndexBuffers->ReversedDepthOnlyIndexBuffer);
	}
#if RHI_RAYTRACING
	BeginReleaseResource(&RayTracingGeometry);
#endif // RHI_RAYTRACING

	if (DistanceFieldData)
	{
		DEC_DWORD_STAT_BY( STAT_StaticMeshDistanceFieldMemory, DistanceFieldData->GetResourceSizeBytes() );
		DistanceFieldData->VolumeTexture.Release();
	}
}

void FStaticMeshLODResources::IncrementMemoryStats()
{
	UpdateIndexMemoryStats<true>();
	UpdateVertexMemoryStats<true>();
}

void FStaticMeshLODResources::DecrementMemoryStats()
{
	UpdateVertexMemoryStats<false>();
	UpdateIndexMemoryStats<false>();
}

void FStaticMeshLODResources::DiscardCPUData()
{
	VertexBuffers.StaticMeshVertexBuffer.CleanUp();
	VertexBuffers.PositionVertexBuffer.CleanUp();
	VertexBuffers.ColorVertexBuffer.CleanUp();
	IndexBuffer.Discard();
	DepthOnlyIndexBuffer.Discard();

	if (AdditionalIndexBuffers)
	{
		AdditionalIndexBuffers->ReversedIndexBuffer.Discard();
		AdditionalIndexBuffers->ReversedDepthOnlyIndexBuffer.Discard();
		AdditionalIndexBuffers->WireframeIndexBuffer.Discard();
		AdditionalIndexBuffers->AdjacencyIndexBuffer.Discard();
	}
}

/*------------------------------------------------------------------------------
	FStaticMeshRenderData
------------------------------------------------------------------------------*/

FStaticMeshRenderData::FStaticMeshRenderData()
	: bLODsShareStaticLighting(false)
	, bReadyForStreaming(false)
	, NumInlinedLODs(0)
	, CurrentFirstLODIdx(0)
{
	for (int32 LODIndex = 0; LODIndex < MAX_STATIC_MESH_LODS; ++LODIndex)
	{
		ScreenSize[LODIndex] = 0.0f;
	}
}

void FStaticMeshRenderData::Serialize(FArchive& Ar, UStaticMesh* Owner, bool bCooked)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FStaticMeshRenderData::Serialize);

	DECLARE_SCOPE_CYCLE_COUNTER( TEXT("FStaticMeshRenderData::Serialize"), STAT_StaticMeshRenderData_Serialize, STATGROUP_LoadTime );

	// Note: this is all derived data, native versioning is not needed, but be sure to bump STATICMESH_DERIVEDDATA_VER when modifying!
#if WITH_EDITOR
	const bool bHasEditorData = !Owner->GetOutermost()->bIsCookedForEditor;
	if (Ar.IsSaving() && bHasEditorData)
	{
		ResolveSectionInfo(Owner);
	}
#endif
#if WITH_EDITORONLY_DATA
	if (!bCooked)
	{
		Ar << MaterialIndexToImportIndex;
	}

#endif // #if WITH_EDITORONLY_DATA

	LODResources.Serialize(Ar, Owner);
#if WITH_EDITOR
	if (Ar.IsSaving())
	{
		int32 Tmp = 0;
		for (int32 Idx = LODResources.Num() - 1; Idx >= 0; --Idx)
		{
			if (!LODResources[Idx].bBuffersInlined)
			{
				break;
			}
			++Tmp;
		}
		NumInlinedLODs = Tmp;
	}
#endif
	Ar << NumInlinedLODs;
	CurrentFirstLODIdx = LODResources.Num() - NumInlinedLODs;
	Owner->SetCachedNumResidentLODs(NumInlinedLODs);

	if (Ar.IsLoading())
	{
		LODVertexFactories.Empty(LODResources.Num());
		for (int i = 0; i < LODResources.Num(); i++)
		{
			LODVertexFactories.Add(new FStaticMeshVertexFactories(GMaxRHIFeatureLevel));
		}
	}

	// Inline the distance field derived data for cooked builds
	if (bCooked)
	{
		// Defined class flags for possible stripping
		const uint8 DistanceFieldDataStripFlag = 1;

		// Actual flags used during serialization
		uint8 ClassDataStripFlags = 0;

#if WITH_EDITOR
		const bool bWantToStripDistanceFieldData = Ar.IsCooking() && (!Ar.CookingTarget()->SupportsFeature(ETargetPlatformFeatures::DeferredRendering) || !Ar.CookingTarget()->SupportsFeature(ETargetPlatformFeatures::DistanceFieldAO));

		ClassDataStripFlags |= (bWantToStripDistanceFieldData ? DistanceFieldDataStripFlag : 0);
#endif

		FStripDataFlags StripFlags(Ar, ClassDataStripFlags);
		if (!StripFlags.IsDataStrippedForServer() && !StripFlags.IsClassDataStripped(DistanceFieldDataStripFlag))
		{
			if (Ar.IsSaving())
			{
				GDistanceFieldAsyncQueue->BlockUntilBuildComplete(Owner, false);
			}

			for (int32 ResourceIndex = 0; ResourceIndex < LODResources.Num(); ResourceIndex++)
			{
				FStaticMeshLODResources& LOD = LODResources[ResourceIndex];
				
				bool bValid = (LOD.DistanceFieldData != nullptr);

				Ar << bValid;

				if (bValid)
				{
#if WITH_EDITOR
					if (Ar.IsCooking() && Ar.IsSaving())
					{
						check(LOD.DistanceFieldData != nullptr);

						float Divider = Ar.CookingTarget()->GetDownSampleMeshDistanceFieldDivider();

						if (Divider > 1)
						{
							FDistanceFieldVolumeData DownSampledDFVolumeData = *LOD.DistanceFieldData;
							IMeshUtilities& MeshUtilities = FModuleManager::Get().LoadModuleChecked<IMeshUtilities>(TEXT("MeshUtilities"));

							MeshUtilities.DownSampleDistanceFieldVolumeData(DownSampledDFVolumeData, Divider);

							Ar << DownSampledDFVolumeData;
						}
						else
						{
							Ar << *(LOD.DistanceFieldData);
						}
					}
					else
#endif
					{
						if (LOD.DistanceFieldData == nullptr)
						{
							LOD.DistanceFieldData = new FDistanceFieldVolumeData();
						}

						Ar << *(LOD.DistanceFieldData);
					}
				}
			}
		}
	}

	Ar << Bounds;
	Ar << bLODsShareStaticLighting;

	if (Ar.IsLoading() && Ar.CustomVer(FRenderingObjectVersion::GUID) < FRenderingObjectVersion::TextureStreamingMeshUVChannelData)
	{
		float DummyFactor;
		for (int32 TexCoordIndex = 0; TexCoordIndex < MAX_STATIC_TEXCOORDS; ++TexCoordIndex)
		{
			Ar << DummyFactor; // StreamingTextureFactors[TexCoordIndex];
		}
		Ar << DummyFactor; // MaxStreamingTextureFactor;
	}

	if (bCooked)
	{
		for (int32 LODIndex = 0; LODIndex < MAX_STATIC_MESH_LODS; ++LODIndex)
		{
			Ar << ScreenSize[LODIndex];
		}
	}

	if (Ar.IsLoading() )
	{
		bool bStripDistanceFieldDataDuringLoad = (CVarStripDistanceFieldDataDuringLoad.GetValueOnAnyThread() == 1);
		if( bStripDistanceFieldDataDuringLoad )
		{
			for (int32 ResourceIndex = 0; ResourceIndex < LODResources.Num(); ResourceIndex++)
			{
				FStaticMeshLODResources& LOD = LODResources[ResourceIndex];
				if( LOD.DistanceFieldData != nullptr )
				{
					delete LOD.DistanceFieldData;
					LOD.DistanceFieldData = nullptr;
				}
			}
		}
	}
}

void FStaticMeshRenderData::InitResources(ERHIFeatureLevel::Type InFeatureLevel, UStaticMesh* Owner)
{
#if WITH_EDITOR
	ResolveSectionInfo(Owner);
#endif // #if WITH_EDITOR

	for (int32 LODIndex = 0; LODIndex < LODResources.Num(); ++LODIndex)
	{
		// Skip LODs that have their render data stripped
		if (LODResources[LODIndex].VertexBuffers.StaticMeshVertexBuffer.GetNumVertices() > 0)
		{
			LODResources[LODIndex].InitResources(Owner);
			LODVertexFactories[LODIndex].InitResources(LODResources[LODIndex], LODIndex, Owner);
		}
		else if (!LODIndex && LODResources[LODIndex].DistanceFieldData)
		{
			FDistanceFieldVolumeData* DistanceFieldData = LODResources[LODIndex].DistanceFieldData;
			DistanceFieldData->VolumeTexture.Initialize(Owner);
			INC_DWORD_STAT_BY(STAT_StaticMeshDistanceFieldMemory, DistanceFieldData->GetResourceSizeBytes());
		}
	}

	ENQUEUE_RENDER_COMMAND(CmdSetStaticMeshReadyForStreaming)(
		[this, Owner](FRHICommandListImmediate&)
	{
		bReadyForStreaming = true;
		Owner->SetCachedReadyForStreaming(true);
	});
	bIsInitialized = true;
}

void FStaticMeshRenderData::ReleaseResources()
{
	for (int32 LODIndex = 0; LODIndex < LODResources.Num(); ++LODIndex)
	{
		if (LODResources[LODIndex].VertexBuffers.StaticMeshVertexBuffer.GetNumVertices() > 0)
		{
			LODResources[LODIndex].ReleaseResources();
			LODVertexFactories[LODIndex].ReleaseResources();
		}
		else if (!LODIndex && LODResources[LODIndex].DistanceFieldData)
		{
			FDistanceFieldVolumeData* DistanceFieldData = LODResources[LODIndex].DistanceFieldData;
			DEC_DWORD_STAT_BY(STAT_StaticMeshDistanceFieldMemory, DistanceFieldData->GetResourceSizeBytes());
			DistanceFieldData->VolumeTexture.Release();
		}
	}
}

void FStaticMeshRenderData::AllocateLODResources(int32 NumLODs)
{
	check(LODResources.Num() == 0);
	while (LODResources.Num() < NumLODs)
	{
		LODResources.Add(new FStaticMeshLODResources);
		LODVertexFactories.Add(new FStaticMeshVertexFactories(GMaxRHIFeatureLevel));
	}
}

FStaticMeshOccluderData::FStaticMeshOccluderData()
{
	VerticesSP = MakeShared<FOccluderVertexArray, ESPMode::ThreadSafe>();
	IndicesSP = MakeShared<FOccluderIndexArray, ESPMode::ThreadSafe>();
}

SIZE_T FStaticMeshOccluderData::GetResourceSizeBytes() const
{
	return VerticesSP->GetAllocatedSize() + IndicesSP->GetAllocatedSize();
}

TUniquePtr<FStaticMeshOccluderData> FStaticMeshOccluderData::Build(UStaticMesh* Owner)
{
	TUniquePtr<FStaticMeshOccluderData> Result;
#if WITH_EDITOR		
	if (Owner->LODForOccluderMesh >= 0)
	{
		// TODO: Custom geometry for occluder mesh?
		int32 LODIndex = FMath::Min(Owner->LODForOccluderMesh, Owner->RenderData->LODResources.Num()-1);
		const FStaticMeshLODResources& LODModel = Owner->RenderData->LODResources[LODIndex];
			
		const FRawStaticIndexBuffer& IndexBuffer = LODModel.DepthOnlyIndexBuffer.GetNumIndices() > 0 ? LODModel.DepthOnlyIndexBuffer : LODModel.IndexBuffer;
		int32 NumVtx = LODModel.VertexBuffers.PositionVertexBuffer.GetNumVertices();
		int32 NumIndices = IndexBuffer.GetNumIndices();
		
		if (NumVtx > 0 && NumIndices > 0 && !IndexBuffer.Is32Bit())
		{
			Result = MakeUnique<FStaticMeshOccluderData>();
		
			Result->VerticesSP->SetNumUninitialized(NumVtx);
			Result->IndicesSP->SetNumUninitialized(NumIndices);

			const FVector* V0 = &LODModel.VertexBuffers.PositionVertexBuffer.VertexPosition(0);
			const uint16* Indices = IndexBuffer.AccessStream16();

			FMemory::Memcpy(Result->VerticesSP->GetData(), V0, NumVtx*sizeof(FVector));
			FMemory::Memcpy(Result->IndicesSP->GetData(), Indices, NumIndices*sizeof(uint16));
		}
	}
#endif // WITH_EDITOR
	return Result;
}

void FStaticMeshOccluderData::SerializeCooked(FArchive& Ar, UStaticMesh* Owner)
{
#if WITH_EDITOR	
	if (Ar.IsSaving())
	{
		bool bHasOccluderData = false;
		if (Ar.CookingTarget()->SupportsFeature(ETargetPlatformFeatures::SoftwareOcclusion) && Owner->OccluderData.IsValid())
		{
			bHasOccluderData = true;
		}
		
		Ar << bHasOccluderData;
		
		if (bHasOccluderData)
		{
			Owner->OccluderData->VerticesSP->BulkSerialize(Ar);
			Owner->OccluderData->IndicesSP->BulkSerialize(Ar);
		}
	}
	else
#endif // WITH_EDITOR
	{
		bool bHasOccluderData;
		Ar << bHasOccluderData;
		if (bHasOccluderData)
		{
			Owner->OccluderData = MakeUnique<FStaticMeshOccluderData>();
			Owner->OccluderData->VerticesSP->BulkSerialize(Ar);
			Owner->OccluderData->IndicesSP->BulkSerialize(Ar);
		}
	}
}


#if WITH_EDITOR
/**
 * Calculates the view distance that a mesh should be displayed at.
 * @param MaxDeviation - The maximum surface-deviation between the reduced geometry and the original. This value should be acquired from Simplygon
 * @returns The calculated view distance	 
 */
static float CalculateViewDistance(float MaxDeviation, float AllowedPixelError)
{
	// We want to solve for the depth in world space given the screen space distance between two pixels
	//
	// Assumptions:
	//   1. There is no scaling in the view matrix.
	//   2. The horizontal FOV is 90 degrees.
	//   3. The backbuffer is 1920x1080.
	//
	// If we project two points at (X,Y,Z) and (X',Y,Z) from view space, we get their screen
	// space positions: (X/Z, Y'/Z) and (X'/Z, Y'/Z) where Y' = Y * AspectRatio.
	//
	// The distance in screen space is then sqrt( (X'-X)^2/Z^2 + (Y'-Y')^2/Z^2 )
	// or (X'-X)/Z. This is in clip space, so PixelDist = 1280 * 0.5 * (X'-X)/Z.
	//
	// Solving for Z: ViewDist = (X'-X * 640) / PixelDist

	const float ViewDistance = (MaxDeviation * 960.0f) / FMath::Max(AllowedPixelError, UStaticMesh::MinimumAutoLODPixelError);
	return ViewDistance;
}

void FStaticMeshRenderData::ResolveSectionInfo(UStaticMesh* Owner)
{
	int32 LODIndex = 0;
	int32 MaxLODs = LODResources.Num();
	check(MaxLODs <= MAX_STATIC_MESH_LODS);
	for (; LODIndex < MaxLODs; ++LODIndex)
	{
		FStaticMeshLODResources& LOD = LODResources[LODIndex];
		for (int32 SectionIndex = 0; SectionIndex < LOD.Sections.Num(); ++SectionIndex)
		{
			FMeshSectionInfo Info = Owner->GetSectionInfoMap().Get(LODIndex,SectionIndex);
			FStaticMeshSection& Section = LOD.Sections[SectionIndex];
			Section.MaterialIndex = Info.MaterialIndex;
			Section.bEnableCollision = Info.bEnableCollision;
			Section.bCastShadow = Info.bCastShadow;
		}

		// Arbitrary constant used as a base in Pow(K, LODIndex) that achieves much the same progression as a
		// conversion of the old 1 / (MaxLODs * LODIndex) passed through the newer bounds computation.
		// i.e. this achieves much the same results, but is still fairly arbitrary.
		const float AutoComputeLODPowerBase = 0.75f;

		if (Owner->bAutoComputeLODScreenSize)
		{
			if (LODIndex == 0)
			{
				ScreenSize[LODIndex].Default = 1.0f;
			}
			else if(LOD.MaxDeviation <= 0.0f)
			{
				ScreenSize[LODIndex].Default = FMath::Pow(AutoComputeLODPowerBase, LODIndex);
			}
			else
			{
				const float PixelError = Owner->IsSourceModelValid(LODIndex) ? Owner->GetSourceModel(LODIndex).ReductionSettings.PixelError : UStaticMesh::MinimumAutoLODPixelError;
				const float ViewDistance = CalculateViewDistance(LOD.MaxDeviation, PixelError);

				// Generate a projection matrix.
				// ComputeBoundsScreenSize only uses (0, 0) and (1, 1) of this matrix.
				const float HalfFOV = PI * 0.25f;
				const float ScreenWidth = 1920.0f;
				const float ScreenHeight = 1080.0f;
				const FPerspectiveMatrix ProjMatrix(HalfFOV, ScreenWidth, ScreenHeight, 1.0f);

				// Note we offset ViewDistance by SphereRadius here because the MaxDeviation is known to be somewhere in the bounds of the mesh. 
				// It won't necessarily be at the origin. Before adding this factor for very high poly meshes it would calculate a very small deviation 
				// for LOD1 which translates to a very small ViewDistance and a large (larger than 1) ScreenSize. This meant you could clip the camera 
				// into the mesh but unless you were near its origin it wouldn't switch to LOD0. Adding SphereRadius to ViewDistance makes it so that 
				// the distance is to the bounds which corrects the problem.
				ScreenSize[LODIndex].Default = ComputeBoundsScreenSize(FVector::ZeroVector, Bounds.SphereRadius, FVector(0.0f, 0.0f, ViewDistance + Bounds.SphereRadius), ProjMatrix);
			}
			
			//We must enforce screen size coherence between LOD when we autocompute the LOD screensize
			//This case can happen if we mix auto generate LOD with custom LOD
			if (LODIndex > 0 && ScreenSize[LODIndex].Default > ScreenSize[LODIndex - 1].Default)
			{
				ScreenSize[LODIndex].Default = ScreenSize[LODIndex - 1].Default / 2.0f;
			}
		}
		else if (Owner->IsSourceModelValid(LODIndex))
		{
			ScreenSize[LODIndex] = Owner->GetSourceModel(LODIndex).ScreenSize;
		}
		else
		{
			check(LODIndex > 0);

			// No valid source model and we're not auto-generating. Auto-generate in this case
			// because we have nothing else to go on.
			const float Tolerance = 0.01f;
			float AutoDisplayFactor = FMath::Pow(AutoComputeLODPowerBase, LODIndex);

			// Make sure this fits in with the previous LOD
			ScreenSize[LODIndex].Default = FMath::Clamp(AutoDisplayFactor, 0.0f, ScreenSize[LODIndex-1].Default - Tolerance);
		}
	}
	for (; LODIndex < MAX_STATIC_MESH_LODS; ++LODIndex)
	{
		ScreenSize[LODIndex].Default = 0.0f;
	}
}

void FStaticMeshRenderData::SyncUVChannelData(const TArray<FStaticMaterial>& ObjectData)
{
	TUniquePtr< TArray<FMeshUVChannelInfo> > UpdateData = MakeUnique< TArray<FMeshUVChannelInfo> >();
	UpdateData->Empty(ObjectData.Num());

	for (const FStaticMaterial& StaticMaterial : ObjectData)
	{
		UpdateData->Add(StaticMaterial.UVChannelData);
	}

	ENQUEUE_RENDER_COMMAND(SyncUVChannelData)([this, UpdateData = MoveTemp(UpdateData)](FRHICommandListImmediate& RHICmdList)
	{
		FMemory::Memswap(&UVChannelDataPerMaterial, UpdateData.Get(), sizeof(TArray<FMeshUVChannelInfo>));
	});
}

/*------------------------------------------------------------------------------
	FStaticMeshLODSettings
------------------------------------------------------------------------------*/

void FStaticMeshLODSettings::Initialize(const FConfigFile& IniFile)
{
	check(!Groups.Num());
	// Ensure there is a default LOD group.
	Groups.FindOrAdd(NAME_None);

	// Read individual entries from a config file.
	const TCHAR* IniSection = TEXT("StaticMeshLODSettings");
	const FConfigSection* Section = IniFile.Find(IniSection);
	if (Section)
	{
		for (TMultiMap<FName,FConfigValue>::TConstIterator It(*Section); It; ++It)
		{
			FName GroupName = It.Key();
			FStaticMeshLODGroup& Group = Groups.FindOrAdd(GroupName);
			ReadEntry(Group, It.Value().GetValue());
		};
	}

	Groups.KeySort(FNameLexicalLess());
	GroupName2Index.Empty(Groups.Num());
	{
		int32 GroupIdx = 0;
		TMap<FName, FStaticMeshLODGroup>::TConstIterator It(Groups);
		for (; It; ++It, ++GroupIdx)
		{
			GroupName2Index.Add(It.Key(), GroupIdx);
		}
	}

	// Do some per-group initialization.
	for (TMap<FName,FStaticMeshLODGroup>::TIterator It(Groups); It; ++It)
	{
		FStaticMeshLODGroup& Group = It.Value();
		float PercentTrianglesPerLOD = Group.DefaultSettings[1].PercentTriangles;
		for (int32 LODIndex = 1; LODIndex < MAX_STATIC_MESH_LODS; ++LODIndex)
		{
			float PercentTriangles = Group.DefaultSettings[LODIndex-1].PercentTriangles;
			Group.DefaultSettings[LODIndex] = Group.DefaultSettings[LODIndex - 1];
			Group.DefaultSettings[LODIndex].PercentTriangles = PercentTriangles * PercentTrianglesPerLOD;
		}
	}
}

void FStaticMeshLODSettings::ReadEntry(FStaticMeshLODGroup& Group, FString Entry)
{
	FMeshReductionSettings& Settings = Group.DefaultSettings[0];
	FMeshReductionSettings& Bias = Group.SettingsBias;
	int32 Importance = EMeshFeatureImportance::Normal;

	// Trim whitespace at the beginning.
	Entry.TrimStartInline();

	FParse::Value(*Entry, TEXT("Name="), Group.DisplayName, TEXT("StaticMeshLODSettings"));

	// Remove brackets.
	Entry = Entry.Replace( TEXT("("), TEXT("") );
	Entry = Entry.Replace( TEXT(")"), TEXT("") );
		
	if (FParse::Value(*Entry, TEXT("NumLODs="), Group.DefaultNumLODs))
	{
		Group.DefaultNumLODs = FMath::Clamp<int32>(Group.DefaultNumLODs, 1, MAX_STATIC_MESH_LODS);
	}

	if (FParse::Value(*Entry, TEXT("MaxNumStreamedLODs="), Group.DefaultMaxNumStreamedLODs))
	{
		Group.DefaultMaxNumStreamedLODs = FMath::Max(Group.DefaultMaxNumStreamedLODs, 0);
	}

	if (FParse::Value(*Entry, TEXT("MaxNumOptionalLODs="), Group.DefaultMaxNumOptionalLODs))
	{
		Group.DefaultMaxNumOptionalLODs = FMath::Max(Group.DefaultMaxNumOptionalLODs, 0);
	}
	
	int32 LocalSupportLODStreaming = 0;
	if (FParse::Value(*Entry, TEXT("bSupportLODStreaming="), LocalSupportLODStreaming))
	{
		Group.bSupportLODStreaming = !!LocalSupportLODStreaming;
	}

	if (FParse::Value(*Entry, TEXT("LightMapResolution="), Group.DefaultLightMapResolution))
	{
		Group.DefaultLightMapResolution = FMath::Max<int32>(Group.DefaultLightMapResolution, 0);
		Group.DefaultLightMapResolution = (Group.DefaultLightMapResolution + 3) & (~3);
	}

	float BasePercentTriangles = 100.0f;
	if (FParse::Value(*Entry, TEXT("BasePercentTriangles="), BasePercentTriangles))
	{
		BasePercentTriangles = FMath::Clamp<float>(BasePercentTriangles, 0.0f, 100.0f);
		Group.DefaultSettings[0].PercentTriangles = BasePercentTriangles * 0.01f;
	}

	float LODPercentTriangles = 100.0f;
	if (FParse::Value(*Entry, TEXT("LODPercentTriangles="), LODPercentTriangles))
	{
		LODPercentTriangles = FMath::Clamp<float>(LODPercentTriangles, 0.0f, 100.0f);
		Group.DefaultSettings[1].PercentTriangles = LODPercentTriangles * 0.01f;
	}

	if (FParse::Value(*Entry, TEXT("MaxDeviation="), Settings.MaxDeviation))
	{
		Settings.MaxDeviation = FMath::Clamp<float>(Settings.MaxDeviation, 0.0f, 1000.0f);
	}

	if (FParse::Value(*Entry, TEXT("PixelError="), Settings.PixelError))
	{
		Settings.PixelError = FMath::Clamp<float>(Settings.PixelError, 1.0f, 1000.0f);
	}

	if (FParse::Value(*Entry, TEXT("WeldingThreshold="), Settings.WeldingThreshold))
	{
		Settings.WeldingThreshold = FMath::Clamp<float>(Settings.WeldingThreshold, 0.0f, 10.0f);
	}

	if (FParse::Value(*Entry, TEXT("HardAngleThreshold="), Settings.HardAngleThreshold))
	{
		Settings.HardAngleThreshold = FMath::Clamp<float>(Settings.HardAngleThreshold, 0.0f, 180.0f);
	}

	if (FParse::Value(*Entry, TEXT("SilhouetteImportance="), Importance))
	{
		Settings.SilhouetteImportance = (EMeshFeatureImportance::Type)FMath::Clamp<int32>(Importance, 0, EMeshFeatureImportance::Highest);
	}

	if (FParse::Value(*Entry, TEXT("TextureImportance="), Importance))
	{
		Settings.TextureImportance = (EMeshFeatureImportance::Type)FMath::Clamp<int32>(Importance, 0, EMeshFeatureImportance::Highest);
	}

	if (FParse::Value(*Entry, TEXT("ShadingImportance="), Importance))
	{
		Settings.ShadingImportance = (EMeshFeatureImportance::Type)FMath::Clamp<int32>(Importance, 0, EMeshFeatureImportance::Highest);
	}

	float BasePercentTrianglesMult = 100.0f;
	if (FParse::Value(*Entry, TEXT("BasePercentTrianglesMult="), BasePercentTrianglesMult))
	{
		BasePercentTrianglesMult = FMath::Clamp<float>(BasePercentTrianglesMult, 0.0f, 100.0f);
		Group.BasePercentTrianglesMult = BasePercentTrianglesMult * 0.01f;
	}

	float LODPercentTrianglesMult = 100.0f;
	if (FParse::Value(*Entry, TEXT("LODPercentTrianglesMult="), LODPercentTrianglesMult))
	{
		LODPercentTrianglesMult = FMath::Clamp<float>(LODPercentTrianglesMult, 0.0f, 100.0f);
		Bias.PercentTriangles = LODPercentTrianglesMult * 0.01f;
	}

	if (FParse::Value(*Entry, TEXT("MaxDeviationBias="), Bias.MaxDeviation))
	{
		Bias.MaxDeviation = FMath::Clamp<float>(Bias.MaxDeviation, -1000.0f, 1000.0f);
	}

	if (FParse::Value(*Entry, TEXT("PixelErrorBias="), Bias.PixelError))
	{
		Bias.PixelError = FMath::Clamp<float>(Bias.PixelError, 1.0f, 1000.0f);
	}

	if (FParse::Value(*Entry, TEXT("WeldingThresholdBias="), Bias.WeldingThreshold))
	{
		Bias.WeldingThreshold = FMath::Clamp<float>(Bias.WeldingThreshold, -10.0f, 10.0f);
	}

	if (FParse::Value(*Entry, TEXT("HardAngleThresholdBias="), Bias.HardAngleThreshold))
	{
		Bias.HardAngleThreshold = FMath::Clamp<float>(Bias.HardAngleThreshold, -180.0f, 180.0f);
	}

	if (FParse::Value(*Entry, TEXT("SilhouetteImportanceBias="), Importance))
	{
		Bias.SilhouetteImportance = (EMeshFeatureImportance::Type)FMath::Clamp<int32>(Importance, -EMeshFeatureImportance::Highest, EMeshFeatureImportance::Highest);
	}

	if (FParse::Value(*Entry, TEXT("TextureImportanceBias="), Importance))
	{
		Bias.TextureImportance = (EMeshFeatureImportance::Type)FMath::Clamp<int32>(Importance, -EMeshFeatureImportance::Highest, EMeshFeatureImportance::Highest);
	}

	if (FParse::Value(*Entry, TEXT("ShadingImportanceBias="), Importance))
	{
		Bias.ShadingImportance = (EMeshFeatureImportance::Type)FMath::Clamp<int32>(Importance, -EMeshFeatureImportance::Highest, EMeshFeatureImportance::Highest);
	}
}

void FStaticMeshLODSettings::GetLODGroupNames(TArray<FName>& OutNames) const
{
	for (TMap<FName,FStaticMeshLODGroup>::TConstIterator It(Groups); It; ++It)
	{
		OutNames.Add(It.Key());
	}
}

void FStaticMeshLODSettings::GetLODGroupDisplayNames(TArray<FText>& OutDisplayNames) const
{
	for (TMap<FName,FStaticMeshLODGroup>::TConstIterator It(Groups); It; ++It)
	{
		OutDisplayNames.Add( It.Value().DisplayName );
	}
}

FMeshReductionSettings FStaticMeshLODGroup::GetSettings(const FMeshReductionSettings& InSettings, int32 LODIndex) const
{
	check(LODIndex >= 0 && LODIndex < MAX_STATIC_MESH_LODS);

	FMeshReductionSettings FinalSettings = InSettings;

	// PercentTriangles is actually a multiplier.
	float PercentTrianglesMult = (LODIndex == 0) ? BasePercentTrianglesMult : SettingsBias.PercentTriangles;
	FinalSettings.PercentTriangles = FMath::Clamp(InSettings.PercentTriangles * PercentTrianglesMult, 0.0f, 1.0f);

	// Bias the remaining settings.
	FinalSettings.MaxDeviation = FMath::Max(InSettings.MaxDeviation + SettingsBias.MaxDeviation, 0.0f);
	FinalSettings.PixelError = FMath::Max(InSettings.PixelError + SettingsBias.PixelError, 1.0f);
	FinalSettings.WeldingThreshold = FMath::Max(InSettings.WeldingThreshold + SettingsBias.WeldingThreshold, 0.0f);
	FinalSettings.HardAngleThreshold = FMath::Clamp(InSettings.HardAngleThreshold + SettingsBias.HardAngleThreshold, 0.0f, 180.0f);
	FinalSettings.SilhouetteImportance = (EMeshFeatureImportance::Type)FMath::Clamp<int32>(InSettings.SilhouetteImportance + SettingsBias.SilhouetteImportance, EMeshFeatureImportance::Off, EMeshFeatureImportance::Highest);
	FinalSettings.TextureImportance = (EMeshFeatureImportance::Type)FMath::Clamp<int32>(InSettings.TextureImportance + SettingsBias.TextureImportance, EMeshFeatureImportance::Off, EMeshFeatureImportance::Highest);
	FinalSettings.ShadingImportance = (EMeshFeatureImportance::Type)FMath::Clamp<int32>(InSettings.ShadingImportance + SettingsBias.ShadingImportance, EMeshFeatureImportance::Off, EMeshFeatureImportance::Highest);
	return FinalSettings;
}

void UStaticMesh::GetLODGroups(TArray<FName>& OutLODGroups)
{
	ITargetPlatform* RunningPlatform = GetTargetPlatformManagerRef().GetRunningTargetPlatform();
	check(RunningPlatform);
	RunningPlatform->GetStaticMeshLODSettings().GetLODGroupNames(OutLODGroups);
}

void UStaticMesh::GetLODGroupsDisplayNames(TArray<FText>& OutLODGroupsDisplayNames)
{
	ITargetPlatform* RunningPlatform = GetTargetPlatformManagerRef().GetRunningTargetPlatform();
	check(RunningPlatform);
	RunningPlatform->GetStaticMeshLODSettings().GetLODGroupDisplayNames(OutLODGroupsDisplayNames);
}

bool UStaticMesh::IsReductionActive(int32 LODIndex) const
{
	FMeshReductionSettings ReductionSettings = GetReductionSettings(LODIndex);
	IMeshReduction* ReductionModule = FModuleManager::Get().LoadModuleChecked<IMeshReductionManagerModule>("MeshReductionInterface").GetStaticMeshReductionInterface();
	return ReductionModule->IsReductionActive(ReductionSettings);
}

FMeshReductionSettings UStaticMesh::GetReductionSettings(int32 LODIndex) const
{
	check(IsSourceModelValid(LODIndex));
	//Retrieve the reduction settings, make sure we use the LODGroup if the Group is valid
	ITargetPlatformManagerModule& TargetPlatformManager = GetTargetPlatformManagerRef();
	ITargetPlatform* RunningPlatform = TargetPlatformManager.GetRunningTargetPlatform();
	check(RunningPlatform);
	const FStaticMeshLODSettings& LODSettings = RunningPlatform->GetStaticMeshLODSettings();
	const FStaticMeshLODGroup& SMLODGroup = LODSettings.GetLODGroup(LODGroup);
	const FStaticMeshSourceModel& SrcModel = GetSourceModel(LODIndex);
	return SMLODGroup.GetSettings(SrcModel.ReductionSettings, LODIndex);
}


void UStaticMesh::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);

	if (!bDuplicateForPIE)
	{
		SetLightingGuid();
	}
}

static void SerializeReductionSettingsForDDC(FArchive& Ar, FMeshReductionSettings& ReductionSettings)
{
	// Note: this serializer is only used to build the mesh DDC key, no versioning is required
	Ar << ReductionSettings.TerminationCriterion;
	Ar << ReductionSettings.PercentTriangles;
	Ar << ReductionSettings.PercentVertices;
	Ar << ReductionSettings.MaxDeviation;
	Ar << ReductionSettings.PixelError;
	Ar << ReductionSettings.WeldingThreshold;
	Ar << ReductionSettings.HardAngleThreshold;
	Ar << ReductionSettings.SilhouetteImportance;
	Ar << ReductionSettings.TextureImportance;
	Ar << ReductionSettings.ShadingImportance;
	Ar << ReductionSettings.BaseLODModel;
	FArchive_Serialize_BitfieldBool(Ar, ReductionSettings.bRecalculateNormals);
}

static void SerializeBuildSettingsForDDC(FArchive& Ar, FMeshBuildSettings& BuildSettings)
{
	// Note: this serializer is only used to build the mesh DDC key, no versioning is required
	FArchive_Serialize_BitfieldBool(Ar, BuildSettings.bRecomputeNormals);
	FArchive_Serialize_BitfieldBool(Ar, BuildSettings.bRecomputeTangents);
	FArchive_Serialize_BitfieldBool(Ar, BuildSettings.bUseMikkTSpace);
	FArchive_Serialize_BitfieldBool(Ar, BuildSettings.bComputeWeightedNormals);
	FArchive_Serialize_BitfieldBool(Ar, BuildSettings.bRemoveDegenerates);
	FArchive_Serialize_BitfieldBool(Ar, BuildSettings.bBuildAdjacencyBuffer);
	FArchive_Serialize_BitfieldBool(Ar, BuildSettings.bBuildReversedIndexBuffer);
	FArchive_Serialize_BitfieldBool(Ar, BuildSettings.bUseHighPrecisionTangentBasis);
	FArchive_Serialize_BitfieldBool(Ar, BuildSettings.bUseFullPrecisionUVs);
	FArchive_Serialize_BitfieldBool(Ar, BuildSettings.bGenerateLightmapUVs);

	Ar << BuildSettings.MinLightmapResolution;
	Ar << BuildSettings.SrcLightmapIndex;
	Ar << BuildSettings.DstLightmapIndex;

	if (Ar.IsLoading() && Ar.UE4Ver() < VER_UE4_BUILD_SCALE_VECTOR)
	{
		float BuildScale(1.0f);
		Ar << BuildScale;
		BuildSettings.BuildScale3D = FVector( BuildScale );
	}
	else
	{
		Ar << BuildSettings.BuildScale3D;
	}
	
	Ar << BuildSettings.DistanceFieldResolutionScale;
	FArchive_Serialize_BitfieldBool(Ar, BuildSettings.bGenerateDistanceFieldAsIfTwoSided);

	FString ReplacementMeshName = BuildSettings.DistanceFieldReplacementMesh->GetPathName();
	Ar << ReplacementMeshName;
}

// If static mesh derived data needs to be rebuilt (new format, serialization
// differences, etc.) replace the version GUID below with a new one.
// In case of merge conflicts with DDC versions, you MUST generate a new GUID
// and set this new GUID as the version.                                       
#define STATICMESH_DERIVEDDATA_VER TEXT("D819AE82DB6A4CE0891F68BD81CFC2A8")

static const FString& GetStaticMeshDerivedDataVersion()
{
	static FString CachedVersionString;
	if (CachedVersionString.IsEmpty())
	{
		// Static mesh versioning is controlled by the version reported by the mesh utilities module.
		IMeshUtilities& MeshUtilities = FModuleManager::Get().LoadModuleChecked<IMeshUtilities>(TEXT("MeshUtilities"));
		CachedVersionString = FString::Printf(TEXT("%s_%s"),
			STATICMESH_DERIVEDDATA_VER,
			*MeshUtilities.GetVersionString()
			);
	}
	return CachedVersionString;
}

class FStaticMeshStatusMessageContext : public FScopedSlowTask
{
public:
	explicit FStaticMeshStatusMessageContext(const FText& InMessage)
		: FScopedSlowTask(0, InMessage)
	{
		UE_LOG(LogStaticMesh,Log,TEXT("%s"),*InMessage.ToString());
		MakeDialog();
	}
};

namespace StaticMeshDerivedDataTimings
{
	int64 GetCycles = 0;
	int64 BuildCycles = 0;
	int64 ConvertCycles = 0;

	static void DumpTimings()
	{
		UE_LOG(LogStaticMesh,Log,TEXT("Derived Data Times: Get=%.3fs Build=%.3fs ConvertLegacy=%.3fs"),
			FPlatformTime::ToSeconds(GetCycles),
			FPlatformTime::ToSeconds(BuildCycles),
			FPlatformTime::ToSeconds(ConvertCycles)
			);
	}

	static FAutoConsoleCommand DumpTimingsCmd(
		TEXT("sm.DerivedDataTimings"),
		TEXT("Dumps derived data timings to the log."),
		FConsoleCommandDelegate::CreateStatic(DumpTimings)
		);
}

static FString BuildStaticMeshDerivedDataKeySuffix(UStaticMesh* Mesh, const FStaticMeshLODGroup& LODGroup)
{
	FString KeySuffix(TEXT(""));
	TArray<uint8> TempBytes;
	TempBytes.Reserve(64);

	// Add LightmapUVVersion to key going forward
	if ( (ELightmapUVVersion)Mesh->LightmapUVVersion > ELightmapUVVersion::BitByBit )
	{
		KeySuffix += LexToString(Mesh->LightmapUVVersion);
	}
#if WITH_EDITOR
	if (GIsAutomationTesting && Mesh->BuildCacheAutomationTestGuid.IsValid())
	{
		//If we are in automation testing and the BuildCacheAutomationTestGuid was set
		KeySuffix += Mesh->BuildCacheAutomationTestGuid.ToString(EGuidFormats::Digits);
	}
#endif

	int32 NumLODs = Mesh->GetNumSourceModels();
	for (int32 LODIndex = 0; LODIndex < NumLODs; ++LODIndex)
	{
		FStaticMeshSourceModel& SrcModel = Mesh->GetSourceModel(LODIndex);
		
		if (SrcModel.MeshDescriptionBulkData.IsValid())
		{
			KeySuffix += "MD";
			KeySuffix += SrcModel.MeshDescriptionBulkData->GetIdString();
		}
		else if (!SrcModel.RawMeshBulkData->IsEmpty())
		{
			// Legacy path for old assets
			KeySuffix += SrcModel.RawMeshBulkData->GetIdString();
		}
		else
		{
			// If neither mesh description nor raw mesh bulk data are valid, this is a generated LOD
			KeySuffix += "_";
		}

		// Serialize the build and reduction settings into a temporary array. The archive
		// is flagged as persistent so that machines of different endianness produce
		// identical binary results.
		TempBytes.Reset();
		FMemoryWriter Ar(TempBytes, /*bIsPersistent=*/ true);
		SerializeBuildSettingsForDDC(Ar, SrcModel.BuildSettings);

		ANSICHAR Flag[2] = { (SrcModel.BuildSettings.bUseFullPrecisionUVs || !GVertexElementTypeSupport.IsSupported(VET_Half2)) ? '1' : '0', '\0' };
		Ar.Serialize(Flag, 1);

		FMeshReductionSettings FinalReductionSettings = LODGroup.GetSettings(SrcModel.ReductionSettings, LODIndex);
		SerializeReductionSettingsForDDC(Ar, FinalReductionSettings);


		// Now convert the raw bytes to a string.
		const uint8* SettingsAsBytes = TempBytes.GetData();
		KeySuffix.Reserve(KeySuffix.Len() + TempBytes.Num() + 1);
		for (int32 ByteIndex = 0; ByteIndex < TempBytes.Num(); ++ByteIndex)
		{
			ByteToHex(SettingsAsBytes[ByteIndex], KeySuffix);
		}
	}

	// Mesh LOD streaming settings that need to trigger recache when changed
	const ITargetPlatform* RunningPlatform = GetTargetPlatformManagerRef().GetRunningTargetPlatform();
	check(RunningPlatform);
	const bool bAllowLODStreaming = RunningPlatform->SupportsFeature(ETargetPlatformFeatures::MeshLODStreaming) && LODGroup.IsLODStreamingSupported();
	KeySuffix += bAllowLODStreaming ? TEXT("LS1") : TEXT("LS0");
	KeySuffix += TEXT("MNS");
	if (bAllowLODStreaming)
	{
		int32 MaxNumStreamedLODs = Mesh->NumStreamedLODs.GetValueForPlatformIdentifiers(
				RunningPlatform->GetPlatformInfo().PlatformGroupName,
				RunningPlatform->GetPlatformInfo().VanillaPlatformName);
		if (MaxNumStreamedLODs < 0)
		{
			MaxNumStreamedLODs = LODGroup.GetDefaultMaxNumStreamedLODs();
		}
		for (int32 Idx = 0; Idx < 4; ++Idx)
		{
			ByteToHex((MaxNumStreamedLODs & 0xff000000) >> 24, KeySuffix);
			MaxNumStreamedLODs <<= 8;
		}
	}
	else
	{
		KeySuffix += TEXT("zzzzzzzz");
	}

	KeySuffix.AppendChar(Mesh->bSupportUniformlyDistributedSampling ? TEXT('1') : TEXT('0'));

	// Value of this CVar affects index buffer <-> painted vertex color correspondence (see UE-51421).
	static const TConsoleVariableData<int32>* CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.TriangleOrderOptimization"));

	// depending on module loading order this might be called too early on Linux (possibly other platforms too?)
	if (CVar == nullptr)
	{
		FModuleManager::Get().LoadModuleChecked<IMeshUtilities>(TEXT("MeshUtilities"));
		CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.TriangleOrderOptimization"));
	}

	if (CVar)
	{
		switch (CVar->GetValueOnAnyThread())
		{
			case 2:
				KeySuffix += TEXT("_NoTOO");
				break;
			case 0:
				KeySuffix += TEXT("_NVTS");
				break;
			case 1:
				// intentional - default value will not influence DDC to avoid unnecessary invalidation
				break;
			default:
				KeySuffix += FString::Printf(TEXT("_TOO%d"), CVar->GetValueOnAnyThread());	//	 allow unknown values transparently
				break;
		}
	}
	return KeySuffix;
}

static FString BuildStaticMeshDerivedDataKey(const FString& KeySuffix)
{
	return FDerivedDataCacheInterface::BuildCacheKey(
		TEXT("STATICMESH"),
		*GetStaticMeshDerivedDataVersion(),
		*KeySuffix);
}

static FString BuildStaticMeshLODDerivedDataKey(const FString& KeySuffix, int32 LODIdx)
{
	return FDerivedDataCacheInterface::BuildCacheKey(
		TEXT("STATICMESH"),
		*GetStaticMeshDerivedDataVersion(),
		*FString::Printf(TEXT("%s_LOD%d"), *KeySuffix, LODIdx));
}

void FStaticMeshRenderData::ComputeUVDensities()
{
#if WITH_EDITORONLY_DATA
	for (FStaticMeshLODResources& LODModel : LODResources)
	{
		const int32 NumTexCoords = FMath::Min<int32>(LODModel.GetNumTexCoords(), MAX_STATIC_TEXCOORDS);

		for (FStaticMeshSection& SectionInfo : LODModel.Sections)
		{
			FMemory::Memzero(SectionInfo.UVDensities);
			FMemory::Memzero(SectionInfo.Weights);

			FUVDensityAccumulator UVDensityAccs[MAX_STATIC_TEXCOORDS];
			for (int32 UVIndex = 0; UVIndex < NumTexCoords; ++UVIndex)
			{
				UVDensityAccs[UVIndex].Reserve(SectionInfo.NumTriangles);
			}

			FIndexArrayView IndexBuffer = LODModel.IndexBuffer.GetArrayView();

			for (uint32 TriangleIndex = 0; TriangleIndex < SectionInfo.NumTriangles; ++TriangleIndex)
			{
				const int32 Index0 = IndexBuffer[SectionInfo.FirstIndex + TriangleIndex * 3 + 0];
				const int32 Index1 = IndexBuffer[SectionInfo.FirstIndex + TriangleIndex * 3 + 1];
				const int32 Index2 = IndexBuffer[SectionInfo.FirstIndex + TriangleIndex * 3 + 2];

				const float Aera = FUVDensityAccumulator::GetTriangleAera(
										LODModel.VertexBuffers.PositionVertexBuffer.VertexPosition(Index0), 
										LODModel.VertexBuffers.PositionVertexBuffer.VertexPosition(Index1), 
										LODModel.VertexBuffers.PositionVertexBuffer.VertexPosition(Index2));

				if (Aera > SMALL_NUMBER)
				{
					for (int32 UVIndex = 0; UVIndex < NumTexCoords; ++UVIndex)
					{
						const float UVAera = FUVDensityAccumulator::GetUVChannelAera(
												LODModel.VertexBuffers.StaticMeshVertexBuffer.GetVertexUV(Index0, UVIndex), 
												LODModel.VertexBuffers.StaticMeshVertexBuffer.GetVertexUV(Index1, UVIndex), 
												LODModel.VertexBuffers.StaticMeshVertexBuffer.GetVertexUV(Index2, UVIndex));

						UVDensityAccs[UVIndex].PushTriangle(Aera, UVAera);
					}
				}
			}

			for (int32 UVIndex = 0; UVIndex < NumTexCoords; ++UVIndex)
			{
				float WeightedUVDensity = 0;
				float Weight = 0;
				UVDensityAccs[UVIndex].AccumulateDensity(WeightedUVDensity, Weight);

				if (Weight > SMALL_NUMBER)
				{
					SectionInfo.UVDensities[UVIndex] = WeightedUVDensity / Weight;
					SectionInfo.Weights[UVIndex] = Weight;
				}
			}
		}
	}
#endif // WITH_EDITORONLY_DATA
}

void FStaticMeshRenderData::BuildAreaWeighedSamplingData()
{
	for (FStaticMeshLODResources& LODModel : LODResources)
	{
		for (FStaticMeshSection& SectionInfo : LODModel.Sections)
		{
			LODModel.AreaWeightedSectionSamplers.SetNum(LODModel.Sections.Num());
			for (int32 i = 0; i < LODModel.Sections.Num(); ++i)
			{
				LODModel.AreaWeightedSectionSamplers[i].Init(&LODModel, i);
			}
			LODModel.AreaWeightedSampler.Init(&LODModel);
		}
	}
}

void FStaticMeshRenderData::Cache(UStaticMesh* Owner, const FStaticMeshLODSettings& LODSettings)
{
	if (Owner->GetOutermost()->HasAnyPackageFlags(PKG_FilterEditorOnly))
	{
		// Don't cache for cooked packages
		return;
	}


	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FStaticMeshRenderData::Cache);

		COOK_STAT(auto Timer = StaticMeshCookStats::UsageStats.TimeSyncWork());
		int32 T0 = FPlatformTime::Cycles();
		int32 NumLODs = Owner->GetNumSourceModels();
		const FStaticMeshLODGroup& LODGroup = LODSettings.GetLODGroup(Owner->LODGroup);
		const FString KeySuffix = BuildStaticMeshDerivedDataKeySuffix(Owner, LODGroup);
		DerivedDataKey = BuildStaticMeshDerivedDataKey(KeySuffix);

		TArray<uint8> DerivedData;
		if (GetDerivedDataCacheRef().GetSynchronous(*DerivedDataKey, DerivedData))
		{
			COOK_STAT(Timer.AddHit(DerivedData.Num()));
			FMemoryReader Ar(DerivedData, /*bIsPersistent=*/ true);
			Serialize(Ar, Owner, /*bCooked=*/ false);

			for (int32 LODIdx = 0; LODIdx < LODResources.Num(); ++LODIdx)
			{
				FStaticMeshLODResources& LODResource = LODResources[LODIdx];
				if (LODResource.bBuffersInlined)
				{
					break;
				}
				// TODO: can we postpone the loading to streaming time?
				LODResource.DerivedDataKey = BuildStaticMeshLODDerivedDataKey(KeySuffix, LODIdx);
				typename FStaticMeshLODResources::FStaticMeshBuffersSize DummyBuffersSize;
				LODResource.SerializeBuffers(Ar, Owner, 0, DummyBuffersSize);
				typename FStaticMeshLODResources::FStaticMeshBuffersSize LODBuffersSize;
				Ar << LODBuffersSize;
				LODResource.BuffersSize = LODBuffersSize.CalcBuffersSize();
				check(LODResource.BuffersSize == DummyBuffersSize.CalcBuffersSize());
			}

			int32 T1 = FPlatformTime::Cycles();
			UE_LOG(LogStaticMesh,Verbose,TEXT("Static mesh found in DDC [%fms] %s"),
				FPlatformTime::ToMilliseconds(T1-T0),
				*Owner->GetPathName()
				);
			FPlatformAtomics::InterlockedAdd(&StaticMeshDerivedDataTimings::GetCycles, T1 - T0);
		}
		else
		{
			FFormatNamedArguments Args;
			Args.Add(TEXT("StaticMeshName"), FText::FromString( Owner->GetName() ) );
			FStaticMeshStatusMessageContext StatusContext( FText::Format( NSLOCTEXT("Engine", "BuildingStaticMeshStatus", "Building static mesh {StaticMeshName}..."), Args ) );

			checkf(Owner->IsMeshDescriptionValid(0), TEXT("Bad MeshDescription on %s"), *GetPathNameSafe(Owner));

			IMeshBuilderModule& MeshBuilderModule = FModuleManager::Get().LoadModuleChecked<IMeshBuilderModule>(TEXT("MeshBuilder"));
			if (!MeshBuilderModule.BuildMesh(*this, Owner, LODGroup))
			{
				UE_LOG(LogStaticMesh, Error, TEXT("Failed to build static mesh. See previous line(s) for details."));
				return;
			}

			ComputeUVDensities();
			if(Owner->bSupportUniformlyDistributedSampling)
			{
				BuildAreaWeighedSamplingData();
			}
			bLODsShareStaticLighting = Owner->CanLODsShareStaticLighting();
			FMemoryWriter Ar(DerivedData, /*bIsPersistent=*/ true);
			Serialize(Ar, Owner, /*bCooked=*/ false);

			for (int32 LODIdx = 0; LODIdx < LODResources.Num(); ++LODIdx)
			{
				FStaticMeshLODResources& LODResource = LODResources[LODIdx];
				if (LODResource.bBuffersInlined)
				{
					break;
				}
				typename FStaticMeshLODResources::FStaticMeshBuffersSize LODBuffersSize;
				const uint8 LODStripFlags = FStaticMeshLODResources::GenerateClassStripFlags(Ar, Owner, LODIdx);
				LODResource.SerializeBuffers(Ar, Owner, LODStripFlags, LODBuffersSize);
				Ar << LODBuffersSize;
				LODResource.DerivedDataKey = BuildStaticMeshLODDerivedDataKey(KeySuffix, LODIdx);
				// TODO: Save non-inlined LODs separately
			}

			bool bSaveDDC = true;
#if WITH_EDITOR
			//Do not save ddc when we are forcing the regeneration of ddc in automation test
			//No need to take more space in the ddc.
			if (GIsAutomationTesting && Owner->BuildCacheAutomationTestGuid.IsValid())
			{
				bSaveDDC = false;
			}
#endif
			if (bSaveDDC)
			{
				GetDerivedDataCacheRef().Put(*DerivedDataKey, DerivedData);
			}

			int32 T1 = FPlatformTime::Cycles();
			UE_LOG(LogStaticMesh,Log,TEXT("Built static mesh [%.2fs] %s"),
				FPlatformTime::ToMilliseconds(T1-T0) / 1000.0f,
				*Owner->GetPathName()
				);
			FPlatformAtomics::InterlockedAdd(&StaticMeshDerivedDataTimings::BuildCycles, T1 - T0);
			COOK_STAT(Timer.AddMiss(DerivedData.Num()));
		}
	}

	static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.GenerateMeshDistanceFields"));

	if (CVar->GetValueOnAnyThread(true) != 0 || Owner->bGenerateMeshDistanceField)
	{
		FString DistanceFieldKey = BuildDistanceFieldDerivedDataKey(DerivedDataKey);
		if (LODResources.IsValidIndex(0))
		{
			if (!LODResources[0].DistanceFieldData)
			{
				LODResources[0].DistanceFieldData = new FDistanceFieldVolumeData();
			}

			const FMeshBuildSettings& BuildSettings = Owner->GetSourceModel(0).BuildSettings;
			UStaticMesh* MeshToGenerateFrom = BuildSettings.DistanceFieldReplacementMesh ? BuildSettings.DistanceFieldReplacementMesh : Owner;

			if (BuildSettings.DistanceFieldReplacementMesh)
			{
				// Make sure dependency is postloaded
				BuildSettings.DistanceFieldReplacementMesh->ConditionalPostLoad();
			}

			LODResources[0].DistanceFieldData->CacheDerivedData(DistanceFieldKey, Owner, MeshToGenerateFrom, BuildSettings.DistanceFieldResolutionScale, BuildSettings.bGenerateDistanceFieldAsIfTwoSided);
		}
		else
		{
			UE_LOG(LogStaticMesh, Error, TEXT("Failed to generate distance field data for %s due to missing LODResource for LOD 0."), *Owner->GetPathName());
		}
	}
}
#endif // #if WITH_EDITOR

FArchive& operator<<(FArchive& Ar, FStaticMaterial& Elem)
{
	Ar << Elem.MaterialInterface;

	Ar << Elem.MaterialSlotName;
#if WITH_EDITORONLY_DATA
	if((!Ar.IsCooking() && !Ar.IsFilterEditorOnly()) || (Ar.IsCooking() && Ar.CookingTarget()->HasEditorOnlyData()))
	{
		Ar << Elem.ImportedMaterialSlotName;
	}
#endif //#if WITH_EDITORONLY_DATA

	if (!Ar.IsLoading() || Ar.CustomVer(FRenderingObjectVersion::GUID) >= FRenderingObjectVersion::TextureStreamingMeshUVChannelData)
	{
		Ar << Elem.UVChannelData;
	}
	
	return Ar;
}

bool operator== (const FStaticMaterial& LHS, const FStaticMaterial& RHS)
{
	return (LHS.MaterialInterface == RHS.MaterialInterface &&
		LHS.MaterialSlotName == RHS.MaterialSlotName
#if WITH_EDITORONLY_DATA
		&& LHS.ImportedMaterialSlotName == RHS.ImportedMaterialSlotName
#endif
		);
}

bool operator== (const FStaticMaterial& LHS, const UMaterialInterface& RHS)
{
	return (LHS.MaterialInterface == &RHS);
}

bool operator== (const UMaterialInterface& LHS, const FStaticMaterial& RHS)
{
	return (RHS.MaterialInterface == &LHS);
}

/*-----------------------------------------------------------------------------
UStaticMesh
-----------------------------------------------------------------------------*/

#if WITH_EDITORONLY_DATA
const float UStaticMesh::MinimumAutoLODPixelError = SMALL_NUMBER;
#endif	//#if WITH_EDITORONLY_DATA

UStaticMesh::UStaticMesh(const FObjectInitializer& ObjectInitializer)
	: UStreamableRenderAsset(ObjectInitializer)
{
	ElementToIgnoreForTexFactor = -1;
	bHasNavigationData=true;
#if WITH_EDITORONLY_DATA
	bAutoComputeLODScreenSize=true;
	ImportVersion = EImportStaticMeshVersion::BeforeImportStaticMeshVersionWasAdded;
	LODForOccluderMesh = -1;
	NumStreamedLODs.Default = -1;
#endif // #if WITH_EDITORONLY_DATA
	LightMapResolution = 4;
	LpvBiasMultiplier = 1.0f;
	MinLOD.Default = 0;

	bSupportUniformlyDistributedSampling = false;
	bIsBuiltAtRuntime = false;
	bRenderingResourcesInitialized = false;
#if WITH_EDITOR
	BuildCacheAutomationTestGuid.Invalidate();
#endif
}

void UStaticMesh::PostInitProperties()
{
#if WITH_EDITORONLY_DATA
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		AssetImportData = NewObject<UAssetImportData>(this, TEXT("AssetImportData"));
	}
#endif
	Super::PostInitProperties();
}

/**
 * Initializes the static mesh's render resources.
 */
void UStaticMesh::InitResources()
{
	LLM_SCOPE(ELLMTag::StaticMesh);

	bRenderingResourcesInitialized = true;

	UpdateUVChannelData(false);

	if (RenderData)
	{
		UWorld* World = GetWorld();
		RenderData->InitResources(World ? World->FeatureLevel.GetValue() : ERHIFeatureLevel::Num, this);
	}

	if (OccluderData)
	{
		INC_DWORD_STAT_BY( STAT_StaticMeshOccluderMemory, OccluderData->GetResourceSizeBytes() );
	}

	// Determine whether or not this mesh can be streamed.
	const int32 NumLODs = GetNumLODs();
	bIsStreamable = !NeverStream
		&& NumLODs > 1
		&& !RenderData->LODResources[0].bBuffersInlined;
		//&& !bTemporarilyDisableStreaming;

#if (WITH_EDITOR && DO_CHECK)
	if (bIsStreamable && !GetOutermost()->bIsCookedForEditor)
	{
		for (int32 LODIdx = 0; LODIdx < NumLODs; ++LODIdx)
		{
			const FStaticMeshLODResources& LODResource = RenderData->LODResources[LODIdx];
			check(LODResource.bBuffersInlined || !LODResource.DerivedDataKey.IsEmpty());
		}
	}
#endif

	UnlinkStreaming();
	if (bIsStreamable)
	{
		LinkStreaming();
	}

#if	STATS
	UStaticMesh* This = this;
	ENQUEUE_RENDER_COMMAND(UpdateMemoryStats)(
		[This](FRHICommandList& RHICmdList)
		{
			const uint32 StaticMeshResourceSize = This->GetResourceSizeBytes( EResourceSizeMode::Exclusive );
			INC_DWORD_STAT_BY( STAT_StaticMeshTotalMemory, StaticMeshResourceSize );
			INC_DWORD_STAT_BY( STAT_StaticMeshTotalMemory2, StaticMeshResourceSize );
		} );
#endif // STATS
}

void UStaticMesh::GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize)
{
	Super::GetResourceSizeEx(CumulativeResourceSize);

	if (RenderData)
	{
		RenderData->GetResourceSizeEx(CumulativeResourceSize);
	}

	if (OccluderData)
	{
		CumulativeResourceSize.AddDedicatedSystemMemoryBytes(OccluderData->GetResourceSizeBytes());
	}
}

void FStaticMeshRenderData::GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize) const
{
	CumulativeResourceSize.AddDedicatedSystemMemoryBytes(sizeof(*this));

	// Count dynamic arrays.
	CumulativeResourceSize.AddUnknownMemoryBytes(LODResources.GetAllocatedSize());

	for(int32 LODIndex = 0;LODIndex < LODResources.Num();LODIndex++)
	{
		const FStaticMeshLODResources& LODRenderData = LODResources[LODIndex];

		const int32 VBSize = LODRenderData.VertexBuffers.StaticMeshVertexBuffer.GetResourceSize() +
			LODRenderData.VertexBuffers.PositionVertexBuffer.GetStride()			* LODRenderData.VertexBuffers.PositionVertexBuffer.GetNumVertices() +
			LODRenderData.VertexBuffers.ColorVertexBuffer.GetStride()				* LODRenderData.VertexBuffers.ColorVertexBuffer.GetNumVertices();
		
		int32 NumIndicies = LODRenderData.IndexBuffer.GetNumIndices();

		if (LODRenderData.AdditionalIndexBuffers)
		{
			NumIndicies += LODRenderData.AdditionalIndexBuffers->ReversedDepthOnlyIndexBuffer.GetNumIndices();
			NumIndicies += LODRenderData.AdditionalIndexBuffers->ReversedIndexBuffer.GetNumIndices();
			NumIndicies += LODRenderData.AdditionalIndexBuffers->WireframeIndexBuffer.GetNumIndices();
			NumIndicies += (RHISupportsTessellation(GShaderPlatformForFeatureLevel[GMaxRHIFeatureLevel]) ? LODRenderData.AdditionalIndexBuffers->AdjacencyIndexBuffer.GetNumIndices() : 0);
		}

		int32 IBSize = NumIndicies * (LODRenderData.IndexBuffer.Is32Bit() ? 4 : 2);

		CumulativeResourceSize.AddUnknownMemoryBytes(VBSize + IBSize);
		CumulativeResourceSize.AddUnknownMemoryBytes(LODRenderData.Sections.GetAllocatedSize());

		if (LODRenderData.DistanceFieldData)
		{
			LODRenderData.DistanceFieldData->GetResourceSizeEx(CumulativeResourceSize);
		}
	}

#if WITH_EDITORONLY_DATA
	// If render data for multiple platforms is loaded, count it all.
	if (NextCachedRenderData)
	{
		NextCachedRenderData->GetResourceSizeEx(CumulativeResourceSize);
	}
#endif // #if WITH_EDITORONLY_DATA
}

int32 UStaticMesh::GetNumVertices(int32 LODIndex) const
{
	int32 NumVertices = 0;
	if (RenderData && RenderData->LODResources.IsValidIndex(LODIndex))
	{
		NumVertices = RenderData->LODResources[LODIndex].VertexBuffers.StaticMeshVertexBuffer.GetNumVertices();
	}
	return NumVertices;
}

int32 UStaticMesh::GetNumLODs() const
{
	int32 NumLODs = 0;
	if (RenderData)
	{
		NumLODs = RenderData->LODResources.Num();
	}
	return NumLODs;
}

// pass false for bCheckLODForVerts for any runtime code that can handle empty LODs, for example due to them being stripped
//  as a result of minimum LOD setup on the static mesh; in cooked builds, those verts are stripped, but systems still need to
//  be able to handle these cases; to check specifically for an LOD, pass true (default arg), and an LOD index (default arg implies MinLOD)
//
bool UStaticMesh::HasValidRenderData(bool bCheckLODForVerts, int32 LODIndex) const
{
	if (RenderData != nullptr
		&& RenderData->LODResources.Num() > 0
		&& RenderData->LODResources.GetData() != nullptr)
	{
		if (bCheckLODForVerts)
		{
		    if (LODIndex == INDEX_NONE)
		    {
			    LODIndex = FMath::Clamp<int32>(MinLOD.GetValueForFeatureLevel(GMaxRHIFeatureLevel), 0, RenderData->LODResources.Num() - 1);
		    }
			return (RenderData->LODResources[LODIndex].VertexBuffers.StaticMeshVertexBuffer.GetNumVertices() > 0);
		}
		else
		{
			return true;
		}
	}
	return false;
}

FBoxSphereBounds UStaticMesh::GetBounds() const
{
	return ExtendedBounds;
}

FBox UStaticMesh::GetBoundingBox() const
{
	return ExtendedBounds.GetBox();
}

int32 UStaticMesh::GetNumSections(int32 InLOD) const
{
	int32 NumSections = 0;
	if (RenderData != NULL && RenderData->LODResources.IsValidIndex(InLOD))
	{
		const FStaticMeshLODResources& LOD = RenderData->LODResources[InLOD];
		NumSections = LOD.Sections.Num();
	}
	return NumSections;
}

#if WITH_EDITORONLY_DATA
static float GetUVDensity(const TIndirectArray<FStaticMeshLODResources>& LODResources, int32 UVIndex)
{
	float WeightedUVDensity = 0;
	float WeightSum = 0;

	if (UVIndex < MAX_STATIC_TEXCOORDS)
	{
		// Parse all LOD-SECTION using this material index.
		for (const FStaticMeshLODResources& LODModel : LODResources)
		{
			if (UVIndex < LODModel.GetNumTexCoords())
			{
				for (const FStaticMeshSection& SectionInfo : LODModel.Sections)
				{
					WeightedUVDensity += SectionInfo.UVDensities[UVIndex] * SectionInfo.Weights[UVIndex];
					WeightSum += SectionInfo.Weights[UVIndex];
				}
			}
		}
	}

	return (WeightSum > SMALL_NUMBER) ? (WeightedUVDensity / WeightSum) : 0;
}
#endif

void UStaticMesh::UpdateUVChannelData(bool bRebuildAll)
{
#if WITH_EDITORONLY_DATA
	TRACE_CPUPROFILER_EVENT_SCOPE(UStaticMesh::UpdateUVChannelData);

	// Once cooked, the data required to compute the scales will not be CPU accessible.
	if (FPlatformProperties::HasEditorOnlyData() && RenderData)
	{
		bool bDensityChanged = false;

		for (int32 MaterialIndex = 0; MaterialIndex < StaticMaterials.Num(); ++MaterialIndex)
		{
			FMeshUVChannelInfo& UVChannelData = StaticMaterials[MaterialIndex].UVChannelData;

			// Skip it if we want to keep it.
			if (UVChannelData.bInitialized && (!bRebuildAll || UVChannelData.bOverrideDensities))
			{
				continue;
			}

			float WeightedUVDensities[TEXSTREAM_MAX_NUM_UVCHANNELS] = {0, 0, 0, 0};
			float Weights[TEXSTREAM_MAX_NUM_UVCHANNELS] = {0, 0, 0, 0};

			// Parse all LOD-SECTION using this material index.
			for (const FStaticMeshLODResources& LODModel : RenderData->LODResources)
			{
				const int32 NumTexCoords = FMath::Min<int32>(LODModel.GetNumTexCoords(), TEXSTREAM_MAX_NUM_UVCHANNELS);
				for (const FStaticMeshSection& SectionInfo : LODModel.Sections)
				{
					if (SectionInfo.MaterialIndex == MaterialIndex)
					{
						for (int32 UVIndex = 0; UVIndex < NumTexCoords; ++UVIndex)
						{
							WeightedUVDensities[UVIndex] += SectionInfo.UVDensities[UVIndex] * SectionInfo.Weights[UVIndex];
							Weights[UVIndex] += SectionInfo.Weights[UVIndex];
						}

						// If anything needs to be updated, also update the lightmap densities.
						bDensityChanged = true;
					}
				}
			}

			UVChannelData.bInitialized = true;
			UVChannelData.bOverrideDensities = false;
			for (int32 UVIndex = 0; UVIndex < TEXSTREAM_MAX_NUM_UVCHANNELS; ++UVIndex)
			{
				UVChannelData.LocalUVDensities[UVIndex] = (Weights[UVIndex] > SMALL_NUMBER) ? (WeightedUVDensities[UVIndex] / Weights[UVIndex]) : 0;
			}
		}

		if (bDensityChanged || bRebuildAll)
		{
			LightmapUVDensity = GetUVDensity(RenderData->LODResources, LightMapCoordinateIndex);

			if (GEngine)
			{
				GEngine->TriggerStreamingDataRebuild();
			}
		}

		// Update the data for the renderthread debug viewmodes
		RenderData->SyncUVChannelData(StaticMaterials);
	}
#endif
}

#if WITH_EDITORONLY_DATA
static void AccumulateBounds(FBox& Bounds, const FStaticMeshLODResources& LODModel, const FStaticMeshSection& SectionInfo, const FTransform& Transform)
{
	const int32 SectionIndexCount = SectionInfo.NumTriangles * 3;
	FIndexArrayView IndexBuffer = LODModel.IndexBuffer.GetArrayView();

	FBox TransformedBox(ForceInit);
	for (uint32 TriangleIndex = 0; TriangleIndex < SectionInfo.NumTriangles; ++TriangleIndex)
	{
		const int32 Index0 = IndexBuffer[SectionInfo.FirstIndex + TriangleIndex * 3 + 0];
		const int32 Index1 = IndexBuffer[SectionInfo.FirstIndex + TriangleIndex * 3 + 1];
		const int32 Index2 = IndexBuffer[SectionInfo.FirstIndex + TriangleIndex * 3 + 2];

		FVector Pos1 = Transform.TransformPosition(LODModel.VertexBuffers.PositionVertexBuffer.VertexPosition(Index1));
		FVector Pos2 = Transform.TransformPosition(LODModel.VertexBuffers.PositionVertexBuffer.VertexPosition(Index2));
		FVector Pos0 = Transform.TransformPosition(LODModel.VertexBuffers.PositionVertexBuffer.VertexPosition(Index0));

		Bounds += Pos0;
		Bounds += Pos1;
		Bounds += Pos2;
	}
}
#endif

FBox UStaticMesh::GetMaterialBox(int32 MaterialIndex, const FTransform& Transform) const
{
#if WITH_EDITORONLY_DATA
	// Once cooked, the data requires to compute the scales will not be CPU accessible.
	if (FPlatformProperties::HasEditorOnlyData() && RenderData)
	{
		FBox MaterialBounds(ForceInit);
		for (const FStaticMeshLODResources& LODModel : RenderData->LODResources)
		{
			for (const FStaticMeshSection& SectionInfo : LODModel.Sections)
			{
				if (SectionInfo.MaterialIndex != MaterialIndex)
					continue;

				AccumulateBounds(MaterialBounds, LODModel, SectionInfo, Transform);
			}
		}
		return MaterialBounds;
	}
#endif
	// Fallback back using the full bounds.
	return GetBoundingBox().TransformBy(Transform);
}

const FMeshUVChannelInfo* UStaticMesh::GetUVChannelData(int32 MaterialIndex) const
{
	if (StaticMaterials.IsValidIndex(MaterialIndex))
	{
		ensure(StaticMaterials[MaterialIndex].UVChannelData.bInitialized);
		return &StaticMaterials[MaterialIndex].UVChannelData;
	}

	return nullptr;
}

/**
 * Releases the static mesh's render resources.
 */
void UStaticMesh::ReleaseResources()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UStaticMesh::ReleaseResources);
#if STATS
	uint32 StaticMeshResourceSize = GetResourceSizeBytes(EResourceSizeMode::Exclusive);
	DEC_DWORD_STAT_BY( STAT_StaticMeshTotalMemory, StaticMeshResourceSize );
	DEC_DWORD_STAT_BY( STAT_StaticMeshTotalMemory2, StaticMeshResourceSize );
#endif

	if (RenderData)
	{
		RenderData->ReleaseResources();
	}

	if (OccluderData)
	{
		DEC_DWORD_STAT_BY( STAT_StaticMeshOccluderMemory, OccluderData->GetResourceSizeBytes() );
	}
	
	// insert a fence to signal when these commands completed
	ReleaseResourcesFence.BeginFence();

	bRenderingResourcesInitialized = false;
}

#if WITH_EDITOR
void UStaticMesh::PreEditChange(FProperty* PropertyAboutToChange)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UStaticMesh::PreEditChange);

	Super::PreEditChange(PropertyAboutToChange);

	// Release the static mesh's resources.
	ReleaseResources();

	// Flush the resource release commands to the rendering thread to ensure that the edit change doesn't occur while a resource is still
	// allocated, and potentially accessing the UStaticMesh.
	ReleaseResourcesFence.Wait();
}

void UStaticMesh::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UStaticMesh::PostEditChangeProperty);

	FProperty* PropertyThatChanged = PropertyChangedEvent.Property;
	const FName PropertyName = PropertyThatChanged ? PropertyThatChanged->GetFName() : NAME_None;
	
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UStaticMesh, LODGroup))
	{
		// Force an update of LOD group settings

		// Dont rebuild inside here.  We're doing that below.
		bool bRebuild = false;
		SetLODGroup(LODGroup, bRebuild);
	}
#if WITH_EDITORONLY_DATA
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UStaticMesh, ComplexCollisionMesh) && ComplexCollisionMesh != this)
	{
		if (BodySetup)
		{
			BodySetup->InvalidatePhysicsData();
			BodySetup->CreatePhysicsMeshes();
		}
	}
#endif

	LightMapResolution = FMath::Max(LightMapResolution, 0);

	if (PropertyChangedEvent.MemberProperty 
		&& ((PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UStaticMesh, PositiveBoundsExtension)) 
			|| (PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UStaticMesh, NegativeBoundsExtension))))
	{
		// Update the extended bounds
		CalculateExtendedBounds();
	}

	if (!bAutoComputeLODScreenSize
		&& RenderData
		&& PropertyName == GET_MEMBER_NAME_CHECKED(UStaticMesh, bAutoComputeLODScreenSize))
	{
		for (int32 LODIndex = 1; LODIndex < GetNumSourceModels(); ++LODIndex)
		{
			GetSourceModel(LODIndex).ScreenSize = RenderData->ScreenSize[LODIndex];
		}
	}

	//Don't use the render data here because the property that just changed might be invalidating the current RenderData.
	EnforceLightmapRestrictions(/*bUseRenderData=*/false);

	// Following an undo or other operation which can change the SourceModels, ensure the StaticMeshOwner is up to date
	for (int32 Index = 0; Index < GetNumSourceModels(); ++Index)
	{
		GetSourceModel(Index).StaticMeshOwner = this;
	}

	Build(/*bSilent=*/ true);

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UStaticMesh, bHasNavigationData)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UStaticMesh, BodySetup))
	{
		// Build called above will result in creation, update or destruction 
		// of NavCollision. We need to let related StaticMeshComponents know
		BroadcastNavCollisionChange();
	}

	// Only unbuild lighting for properties which affect static lighting
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UStaticMesh, LightMapResolution)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UStaticMesh, LightMapCoordinateIndex))
	{
		FStaticMeshComponentRecreateRenderStateContext Context(this, true);		
		SetLightingGuid();
	}
	
	UpdateUVChannelData(true);

	for (UAssetUserData* Datum : AssetUserData)
	{
		if (Datum != nullptr)
		{
			Datum->PostEditChangeOwner();
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);

	OnMeshChanged.Broadcast();
}

void UStaticMesh::PostEditUndo()
{
	// Following an undo or other operation which can change the SourceModels, ensure the StaticMeshOwner is up to date
	for (int32 Index = 0; Index < GetNumSourceModels(); ++Index)
	{
		GetSourceModel(Index).StaticMeshOwner = this;
	}

	// The super will cause a Build() via PostEditChangeProperty().
	Super::PostEditUndo();
}

void UStaticMesh::SetLODGroup(FName NewGroup, bool bRebuildImmediately)
{
#if WITH_EDITORONLY_DATA
	const bool bBeforeDerivedDataCached = (RenderData == nullptr);
	if (!bBeforeDerivedDataCached)
	{
		Modify();
	}
	bool bResetSectionInfoMap = (LODGroup != NewGroup);
	LODGroup = NewGroup;
	if (NewGroup != NAME_None)
	{
		const ITargetPlatform* Platform = GetTargetPlatformManagerRef().GetRunningTargetPlatform();
		check(Platform);
		const FStaticMeshLODGroup& GroupSettings = Platform->GetStaticMeshLODSettings().GetLODGroup(NewGroup);

		// Set the number of LODs to at least the default. If there are already LODs they will be preserved, with default settings of the new LOD group.
		int32 DefaultLODCount = GroupSettings.GetDefaultNumLODs();

		SetNumSourceModels(DefaultLODCount);
		
		for (int32 LODIndex = 0; LODIndex < DefaultLODCount; ++LODIndex)
		{
			FStaticMeshSourceModel& SourceModel = GetSourceModel(LODIndex);

			// Set reduction settings to the defaults.
			SourceModel.ReductionSettings = GroupSettings.GetDefaultSettings(LODIndex);
			
			if (LODIndex != 0)
			{
			//Reset the section info map
			if (bResetSectionInfoMap)
			{
				for (int32 SectionIndex = 0; SectionIndex < GetSectionInfoMap().GetSectionNumber(LODIndex); ++SectionIndex)
				{
						GetSectionInfoMap().Remove(LODIndex, SectionIndex);
				}
			}
			//Clear the raw data if we change the LOD Group and we do not reduce ourself, this will force the user to do a import LOD which will manage the section info map properly
			if (!SourceModel.IsRawMeshEmpty() && SourceModel.ReductionSettings.BaseLODModel != LODIndex)
			{
				FRawMesh EmptyRawMesh;
				SourceModel.SaveRawMesh(EmptyRawMesh);
				SourceModel.SourceImportFilename = FString();
			}
		}
		}
		LightMapResolution = GroupSettings.GetDefaultLightMapResolution();

		if (!bBeforeDerivedDataCached)
		{
			bAutoComputeLODScreenSize = true;
		}
	}
	if (bRebuildImmediately && !bBeforeDerivedDataCached)
	{
		PostEditChange();
	}
#endif
}

void UStaticMesh::BroadcastNavCollisionChange()
{
	if (FNavigationSystem::WantsComponentChangeNotifies())
	{
		for (FObjectIterator Iter(UStaticMeshComponent::StaticClass()); Iter; ++Iter)
		{
			UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(*Iter);
			UWorld* MyWorld = StaticMeshComponent->GetWorld();
			if (StaticMeshComponent->GetStaticMesh() == this)
			{
				StaticMeshComponent->bNavigationRelevant = StaticMeshComponent->IsNavigationRelevant();
				FNavigationSystem::UpdateComponentData(*StaticMeshComponent);
			}
		}
	}
}

FStaticMeshSourceModel& UStaticMesh::AddSourceModel()
{
	int32 LodModelIndex = GetSourceModels().AddDefaulted();
	FStaticMeshSourceModel& NewSourceModel = GetSourceModel(LodModelIndex);
	NewSourceModel.StaticMeshOwner = this;
	return NewSourceModel;
}

void UStaticMesh::SetNumSourceModels(const int32 Num)
{
	const int32 OldNum = GetNumSourceModels();
	GetSourceModels().SetNum(Num);

	//Shrink the SectionInfoMap if some SourceModel are removed
	if (OldNum > Num)
	{
		for (int32 RemoveLODIndex = Num; RemoveLODIndex < OldNum; ++RemoveLODIndex)
		{
			int32 SectionCount = GetSectionInfoMap().GetSectionNumber(RemoveLODIndex);
			for (int32 SectionIndex = 0; SectionIndex < SectionCount; ++SectionIndex)
			{
				GetSectionInfoMap().Remove(RemoveLODIndex, SectionIndex);
			}
			SectionCount = GetOriginalSectionInfoMap().GetSectionNumber(RemoveLODIndex);
			for (int32 SectionIndex = 0; SectionIndex < SectionCount; ++SectionIndex)
			{
				GetOriginalSectionInfoMap().Remove(RemoveLODIndex, SectionIndex);
			}
		}
	}

	for (int32 Index = OldNum; Index < Num; ++Index)
	{
		FStaticMeshSourceModel& ThisSourceModel = GetSourceModel(Index);

		ThisSourceModel.StaticMeshOwner = this;
		int32 PreviousCustomLODIndex = 0;
		//Find the previous custom LOD
		for (int32 ReverseIndex = Index - 1; ReverseIndex > 0; ReverseIndex--)
		{
			const FStaticMeshSourceModel& StaticMeshModel = GetSourceModel(ReverseIndex);
			//If the custom import LOD is reduce and is not using himself as the source, do not consider it
			if (IsMeshDescriptionValid(ReverseIndex) && !(IsReductionActive(ReverseIndex) && StaticMeshModel.ReductionSettings.BaseLODModel != ReverseIndex))
			{
				PreviousCustomLODIndex = ReverseIndex;
				break;
			}
		}
		ThisSourceModel.ReductionSettings.BaseLODModel = PreviousCustomLODIndex;
		if (!IsMeshDescriptionValid(Index) && !IsReductionActive(Index))
		{
			//Set the Reduction percent
			ThisSourceModel.ReductionSettings.PercentTriangles = FMath::Pow(0.5f, (float)(Index-PreviousCustomLODIndex));
		}
	}
}

void UStaticMesh::RemoveSourceModel(const int32 Index)
{
	check(IsSourceModelValid(Index));

	//Remove the SectionInfoMap of the LOD we remove
	{
		int32 SectionCount = GetSectionInfoMap().GetSectionNumber(Index);
		for (int32 SectionIndex = 0; SectionIndex < SectionCount; ++SectionIndex)
		{
			GetSectionInfoMap().Remove(Index, SectionIndex);
		}
		SectionCount = GetOriginalSectionInfoMap().GetSectionNumber(Index);
		for (int32 SectionIndex = 0; SectionIndex < SectionCount; ++SectionIndex)
		{
			GetOriginalSectionInfoMap().Remove(Index, SectionIndex);
		}
	}

	//Move down all SectionInfoMap for the next LOD
	if (Index < GetNumSourceModels() - 1)
	{
		for (int32 MoveIndex = Index + 1; MoveIndex < GetNumSourceModels(); ++MoveIndex)
		{
			int32 SectionCount = GetSectionInfoMap().GetSectionNumber(MoveIndex);
			for (int32 SectionIndex = 0; SectionIndex < SectionCount; ++SectionIndex)
			{
				FMeshSectionInfo SectionInfo = GetSectionInfoMap().Get(MoveIndex, SectionIndex);
				GetSectionInfoMap().Set(MoveIndex - 1, SectionIndex, SectionInfo);
				GetSectionInfoMap().Remove(MoveIndex, SectionIndex);
			}
			SectionCount = GetOriginalSectionInfoMap().GetSectionNumber(MoveIndex);
			for (int32 SectionIndex = 0; SectionIndex < SectionCount; ++SectionIndex)
			{
				FMeshSectionInfo SectionInfo = GetOriginalSectionInfoMap().Get(MoveIndex, SectionIndex);
				GetOriginalSectionInfoMap().Set(MoveIndex - 1, SectionIndex, SectionInfo);
				GetOriginalSectionInfoMap().Remove(MoveIndex, SectionIndex);
			}
		}
	}

	//Remove the LOD
	GetSourceModels().RemoveAt(Index);
}

bool UStaticMesh::FixLODRequiresAdjacencyInformation(const int32 LODIndex, const bool bPreviewMode, bool bPromptUser, bool* OutUserCancel)
{
	if (OutUserCancel != nullptr)
	{
		*OutUserCancel = false;
	}

	bool bIsUnattended = FApp::IsUnattended() == true || GIsRunningUnattendedScript || GIsAutomationTesting;
	//Cannot prompt user in unattended mode
	if (!IsSourceModelValid(LODIndex) || (bIsUnattended && bPromptUser))
	{
		return false;
	}
	FStaticMeshSourceModel& SourceModel = GetSourceModel(LODIndex);
	const FMeshDescription* MeshDescription = GetMeshDescription(LODIndex);

	//In preview mode we simulate a false BuildAdjacencyBuffer
	if (MeshDescription && (!(SourceModel.BuildSettings.bBuildAdjacencyBuffer) || bPreviewMode))
	{
		FStaticMeshConstAttributes StaticMeshAttributes(*MeshDescription);

		TPolygonGroupAttributesConstRef<FName> PolygonGroupImportedMaterialSlotNames = StaticMeshAttributes.GetPolygonGroupMaterialSlotNames();
		int32 SectionIndex = 0;
		
		for (const FPolygonGroupID& PolygonGroupID : MeshDescription->PolygonGroups().GetElementIDs())
		{
			const FName MaterialImportedName = PolygonGroupImportedMaterialSlotNames[PolygonGroupID];
			int32 MaterialIndex = 0;
			for (FStaticMaterial& Material : StaticMaterials)
			{
				if (Material.ImportedMaterialSlotName != NAME_None && Material.ImportedMaterialSlotName == MaterialImportedName)
				{
					FStaticMaterial *RemapMaterial = &Material;
					FMeshSectionInfo SectionInfo = GetSectionInfoMap().Get(LODIndex, SectionIndex);
					if (StaticMaterials.IsValidIndex(SectionInfo.MaterialIndex))
					{
						RemapMaterial = &StaticMaterials[SectionInfo.MaterialIndex];
					}
					const bool bRequiresAdjacencyInformation = RequiresAdjacencyInformation(RemapMaterial->MaterialInterface, nullptr, GWorld->FeatureLevel);
					if (bRequiresAdjacencyInformation)
					{
						if (bPromptUser)
						{
							FText ConfirmRequiredAdjacencyText = FText::Format(LOCTEXT("ConfirmRequiredAdjacency", "Using a tessellation material required the adjacency buffer to be computed.\nDo you want to set the adjacency options to true?\n\n\tSaticMesh: {0}\n\tLOD Index: {1}\n\tMaterial: {2}"), FText::FromString(GetPathName()), LODIndex, FText::FromString(RemapMaterial->MaterialInterface->GetPathName()));
							EAppReturnType::Type Result = FMessageDialog::Open((OutUserCancel != nullptr) ? EAppMsgType::YesNoCancel : EAppMsgType::YesNo, ConfirmRequiredAdjacencyText);
							switch(Result)
							{
								//Handle cancel and negative answer
								case EAppReturnType::Cancel:
								{
									check(OutUserCancel != nullptr);
									*OutUserCancel = true;
									return false;
								}
								case EAppReturnType::No:
								{
									return false;
								}
							}
						}
						if (!bPreviewMode)
						{
							UE_LOG(LogStaticMesh, Warning, TEXT("Adjacency information not built for static mesh with a material that requires it. Forcing build setting to use adjacency.\n\tLOD Index: %d\n\tMaterial: %s\n\tStaticMesh: %s"), LODIndex, *RemapMaterial->MaterialInterface->GetPathName(), *GetPathName());
							SourceModel.BuildSettings.bBuildAdjacencyBuffer = true;
						}
						return true;
					}
				}
				MaterialIndex++;
			}
			SectionIndex++;
		}
	}
	return false;
}

#endif // WITH_EDITOR

void UStaticMesh::BeginDestroy()
{
	Super::BeginDestroy();

	// Cancel any in flight IO requests
	CancelPendingMipChangeRequest();

	// Safely unlink mesh from list of streamable ones.
	UnlinkStreaming();

	// Remove from the list of tracked assets if necessary
	TrackRenderAssetEvent(nullptr, this, false, nullptr);

	if (FApp::CanEverRender() && !HasAnyFlags(RF_ClassDefaultObject))
	{
		ReleaseResources();
	}
}

bool UStaticMesh::IsReadyForFinishDestroy()
{
	return ReleaseResourcesFence.IsFenceComplete() && !UpdateStreamingStatus();
}

int32 UStaticMesh::GetNumSectionsWithCollision() const
{
#if WITH_EDITORONLY_DATA
	int32 NumSectionsWithCollision = 0;

	if (RenderData && RenderData->LODResources.Num() > 0)
	{
		// Find how many sections have collision enabled
		const int32 UseLODIndex = FMath::Clamp(LODForCollision, 0, RenderData->LODResources.Num() - 1);
		const FStaticMeshLODResources& CollisionLOD = RenderData->LODResources[UseLODIndex];
		for (int32 SectionIndex = 0; SectionIndex < CollisionLOD.Sections.Num(); ++SectionIndex)
		{
			if (GetSectionInfoMap().Get(UseLODIndex, SectionIndex).bEnableCollision)
			{
				NumSectionsWithCollision++;
			}
		}
	}

	return NumSectionsWithCollision;
#else
	return 0;
#endif
}

void UStaticMesh::GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const
{
	int32 NumTriangles = 0;
	int32 NumVertices = 0;
	int32 NumUVChannels = 0;
	int32 NumLODs = 0;

	if (RenderData && RenderData->LODResources.Num() > 0)
	{
		const FStaticMeshLODResources& LOD = RenderData->LODResources[0];
		NumTriangles = LOD.IndexBuffer.GetNumIndices() / 3;
		NumVertices = LOD.VertexBuffers.StaticMeshVertexBuffer.GetNumVertices();
		NumUVChannels = LOD.VertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords();
		NumLODs = RenderData->LODResources.Num();
	}

	int32 NumSectionsWithCollision = GetNumSectionsWithCollision();

	int32 NumCollisionPrims = 0;
	if ( BodySetup != NULL )
	{
		NumCollisionPrims = BodySetup->AggGeom.GetElementCount();
	}

	FBoxSphereBounds Bounds(ForceInit);
	if (RenderData)
	{
		Bounds = RenderData->Bounds;
	}
	const FString ApproxSizeStr = FString::Printf(TEXT("%dx%dx%d"), FMath::RoundToInt(Bounds.BoxExtent.X * 2.0f), FMath::RoundToInt(Bounds.BoxExtent.Y * 2.0f), FMath::RoundToInt(Bounds.BoxExtent.Z * 2.0f));

	// Get name of default collision profile
	FName DefaultCollisionName = NAME_None;
	if(BodySetup != nullptr)
	{
		DefaultCollisionName = BodySetup->DefaultInstance.GetCollisionProfileName();
	}

	FString ComplexityString;
	if (BodySetup != nullptr)
	{
		ComplexityString = LexToString((ECollisionTraceFlag)BodySetup->GetCollisionTraceFlag());
	}

	OutTags.Add( FAssetRegistryTag("Triangles", FString::FromInt(NumTriangles), FAssetRegistryTag::TT_Numerical) );
	OutTags.Add( FAssetRegistryTag("Vertices", FString::FromInt(NumVertices), FAssetRegistryTag::TT_Numerical) );
	OutTags.Add( FAssetRegistryTag("UVChannels", FString::FromInt(NumUVChannels), FAssetRegistryTag::TT_Numerical) );
	OutTags.Add( FAssetRegistryTag("Materials", FString::FromInt(StaticMaterials.Num()), FAssetRegistryTag::TT_Numerical) );
	OutTags.Add( FAssetRegistryTag("ApproxSize", ApproxSizeStr, FAssetRegistryTag::TT_Dimensional) );
	OutTags.Add( FAssetRegistryTag("CollisionPrims", FString::FromInt(NumCollisionPrims), FAssetRegistryTag::TT_Numerical));
	OutTags.Add( FAssetRegistryTag("LODs", FString::FromInt(NumLODs), FAssetRegistryTag::TT_Numerical));
	OutTags.Add( FAssetRegistryTag("MinLOD", MinLOD.ToString(), FAssetRegistryTag::TT_Alphabetical));
	OutTags.Add( FAssetRegistryTag("SectionsWithCollision", FString::FromInt(NumSectionsWithCollision), FAssetRegistryTag::TT_Numerical));
	OutTags.Add( FAssetRegistryTag("DefaultCollision", DefaultCollisionName.ToString(), FAssetRegistryTag::TT_Alphabetical));
	OutTags.Add( FAssetRegistryTag("CollisionComplexity", ComplexityString, FAssetRegistryTag::TT_Alphabetical));

#if WITH_EDITORONLY_DATA
	if (AssetImportData)
	{
		OutTags.Add( FAssetRegistryTag(SourceFileTagName(), AssetImportData->GetSourceData().ToJson(), FAssetRegistryTag::TT_Hidden) );
	}
#endif

	Super::GetAssetRegistryTags(OutTags);
}

#if WITH_EDITOR
void UStaticMesh::GetAssetRegistryTagMetadata(TMap<FName, FAssetRegistryTagMetadata>& OutMetadata) const
{
	Super::GetAssetRegistryTagMetadata(OutMetadata);

	OutMetadata.Add("CollisionPrims",
		FAssetRegistryTagMetadata()
			.SetTooltip(NSLOCTEXT("UStaticMesh", "CollisionPrimsTooltip", "The number of collision primitives in the static mesh"))
			.SetImportantValue(TEXT("0"))
		);
}
#endif


/*------------------------------------------------------------------------------
	FStaticMeshSourceModel
------------------------------------------------------------------------------*/

FStaticMeshSourceModel::FStaticMeshSourceModel()
{
	LODDistance_DEPRECATED = 0.0f;
#if WITH_EDITOR
	RawMeshBulkData = new FRawMeshBulkData();
	ScreenSize.Default = 0.0f;
	StaticMeshOwner = nullptr;
#endif // #if WITH_EDITOR
	SourceImportFilename = FString();
#if WITH_EDITORONLY_DATA
	bImportWithBaseMesh = false;
#endif
}

FStaticMeshSourceModel::~FStaticMeshSourceModel()
{
#if WITH_EDITOR
	if (RawMeshBulkData)
	{
		delete RawMeshBulkData;
	}
#endif // #if WITH_EDITOR
}

#if WITH_EDITOR
bool FStaticMeshSourceModel::IsRawMeshEmpty() const
{
	// Even if the RawMeshBulkData is empty, it may be because it's a new asset serialized as MeshDescription.
	// Hence MeshDescriptionBulkData must also be invalid (and, by consequence, also MeshDescription).
	return RawMeshBulkData->IsEmpty() && !MeshDescriptionBulkData.IsValid() && !MeshDescription.IsValid();
}

void FStaticMeshSourceModel::LoadRawMesh(FRawMesh& OutRawMesh) const
{
	if (RawMeshBulkData->IsEmpty())
	{
		// If the RawMesh is empty, consider the possibility that it's a new asset with a valid MeshDescription which needs loading.
		// We require the FStaticMeshSourceModel to be in the UStaticMesh::SourceModels array, so that we can infer which LOD it
		// corresponds to. This would normally be unreasonably limiting, but since these methods are deprecated, we'll go with it.
		check(StaticMeshOwner != nullptr);
		const int32 LODIndex = this - &StaticMeshOwner->GetSourceModel(0);
		check(LODIndex < StaticMeshOwner->GetNumSourceModels());
		if (FMeshDescription* CachedMeshDescription = StaticMeshOwner->GetMeshDescription(LODIndex))
		{
			TMap<FName, int32> MaterialMap;
			check(StaticMeshOwner != nullptr);
			for (int32 MaterialIndex = 0; MaterialIndex < StaticMeshOwner->StaticMaterials.Num(); ++MaterialIndex)
			{
				MaterialMap.Add(StaticMeshOwner->StaticMaterials[MaterialIndex].ImportedMaterialSlotName, MaterialIndex);
			}
			FMeshDescriptionOperations::ConvertToRawMesh(*MeshDescription, OutRawMesh, MaterialMap);
		}
	}
	else
	{
		RawMeshBulkData->LoadRawMesh(OutRawMesh);
	}
}

void FStaticMeshSourceModel::SaveRawMesh(FRawMesh& InRawMesh, bool /* unused */)
{
	if (!InRawMesh.IsValid())
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(FStaticMeshSourceModel::SaveRawMesh);

	//Save both format
	RawMeshBulkData->SaveRawMesh(InRawMesh);

	CreateMeshDescription();

	TMap<int32, FName> MaterialMap;
	check(StaticMeshOwner != nullptr);
	FillMaterialName(StaticMeshOwner->StaticMaterials, MaterialMap);
	FMeshDescriptionOperations::ConvertFromRawMesh(InRawMesh, *MeshDescription, MaterialMap);
	
	// Package up mesh description into bulk data
	if (!MeshDescriptionBulkData.IsValid())
	{
		MeshDescriptionBulkData = MakeUnique<FMeshDescriptionBulkData>();
	}

	MeshDescriptionBulkData->SaveMeshDescription(*MeshDescription);
}

FMeshDescription* FStaticMeshSourceModel::CreateMeshDescription()
{
	if (!MeshDescription.IsValid())
	{
		// If this is the first time a MeshDescription is being created, create it and register its attributes
		MeshDescription = MakeUnique<FMeshDescription>();
	}
	else
	{
		// Otherwise, empty it completely
		*MeshDescription = FMeshDescription();
	}

	// Register static mesh attributes on it
	FStaticMeshAttributes StaticMeshAttributes(*MeshDescription);
	StaticMeshAttributes.Register();

	return MeshDescription.Get();
}

void FStaticMeshSourceModel::SerializeBulkData(FArchive& Ar, UObject* Owner)
{
	const bool bIsLoadingLegacyArchive = Ar.IsLoading() && Ar.CustomVer(FEditorObjectVersion::GUID) < FEditorObjectVersion::StaticMeshDeprecatedRawMesh;

	// Serialize RawMesh when loading legacy archives
	if (bIsLoadingLegacyArchive)
	{
		check(RawMeshBulkData != NULL);
		RawMeshBulkData->Serialize(Ar, Owner);
	}

	// Initialize the StaticMeshOwner
	if (Ar.IsLoading())
	{
		StaticMeshOwner = Cast<UStaticMesh>(Owner);
	}

	// Always serialize the MeshDescription bulk data when transacting (so undo/redo works correctly).
	// Now RawMesh is deprecated, always serialize unless we're loading an old archive.
	if (Ar.IsTransacting() || !bIsLoadingLegacyArchive)
	{
		if (Ar.IsSaving() && !MeshDescriptionBulkData.IsValid() && !RawMeshBulkData->IsEmpty())
		{
			// If saving a legacy asset which hasn't yet been committed as MeshDescription, perform the conversion now
			// so it can be loaded sucessfully as MeshDescription next time.
			// Note: even if there's a more recent cached MeshDescription, if it hasn't been committed, it will not be saved.
			FRawMesh RawMesh;
			LoadRawMesh(RawMesh);
			SaveRawMesh(RawMesh);
		}

		bool bIsValid = MeshDescriptionBulkData.IsValid();
		Ar << bIsValid;

		if (bIsValid)
		{
			if (Ar.IsLoading())
			{
				MeshDescriptionBulkData = MakeUnique<FMeshDescriptionBulkData>();
			}

			MeshDescriptionBulkData->Serialize(Ar, Owner);

			// As we are loading a new mesh description bulkdata, if there's a cached existing unpacked MeshDescription,
			// unpack the new one
			if (Ar.IsLoading() && MeshDescription.IsValid())
			{
				CreateMeshDescription();
				MeshDescriptionBulkData->LoadMeshDescription(*MeshDescription);
			}
		}

		// For transactions only, serialize the unpacked mesh description here too.
		// This is so we can preserve any transient attributes which have been set on it when undoing.
		if (Ar.IsTransacting())
		{
			bool bIsMeshDescriptionValid = MeshDescription.IsValid();
			Ar << bIsMeshDescriptionValid;

			if (bIsMeshDescriptionValid)
			{
				if (Ar.IsLoading())
				{
					CreateMeshDescription();
				}

				Ar << (*MeshDescription);
			}
		}
	}
}
#endif // #if WITH_EDITOR

/*------------------------------------------------------------------------------
	FMeshSectionInfoMap
------------------------------------------------------------------------------*/

#if WITH_EDITORONLY_DATA
	
bool operator==(const FMeshSectionInfo& A, const FMeshSectionInfo& B)
{
	return A.MaterialIndex == B.MaterialIndex
		&& A.bCastShadow == B.bCastShadow
		&& A.bEnableCollision == B.bEnableCollision;
}

bool operator!=(const FMeshSectionInfo& A, const FMeshSectionInfo& B)
{
	return !(A == B);
}
	
static uint32 GetMeshMaterialKey(int32 LODIndex, int32 SectionIndex)
{
	return ((LODIndex & 0xffff) << 16) | (SectionIndex & 0xffff);
}

void FMeshSectionInfoMap::Clear()
{
	Map.Empty();
}

int32 FMeshSectionInfoMap::GetSectionNumber(int32 LODIndex) const
{
	int32 SectionCount = 0;
	for (auto kvp : Map)
	{
		if (((kvp.Key & 0xffff0000) >> 16) == LODIndex)
		{
			SectionCount++;
		}
	}
	return SectionCount;
}

bool FMeshSectionInfoMap::IsValidSection(int32 LODIndex, int32 SectionIndex) const
{
	uint32 Key = GetMeshMaterialKey(LODIndex, SectionIndex);
	return (Map.Find(Key) != nullptr);
}

FMeshSectionInfo FMeshSectionInfoMap::Get(int32 LODIndex, int32 SectionIndex) const
{
	uint32 Key = GetMeshMaterialKey(LODIndex, SectionIndex);
	const FMeshSectionInfo* InfoPtr = Map.Find(Key);
	if (InfoPtr == NULL)
	{
		Key = GetMeshMaterialKey(0, SectionIndex);
		InfoPtr = Map.Find(Key);
	}
	if (InfoPtr != NULL)
	{
		return *InfoPtr;
	}
	return FMeshSectionInfo(SectionIndex);
}

void FMeshSectionInfoMap::Set(int32 LODIndex, int32 SectionIndex, FMeshSectionInfo Info)
{
	uint32 Key = GetMeshMaterialKey(LODIndex, SectionIndex);
	Map.Add(Key, Info);
}

void FMeshSectionInfoMap::Remove(int32 LODIndex, int32 SectionIndex)
{
	uint32 Key = GetMeshMaterialKey(LODIndex, SectionIndex);
	Map.Remove(Key);
}

void FMeshSectionInfoMap::CopyFrom(const FMeshSectionInfoMap& Other)
{
	for (TMap<uint32,FMeshSectionInfo>::TConstIterator It(Other.Map); It; ++It)
	{
		Map.Add(It.Key(), It.Value());
	}
}

bool FMeshSectionInfoMap::AnySectionHasCollision(int32 LodIndex) const
{
	for (TMap<uint32,FMeshSectionInfo>::TConstIterator It(Map); It; ++It)
	{
		uint32 Key = It.Key();
		int32 KeyLODIndex = (int32)(Key >> 16);
		if (KeyLODIndex == LodIndex && It.Value().bEnableCollision)
		{
			return true;
		}
	}
	return false;
}

FArchive& operator<<(FArchive& Ar, FMeshSectionInfo& Info)
{
	Ar << Info.MaterialIndex;
	Ar << Info.bEnableCollision;
	Ar << Info.bCastShadow;
	return Ar;
}

void FMeshSectionInfoMap::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FReleaseObjectVersion::GUID);
	Ar.UsingCustomVersion(FEditorObjectVersion::GUID);

	if ( Ar.CustomVer(FReleaseObjectVersion::GUID) < FReleaseObjectVersion::UPropertryForMeshSectionSerialize // Release-4.15 change
		&& Ar.CustomVer(FEditorObjectVersion::GUID) < FEditorObjectVersion::UPropertryForMeshSectionSerialize) // Dev-Editor change
	{
		Ar << Map;
	}
}

#endif // #if WITH_EDITORONLY_DATA

/**
 * Registers the mesh attributes required by the mesh description for a static mesh.
 */
void UStaticMesh::RegisterMeshAttributes(FMeshDescription& MeshDescription)
{
	FStaticMeshAttributes Attributes(MeshDescription);
	Attributes.Register();
}


#if WITH_EDITOR
static FStaticMeshRenderData& GetPlatformStaticMeshRenderData(UStaticMesh* Mesh, const ITargetPlatform* Platform)
{
	check(Mesh && Mesh->RenderData);
	const FStaticMeshLODSettings& PlatformLODSettings = Platform->GetStaticMeshLODSettings();
	FString PlatformDerivedDataKey = BuildStaticMeshDerivedDataKey(
		BuildStaticMeshDerivedDataKeySuffix(Mesh, PlatformLODSettings.GetLODGroup(Mesh->LODGroup)));
	FStaticMeshRenderData* PlatformRenderData = Mesh->RenderData.Get();

	if (Mesh->GetOutermost()->HasAnyPackageFlags(PKG_FilterEditorOnly))
	{
		check(PlatformRenderData);
		return *PlatformRenderData;
	}

	while (PlatformRenderData && PlatformRenderData->DerivedDataKey != PlatformDerivedDataKey)
	{
		PlatformRenderData = PlatformRenderData->NextCachedRenderData.Get();
	}
	if (PlatformRenderData == NULL)
	{
		// Cache render data for this platform and insert it in to the linked list.
		PlatformRenderData = new FStaticMeshRenderData();
		PlatformRenderData->Cache(Mesh, PlatformLODSettings);
		check(PlatformRenderData->DerivedDataKey == PlatformDerivedDataKey);
		Swap(PlatformRenderData->NextCachedRenderData, Mesh->RenderData->NextCachedRenderData);
		Mesh->RenderData->NextCachedRenderData = TUniquePtr<FStaticMeshRenderData>(PlatformRenderData);
	}
	check(PlatformRenderData);
	return *PlatformRenderData;
}


#if WITH_EDITORONLY_DATA

bool UStaticMesh::LoadMeshDescription(int32 LodIndex, FMeshDescription& OutMeshDescription) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UStaticMesh::LoadMeshDescription);

	// Ensure MeshDescription is empty, with no attributes registered
	OutMeshDescription = FMeshDescription();

	const FStaticMeshSourceModel& SourceModel = GetSourceModel(LodIndex);

	// If we don't have a valid MeshDescription, try and get one...
	if (SourceModel.MeshDescriptionBulkData.IsValid())
	{
		// Unpack MeshDescription from the bulk data which was deserialized
		SourceModel.MeshDescriptionBulkData->LoadMeshDescription(OutMeshDescription);
		return true;
	}
	
	// If BulkData isn't valid, this means either:
	// a) This LOD doesn't have a MeshDescription (because it's been generated), or;
	// b) This is a legacy asset which still uses RawMesh, in which case we'll look in the DDC for it.
	FString MeshDataKey;
	if (GetMeshDataKey(LodIndex, MeshDataKey))
	{
		TArray<uint8> DerivedData;
		if (GetDerivedDataCacheRef().GetSynchronous(*MeshDataKey, DerivedData))
		{
			// If there was valid DDC data, we assume this is because the asset is an old one with valid RawMeshBulkData
			check(!SourceModel.RawMeshBulkData->IsEmpty());

			// Load from the DDC
			const bool bIsPersistent = true;
			FMemoryReader Ar(DerivedData, bIsPersistent);

			// Create a bulk data object which will be immediately thrown away (as it is not in an archive)
			FMeshDescriptionBulkData MeshDescriptionBulkData;
			MeshDescriptionBulkData.Serialize(Ar, const_cast<UStaticMesh*>(this));

			// Unpack MeshDescription from the bulk data
			MeshDescriptionBulkData.LoadMeshDescription(OutMeshDescription);
			return true;
		}
	}

	// If after all this we *still* don't have a valid MeshDescription, but there's a valid RawMesh, convert that to a MeshDescription.
	if (!SourceModel.RawMeshBulkData->IsEmpty())
	{
		FRawMesh LodRawMesh;
		SourceModel.LoadRawMesh(LodRawMesh);
		TMap<int32, FName> MaterialMap;
		FillMaterialName(StaticMaterials, MaterialMap);

		// Register static mesh attributes on the mesh description
		FStaticMeshAttributes StaticMeshAttributes(OutMeshDescription);
		StaticMeshAttributes.Register();

		FMeshDescriptionOperations::ConvertFromRawMesh(LodRawMesh, OutMeshDescription, MaterialMap);
		return true;
	}

	return false;
}

bool UStaticMesh::CloneMeshDescription(int32 LodIndex, FMeshDescription& OutMeshDescription) const
{
	if (!IsSourceModelValid(LodIndex))
	{
		return false;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(UStaticMesh::CloneMeshDescription);
	
	const FStaticMeshSourceModel& SourceModel = GetSourceModel(LodIndex);

	if (SourceModel.MeshDescription.IsValid())
	{
		OutMeshDescription = *SourceModel.MeshDescription.Get();
		return true;
	}

	return LoadMeshDescription(LodIndex, OutMeshDescription);
}

FMeshDescription* UStaticMesh::GetMeshDescription(int32 LodIndex) const
{
	if (!IsSourceModelValid(LodIndex))
	{
		return nullptr;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(UStaticMesh::GetMeshDescription);

	// Require a const_cast here, because GetMeshDescription should ostensibly have const semantics,
	// but the lazy initialization (from the BulkData or the DDC) is a one-off event which breaks constness.
	UStaticMesh* MutableThis = const_cast<UStaticMesh*>(this);

	FStaticMeshSourceModel& SourceModel = MutableThis->GetSourceModel(LodIndex);

	if (!SourceModel.MeshDescription.IsValid())
	{
		FMeshDescription MeshDescription;
		if (LoadMeshDescription(LodIndex, MeshDescription))
		{
			SourceModel.MeshDescription = MakeUnique<FMeshDescription>(MoveTemp(MeshDescription));
		}
	}

	return SourceModel.MeshDescription.Get();
}


bool UStaticMesh::IsMeshDescriptionValid(int32 LodIndex) const
{
	if (!IsSourceModelValid(LodIndex))
	{
		return false;
	}

	const FStaticMeshSourceModel& SourceModel = GetSourceModel(LodIndex);

	// Determine whether a mesh description is valid without requiring it to be loaded first.
	// If there is a valid MeshDescriptionBulkData, we know this implies a valid mesh description.
	// If not, then it's a legacy asset which will have a mesh description built from the RawMeshBulkData, if non-empty.
	return SourceModel.MeshDescription.IsValid() ||
		   SourceModel.MeshDescriptionBulkData.IsValid() ||
		   !SourceModel.RawMeshBulkData->IsEmpty();
}


FMeshDescription* UStaticMesh::CreateMeshDescription(int32 LodIndex)
{
	if (IsSourceModelValid(LodIndex))
	{
		FStaticMeshSourceModel& SourceModel = GetSourceModel(LodIndex);
		return SourceModel.CreateMeshDescription();
	}

	return nullptr;
}

FMeshDescription* UStaticMesh::CreateMeshDescription(int32 LodIndex, FMeshDescription InMeshDescription)
{
	FMeshDescription* NewMeshDescription = CreateMeshDescription(LodIndex);
	if (NewMeshDescription != nullptr)
	{
		*NewMeshDescription = MoveTemp(InMeshDescription);
	}

	return NewMeshDescription;
}


void UStaticMesh::CommitMeshDescription(int32 LodIndex, const FCommitMeshDescriptionParams& Params)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UStaticMesh::CommitMeshDescription);

	// This part should remain thread-safe so it can be called from any thread
	// as long as no more than one thread is calling it for the same UStaticMesh.

	// The source model must be created before calling this function
	check(IsSourceModelValid(LodIndex));

	FStaticMeshSourceModel& SourceModel = GetSourceModel(LodIndex);
	if (SourceModel.MeshDescription.IsValid())
	{
		// Package up mesh description into bulk data
		if (!SourceModel.MeshDescriptionBulkData.IsValid())
		{
			SourceModel.MeshDescriptionBulkData = MakeUnique<FMeshDescriptionBulkData>();
		}

		SourceModel.MeshDescriptionBulkData->SaveMeshDescription(*SourceModel.MeshDescription);
		if (Params.bUseHashAsGuid)
		{
			SourceModel.MeshDescriptionBulkData->UseHashAsGuid();
		}
	}
	else
	{
		SourceModel.MeshDescriptionBulkData.Reset();
	}

	// Clear RawMeshBulkData and mark as invalid.
	// If any legacy tool needs the RawMesh at this point, it will do a conversion from MD at that moment.
	SourceModel.RawMeshBulkData->Empty();

	// This part is not thread-safe, so we give the caller the option of calling it manually from the mainthread
	if (Params.bMarkPackageDirty)
	{
		MarkPackageDirty();
	}
}

void UStaticMesh::ClearMeshDescription(int32 LodIndex)
{
	if (IsSourceModelValid(LodIndex))
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UStaticMesh::ClearMeshDescription);

		FStaticMeshSourceModel& SourceModel = GetSourceModel(LodIndex);
		SourceModel.MeshDescription.Reset();
	}
}


void UStaticMesh::ClearMeshDescriptions()
{
	for (int LODIndex = 0; LODIndex < GetNumSourceModels(); LODIndex++)
	{
		ClearMeshDescription(LODIndex);
	}
}

void UStaticMesh::FixupMaterialSlotName()
{
	TArray<FName> UniqueMaterialSlotName;
	//Make sure we have non empty imported material slot names
	for (FStaticMaterial& Material : StaticMaterials)
	{
		if (Material.ImportedMaterialSlotName == NAME_None)
		{
			if (Material.MaterialSlotName != NAME_None)
			{
				Material.ImportedMaterialSlotName = Material.MaterialSlotName;
			}
			else if (Material.MaterialInterface != nullptr)
			{
				Material.ImportedMaterialSlotName = Material.MaterialInterface->GetFName();
			}
			else
			{
				Material.ImportedMaterialSlotName = FName(TEXT("MaterialSlot"));
			}
		}

		FString UniqueName = Material.ImportedMaterialSlotName.ToString();
		int32 UniqueIndex = 1;
		while (UniqueMaterialSlotName.Contains(FName(*UniqueName)))
		{
			UniqueName = FString::Printf(TEXT("%s_%d"), *UniqueName, UniqueIndex);
			UniqueIndex++;
		}
		Material.ImportedMaterialSlotName = FName(*UniqueName);
		UniqueMaterialSlotName.Add(Material.ImportedMaterialSlotName);
		if (Material.MaterialSlotName == NAME_None)
		{
			Material.MaterialSlotName = Material.ImportedMaterialSlotName;
		}
	}
}

// If static mesh derived data needs to be rebuilt (new format, serialization
// differences, etc.) replace the version GUID below with a new one.
// In case of merge conflicts with DDC versions, you MUST generate a new GUID
// and set this new GUID as the version.                                       
#define MESHDATAKEY_STATICMESH_DERIVEDDATA_VER TEXT("A3E0B7AD760A496A8C56C261B5FE9BF9")

static const FString& GetMeshDataKeyStaticMeshDerivedDataVersion()
{
	static FString CachedVersionString(MESHDATAKEY_STATICMESH_DERIVEDDATA_VER);
	return CachedVersionString;
}

bool UStaticMesh::GetMeshDataKey(int32 LodIndex, FString& OutKey) const
{
	OutKey.Empty();
	if (LodIndex >= GetNumSourceModels())
	{
		return false;
	}

	FSHA1 Sha;
	FString LodIndexString = FString::Printf(TEXT("%d_"), LodIndex);
	const FStaticMeshSourceModel& SourceModel = GetSourceModel(LodIndex);
	if(!SourceModel.RawMeshBulkData->IsEmpty())
	{
		LodIndexString += SourceModel.RawMeshBulkData->GetIdString();
	}
	else
	{
		LodIndexString += TEXT("REDUCELOD");
	}
	const TArray<TCHAR>& LodIndexArray = LodIndexString.GetCharArray();
	Sha.Update((uint8*)LodIndexArray.GetData(), LodIndexArray.Num() * LodIndexArray.GetTypeSize());
	Sha.Final();

	// Retrieve the hash and use it to construct a pseudo-GUID.
	uint32 Hash[5];
	Sha.GetHash((uint8*)Hash);
	FGuid Guid = FGuid(Hash[0] ^ Hash[4], Hash[1], Hash[2], Hash[3]);
	FString MeshLodData = Guid.ToString(EGuidFormats::Digits);

	OutKey = FDerivedDataCacheInterface::BuildCacheKey(
		TEXT("MESHDATAKEY_STATICMESH"),
		*GetMeshDataKeyStaticMeshDerivedDataVersion(),
		*MeshLodData
	);
	return true;
}


void UStaticMesh::CacheMeshData()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UStaticMesh::CacheMeshData);

	// Generate MeshDescription source data in the DDC if no bulk data is present from the asset
	for (int32 LodIndex = 0; LodIndex < GetNumSourceModels(); ++LodIndex)
	{
		FStaticMeshSourceModel& SourceModel = GetSourceModel(LodIndex);
		if (!SourceModel.MeshDescriptionBulkData.IsValid())
		{
			// Legacy assets used to store their source data in the RawMeshBulkData
			// Migrate it to the new description if present
			if (!SourceModel.RawMeshBulkData->IsEmpty())
			{
				FString MeshDataKey;
				if (GetMeshDataKey(LodIndex, MeshDataKey))
				{
					// If the DDC key doesn't exist, convert the data and save it to DDC
					if (!GetDerivedDataCacheRef().CachedDataProbablyExists(*MeshDataKey))
					{
						// Get the RawMesh for this LOD
						FRawMesh TempRawMesh;
						SourceModel.RawMeshBulkData->LoadRawMesh(TempRawMesh);

						// Create a new MeshDescription
						FMeshDescription* MeshDescription = SourceModel.CreateMeshDescription();

						// Convert the RawMesh to MeshDescription
						TMap<int32, FName> MaterialMap;
						FillMaterialName(StaticMaterials, MaterialMap);
						FMeshDescriptionOperations::ConvertFromRawMesh(TempRawMesh, *MeshDescription, MaterialMap);

						// Pack MeshDescription into temporary bulk data, ready to write out to DDC.
						// This will be reloaded from the DDC when needed if a MeshDescription is requested from the static mesh.
						FMeshDescriptionBulkData MeshDescriptionBulkData;
						MeshDescriptionBulkData.SaveMeshDescription(*MeshDescription);

						// Write the DDC cache
						TArray<uint8> DerivedData;
						const bool bIsPersistent = true;
						FMemoryWriter Ar(DerivedData, bIsPersistent);
						MeshDescriptionBulkData.Serialize(Ar, this);
						GetDerivedDataCacheRef().Put(*MeshDataKey, DerivedData);
					}
				}
			}
		}
	}
}

bool UStaticMesh::AddUVChannel(int32 LODIndex)
{
	FMeshDescription* MeshDescription = GetMeshDescription(LODIndex);
	if (MeshDescription)
	{
		Modify();

		if (FMeshDescriptionOperations::AddUVChannel(*MeshDescription))
		{
			CommitMeshDescription(LODIndex);
			PostEditChange();

			return true;
		}
	}
	return false;
}

bool UStaticMesh::InsertUVChannel(int32 LODIndex, int32 UVChannelIndex)
{
	FMeshDescription* MeshDescription = GetMeshDescription(LODIndex);
	if (MeshDescription)
	{
		Modify();

		if (FMeshDescriptionOperations::InsertUVChannel(*MeshDescription, UVChannelIndex))
		{
			// Adjust the lightmap UV indices in the Build Settings to account for the new channel
			FMeshBuildSettings& LODBuildSettings = GetSourceModel(LODIndex).BuildSettings;
			if (UVChannelIndex <= LODBuildSettings.SrcLightmapIndex)
			{
				++LODBuildSettings.SrcLightmapIndex;
			}

			if (UVChannelIndex <= LODBuildSettings.DstLightmapIndex)
			{
				++LODBuildSettings.DstLightmapIndex;
			}

			if (UVChannelIndex <= LightMapCoordinateIndex)
			{
				++LightMapCoordinateIndex;
			}

			CommitMeshDescription(LODIndex);
			PostEditChange();

			return true;
		}
	}
	return false;
}

bool UStaticMesh::RemoveUVChannel(int32 LODIndex, int32 UVChannelIndex)
{
	FMeshDescription* MeshDescription = GetMeshDescription(LODIndex);
	if (MeshDescription)
	{
		FMeshBuildSettings& LODBuildSettings = GetSourceModel(LODIndex).BuildSettings;

		if (LODBuildSettings.bGenerateLightmapUVs)
		{
			if (UVChannelIndex == LODBuildSettings.SrcLightmapIndex)
			{
				UE_LOG(LogStaticMesh, Error, TEXT("RemoveUVChannel: To remove the lightmap source UV channel, disable \"Generate Lightmap UVs\" in the Build Settings."));
				return false;
			}

			if (UVChannelIndex == LODBuildSettings.DstLightmapIndex)
			{
				UE_LOG(LogStaticMesh, Error, TEXT("RemoveUVChannel: To remove the lightmap destination UV channel, disable \"Generate Lightmap UVs\" in the Build Settings."));
				return false;
			}
		}

		Modify();

		if (FMeshDescriptionOperations::RemoveUVChannel(*MeshDescription, UVChannelIndex))
		{
			// Adjust the lightmap UV indices in the Build Settings to account for the removed channel
			if (UVChannelIndex < LODBuildSettings.SrcLightmapIndex)
			{
				--LODBuildSettings.SrcLightmapIndex;
			}

			if (UVChannelIndex < LODBuildSettings.DstLightmapIndex)
			{
				--LODBuildSettings.DstLightmapIndex;
			}

			if (UVChannelIndex < LightMapCoordinateIndex)
			{
				--LightMapCoordinateIndex;
			}

			CommitMeshDescription(LODIndex);
			PostEditChange();

			return true;
		}
	}
	return false;
}

bool UStaticMesh::SetUVChannel(int32 LODIndex, int32 UVChannelIndex, const TMap<FVertexInstanceID, FVector2D>& TexCoords)
{
	FMeshDescription* MeshDescription = GetMeshDescription(LODIndex);
	if (!MeshDescription)
	{
		return false;
	}

	if (TexCoords.Num() < MeshDescription->VertexInstances().Num())
	{
		return false;
	}

	Modify();

	FStaticMeshAttributes Attributes(*MeshDescription);

	TMeshAttributesRef<FVertexInstanceID, FVector2D> UVs = Attributes.GetVertexInstanceUVs();
	for (const FVertexInstanceID& VertexInstanceID : MeshDescription->VertexInstances().GetElementIDs())
	{
		if (const FVector2D* UVCoord = TexCoords.Find(VertexInstanceID))
		{
			UVs.Set(VertexInstanceID, UVChannelIndex, *UVCoord);
		}
		else
		{
			ensureMsgf(false, TEXT("Tried to apply UV data that did not match the StaticMesh MeshDescription."));
		}
	}

	CommitMeshDescription(LODIndex);
	PostEditChange();

	return true;
}

#endif

int32 UStaticMesh::GetNumUVChannels(int32 LODIndex)
{
	int32 NumUVChannels = 0;
#if WITH_EDITORONLY_DATA
	FMeshDescription* MeshDescription = GetMeshDescription(LODIndex);
	if (MeshDescription)
	{
		FStaticMeshConstAttributes Attributes(*MeshDescription);
		NumUVChannels = Attributes.GetVertexInstanceUVs().GetNumIndices();
	}
#endif
	return NumUVChannels;
}

void UStaticMesh::CacheDerivedData()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UStaticMesh::CacheDerivedData);

#if WITH_EDITORONLY_DATA
	CacheMeshData();
#endif
	// Cache derived data for the running platform.
	ITargetPlatformManagerModule& TargetPlatformManager = GetTargetPlatformManagerRef();
	ITargetPlatform* RunningPlatform = TargetPlatformManager.GetRunningTargetPlatform();
	check(RunningPlatform);
	const FStaticMeshLODSettings& LODSettings = RunningPlatform->GetStaticMeshLODSettings();

	if (RenderData)
	{
		// This is the responsability of the caller to ensure this has been called
		// on the main thread when calling CacheDerivedData() from another thread.
		if (IsInGameThread())
		{
			// Finish any previous async builds before modifying RenderData
			// This can happen during import as the mesh is rebuilt redundantly
			GDistanceFieldAsyncQueue->BlockUntilBuildComplete(this, true);
		}

		for (int32 LODIndex = 0; LODIndex < RenderData->LODResources.Num(); ++LODIndex)
		{
			FDistanceFieldVolumeData* DistanceFieldData = RenderData->LODResources[LODIndex].DistanceFieldData;

			if (DistanceFieldData)
			{
				// Release before destroying RenderData
				DistanceFieldData->VolumeTexture.Release();
			}
		}
	}

	RenderData = MakeUnique<FStaticMeshRenderData>();
	RenderData->Cache(this, LODSettings);

	// Conditionally create occluder data
	OccluderData = FStaticMeshOccluderData::Build(this);

	// Additionally cache derived data for any other platforms we care about.
	const TArray<ITargetPlatform*>& TargetPlatforms = TargetPlatformManager.GetActiveTargetPlatforms();
	for (int32 PlatformIndex = 0; PlatformIndex < TargetPlatforms.Num(); ++PlatformIndex)
	{
		ITargetPlatform* Platform = TargetPlatforms[PlatformIndex];
		if (Platform != RunningPlatform)
		{
			GetPlatformStaticMeshRenderData(this, Platform);
		}
	}
}

#endif // #if WITH_EDITORONLY_DATA

void UStaticMesh::CalculateExtendedBounds()
{
	FBoxSphereBounds Bounds(ForceInit);
	if (RenderData)
	{
		Bounds = RenderData->Bounds;
	}

	// Only apply bound extension if necessary, as it will result in a larger bounding sphere radius than retrieved from the render data
	if (!NegativeBoundsExtension.IsZero() || !PositiveBoundsExtension.IsZero())
	{
		// Convert to Min and Max
		FVector Min = Bounds.Origin - Bounds.BoxExtent;
		FVector Max = Bounds.Origin + Bounds.BoxExtent;
		// Apply bound extensions
		Min -= NegativeBoundsExtension;
		Max += PositiveBoundsExtension;
		// Convert back to Origin, Extent and update SphereRadius
		Bounds.Origin = (Min + Max) / 2;
		Bounds.BoxExtent = (Max - Min) / 2;
		Bounds.SphereRadius = Bounds.BoxExtent.Size();
	}

	ExtendedBounds = Bounds;

#if WITH_EDITOR
	OnExtendedBoundsChanged.Broadcast(Bounds);
#endif
}


#if WITH_EDITORONLY_DATA
FUObjectAnnotationSparseBool GStaticMeshesThatNeedMaterialFixup;
#endif // #if WITH_EDITORONLY_DATA

#if WITH_EDITOR
COREUOBJECT_API extern bool GOutputCookingWarnings;
#endif


/**
 *	UStaticMesh::Serialize
 */
void UStaticMesh::Serialize(FArchive& Ar)
{
	LLM_SCOPE(ELLMTag::StaticMesh);

	DECLARE_SCOPE_CYCLE_COUNTER( TEXT("UStaticMesh::Serialize"), STAT_StaticMesh_Serialize, STATGROUP_LoadTime );

	TRACE_CPUPROFILER_EVENT_SCOPE(UStaticMesh::Serialize);

	Super::Serialize(Ar);

	Ar.UsingCustomVersion(FReleaseObjectVersion::GUID);
	Ar.UsingCustomVersion(FEditorObjectVersion::GUID);
	Ar.UsingCustomVersion(FRenderingObjectVersion::GUID);
	Ar.UsingCustomVersion(FReleaseObjectVersion::GUID);

	FStripDataFlags StripFlags( Ar );

	bool bCooked = Ar.IsCooking();
	Ar << bCooked;

#if WITH_EDITORONLY_DATA
	if (Ar.IsLoading() && Ar.UE4Ver() < VER_UE4_REMOVE_ZERO_TRIANGLE_SECTIONS)
	{
		GStaticMeshesThatNeedMaterialFixup.Set(this);
	}
#endif // #if WITH_EDITORONLY_DATA

	Ar << BodySetup;

	if (Ar.UE4Ver() >= VER_UE4_STATIC_MESH_STORE_NAV_COLLISION)
	{
		Ar << NavCollision;
#if WITH_EDITOR
		if ((BodySetup != nullptr) && 
			bHasNavigationData && 
			(NavCollision == nullptr))
		{
			if (Ar.IsPersistent() && Ar.IsLoading() && (Ar.GetDebugSerializationFlags() & DSF_EnableCookerWarnings))
			{
				UE_LOG(LogStaticMesh, Warning, TEXT("Serialized NavCollision but it was null (%s) NavCollision will be created dynamicaly at cook time.  Please resave package %s."), *GetName(), *GetOutermost()->GetPathName());
			}
		}
#endif
	}
#if WITH_EDITOR
	else if (bHasNavigationData && BodySetup && (Ar.GetDebugSerializationFlags() & DSF_EnableCookerWarnings))
	{
		UE_LOG(LogStaticMesh, Warning, TEXT("This StaticMeshes (%s) NavCollision will be created dynamicaly at cook time.  Please resave %s."), *GetName(), *GetOutermost()->GetPathName())
	}
#endif

	Ar.UsingCustomVersion(FFrameworkObjectVersion::GUID);

	if(Ar.IsLoading() && Ar.CustomVer(FFrameworkObjectVersion::GUID) < FFrameworkObjectVersion::UseBodySetupCollisionProfile && BodySetup)
	{
		BodySetup->DefaultInstance.SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	}

#if WITH_EDITORONLY_DATA
	if( !StripFlags.IsEditorDataStripped() )
	{
		if ( Ar.IsLoading() && Ar.UE4Ver() < VER_UE4_DEPRECATED_STATIC_MESH_THUMBNAIL_PROPERTIES_REMOVED )
		{
			FRotator DummyThumbnailAngle;
			float DummyThumbnailDistance;
			Ar << DummyThumbnailAngle;
			Ar << DummyThumbnailDistance;
		}
	}

	if( !StripFlags.IsEditorDataStripped() )
	{
		// TODO: These should be gated with a version check, but not able to be done in this stream.
		FString Deprecated_HighResSourceMeshName;
		uint32 Deprecated_HighResSourceMeshCRC;

		Ar << Deprecated_HighResSourceMeshName;
		Ar << Deprecated_HighResSourceMeshCRC;
	}
#endif // #if WITH_EDITORONLY_DATA

	if( Ar.IsCountingMemory() )
	{
		// Include collision as part of memory used
		if ( BodySetup )
		{
			BodySetup->Serialize( Ar );
		}

		if ( NavCollision )
		{
			NavCollision->Serialize( Ar );
		}

		//TODO: Count these members when calculating memory used
		//Ar << ReleaseResourcesFence;
	}

	Ar << LightingGuid;
	Ar << Sockets;

#if WITH_EDITOR
	if (!StripFlags.IsEditorDataStripped())
	{
		for (int32 i = 0; i < GetNumSourceModels(); ++i)
		{
			FStaticMeshSourceModel& SrcModel = GetSourceModel(i);
			SrcModel.SerializeBulkData(Ar, this);
		}

		if (Ar.CustomVer(FEditorObjectVersion::GUID) < FEditorObjectVersion::UPropertryForMeshSection)
		{
			GetSectionInfoMap().Serialize(Ar);
		}

		// Need to set a flag rather than do conversion in place as RenderData is not
		// created until postload and it is needed for bounding information
		bRequiresLODDistanceConversion = Ar.UE4Ver() < VER_UE4_STATIC_MESH_SCREEN_SIZE_LODS;
		bRequiresLODScreenSizeConversion = Ar.CustomVer(FFrameworkObjectVersion::GUID) < FFrameworkObjectVersion::LODsUseResolutionIndependentScreenSize;
	}
#endif // #if WITH_EDITOR

	// Inline the derived data for cooked builds. Never include render data when
	// counting memory as it is included by GetResourceSize.
	if (bCooked && !IsTemplate() && !Ar.IsCountingMemory())
	{	
		if (Ar.IsLoading())
		{
			RenderData = MakeUnique<FStaticMeshRenderData>();
			RenderData->Serialize(Ar, this, bCooked);
			
			FStaticMeshOccluderData::SerializeCooked(Ar, this);
		}

#if WITH_EDITOR
		else if (Ar.IsSaving())
		{
			FStaticMeshRenderData& PlatformRenderData = GetPlatformStaticMeshRenderData(this, Ar.CookingTarget());
			PlatformRenderData.Serialize(Ar, this, bCooked);
			
			FStaticMeshOccluderData::SerializeCooked(Ar, this);
		}
#endif
	}

	if (Ar.UE4Ver() >= VER_UE4_SPEEDTREE_STATICMESH)
	{
		bool bHasSpeedTreeWind = SpeedTreeWind.IsValid();
		Ar << bHasSpeedTreeWind;

		if (bHasSpeedTreeWind)
		{
			if (!SpeedTreeWind.IsValid())
			{
				SpeedTreeWind = TSharedPtr<FSpeedTreeWind>(new FSpeedTreeWind);
			}

			Ar << *SpeedTreeWind;
		}
	}

#if WITH_EDITORONLY_DATA
	if ( Ar.IsLoading() && Ar.UE4Ver() < VER_UE4_ASSET_IMPORT_DATA_AS_JSON && !AssetImportData)
	{
		// AssetImportData should always be valid
		AssetImportData = NewObject<UAssetImportData>(this, TEXT("AssetImportData"));
	}
	
	// SourceFilePath and SourceFileTimestamp were moved into a subobject
	if ( Ar.IsLoading() && Ar.UE4Ver() < VER_UE4_ADDED_FBX_ASSET_IMPORT_DATA && AssetImportData )
	{
		// AssetImportData should always have been set up in the constructor where this is relevant
		FAssetImportInfo Info;
		Info.Insert(FAssetImportInfo::FSourceFile(SourceFilePath_DEPRECATED));
		AssetImportData->SourceData = MoveTemp(Info);
		
		SourceFilePath_DEPRECATED = TEXT("");
		SourceFileTimestamp_DEPRECATED = TEXT("");
	}

	if (Ar.IsLoading() && Ar.CustomVer(FRenderingObjectVersion::GUID) < FRenderingObjectVersion::DistanceFieldSelfShadowBias)
	{
		DistanceFieldSelfShadowBias = GetSourceModel(0).BuildSettings.DistanceFieldBias_DEPRECATED * 10.0f;
	}

	if (Ar.CustomVer(FEditorObjectVersion::GUID) >= FEditorObjectVersion::RefactorMeshEditorMaterials)
#endif // WITH_EDITORONLY_DATA
	{
		Ar << StaticMaterials;
	}
#if WITH_EDITORONLY_DATA
	else if (Ar.IsLoading())
	{
		TArray<UMaterialInterface*> Unique_Materials_DEPRECATED;
		TArray<FName> MaterialSlotNames;
		for (UMaterialInterface *MaterialInterface : Materials_DEPRECATED)
		{
			FName MaterialSlotName = MaterialInterface != nullptr ? MaterialInterface->GetFName() : NAME_None;
			int32 NameCounter = 1;
			if (MaterialInterface)
			{
				while (MaterialSlotName != NAME_None && MaterialSlotNames.Find(MaterialSlotName) != INDEX_NONE)
				{
					FString MaterialSlotNameStr = MaterialInterface->GetName() + TEXT("_") + FString::FromInt(NameCounter);
					MaterialSlotName = FName(*MaterialSlotNameStr);
					NameCounter++;
				}
			}
			MaterialSlotNames.Add(MaterialSlotName);
			StaticMaterials.Add(FStaticMaterial(MaterialInterface, MaterialSlotName));
			int32 UniqueIndex = Unique_Materials_DEPRECATED.AddUnique(MaterialInterface);
#if WITH_EDITOR
			//We must cleanup the material list since we have a new way to build static mesh
			bCleanUpRedundantMaterialPostLoad = StaticMaterials.Num() > 1;
#endif
		}
		Materials_DEPRECATED.Empty();

	}
#endif // WITH_EDITORONLY_DATA


#if WITH_EDITOR
	bool bHasSpeedTreeWind = SpeedTreeWind.IsValid();
	if (Ar.CustomVer(FReleaseObjectVersion::GUID) < FReleaseObjectVersion::SpeedTreeBillboardSectionInfoFixup && bHasSpeedTreeWind)
	{
		// Ensure we have multiple tree LODs
		if (GetNumSourceModels() > 1)
		{
			// Look a the last LOD model and check its vertices
			const int32 LODIndex = GetNumSourceModels() - 1;
			FStaticMeshSourceModel& SourceModel = GetSourceModel(LODIndex);

			FRawMesh RawMesh;
			SourceModel.LoadRawMesh(RawMesh);

			// Billboard LOD is made up out of quads so check for this
			bool bQuadVertices = ((RawMesh.VertexPositions.Num() % 4) == 0);

			// If there is no section info for the billboard LOD make sure we add it
			uint32 Key = GetMeshMaterialKey(LODIndex, 0);
			bool bSectionInfoExists = GetSectionInfoMap().Map.Contains(Key);
			if (!bSectionInfoExists && bQuadVertices)
			{
				FMeshSectionInfo Info;
				// Assuming billboard material is added last
				Info.MaterialIndex = StaticMaterials.Num() - 1;
				GetSectionInfoMap().Set(LODIndex, 0, Info);
				GetOriginalSectionInfoMap().Set(LODIndex, 0, Info);
			}
		}
	}
#endif // WITH_EDITOR
}

bool UStaticMesh::IsPostLoadThreadSafe() const
{
	return false;
}

//
//	UStaticMesh::PostLoad
//
void UStaticMesh::PostLoad()
{
	LLM_SCOPE(ELLMTag::StaticMesh);
	Super::PostLoad();

#if WITH_EDITOR
	TRACE_CPUPROFILER_EVENT_SCOPE(UStaticMesh::PostLoad);

	if (GetNumSourceModels() > 0)
	{
		UStaticMesh* DistanceFieldReplacementMesh = GetSourceModel(0).BuildSettings.DistanceFieldReplacementMesh;
 
		if (DistanceFieldReplacementMesh)
		{
			DistanceFieldReplacementMesh->ConditionalPostLoad();
		}
		
		//TODO remove this code when FRawMesh will be removed
		//Fill the static mesh owner
		int32 NumLODs = GetNumSourceModels();
		for (int32 LODIndex = 0; LODIndex < NumLODs; ++LODIndex)
		{
			FStaticMeshSourceModel& SrcModel = GetSourceModel(LODIndex);
			SrcModel.StaticMeshOwner = this;
		}
	}

	if (!GetOutermost()->HasAnyPackageFlags(PKG_FilterEditorOnly))
	{
		// Needs to happen before 'CacheDerivedData'
		if (GetLinkerUE4Version() < VER_UE4_BUILD_SCALE_VECTOR)
		{
			int32 NumLODs = GetNumSourceModels();
			for (int32 LODIndex = 0; LODIndex < NumLODs; ++LODIndex)
			{
				FStaticMeshSourceModel& SrcModel = GetSourceModel(LODIndex);
				SrcModel.BuildSettings.BuildScale3D = FVector(SrcModel.BuildSettings.BuildScale_DEPRECATED);
			}
		}

		if (GetLinkerUE4Version() < VER_UE4_LIGHTMAP_MESH_BUILD_SETTINGS)
		{
			for (int32 i = 0; i < GetNumSourceModels(); i++)
			{
				GetSourceModel(i).BuildSettings.bGenerateLightmapUVs = false;
			}
		}

		if (GetLinkerUE4Version() < VER_UE4_MIKKTSPACE_IS_DEFAULT)
		{
			for (int32 i = 0; i < GetNumSourceModels(); ++i)
			{
				GetSourceModel(i).BuildSettings.bUseMikkTSpace = true;
			}
		}

		if (GetLinkerUE4Version() < VER_UE4_BUILD_MESH_ADJ_BUFFER_FLAG_EXPOSED)
		{
			FRawMesh TempRawMesh;
			uint32 TotalIndexCount = 0;

			for (int32 i = 0; i < GetNumSourceModels(); ++i)
			{
				// Access RawMesh directly instead of through the FStaticMeshSourceModel API,
				// because we don't want to perform an automatic conversion to MeshDescription at this point -
				// this will be done below in CacheDerivedData().
				// This is a path for legacy assets.
				if (!GetSourceModel(i).RawMeshBulkData->IsEmpty())
				{
					GetSourceModel(i).RawMeshBulkData->LoadRawMesh(TempRawMesh);
					TotalIndexCount += TempRawMesh.WedgeIndices.Num();
				}
			}

			for (int32 i = 0; i < GetNumSourceModels(); ++i)
			{
				GetSourceModel(i).BuildSettings.bBuildAdjacencyBuffer = (TotalIndexCount < 50000);
			}
		}

		// The LODGroup update on load must happen before CacheDerivedData so we don't have to rebuild it after
		if (GUpdateMeshLODGroupSettingsAtLoad && LODGroup != NAME_None)
		{
			SetLODGroup(LODGroup);
		}

		FixupMaterialSlotName();

		if (bIsBuiltAtRuntime)
		{
#if WITH_EDITOR
			// If built at runtime, but an editor build, we cache the mesh descriptions so that they can be rebuilt within the editor if necessary.
			// This is done through the fast build path for consistency
			TArray<const FMeshDescription*> MeshDescriptions;
			const int32 NumSourceModels = GetNumSourceModels();
			MeshDescriptions.Reserve(NumSourceModels);
			for (int32 SourceModelIndex = 0; SourceModelIndex < NumSourceModels; SourceModelIndex++)
			{
				MeshDescriptions.Add(GetMeshDescription(SourceModelIndex));
			}
			BuildFromMeshDescriptions(MeshDescriptions);
#endif
		}
		else
		{
		// This, among many other things, will build a MeshDescription from the legacy RawMesh if one has not already been serialized,
		// or, failing that, if there is not already one in the DDC. This will remain cached until the end of PostLoad(), upon which it
		// is then released, and can be reloaded on demand.
		CacheDerivedData();
		}

		//Fix up the material to remove redundant material, this is needed since the material refactor where we do not have anymore copy of the materials
		//in the materials list
		if (RenderData && bCleanUpRedundantMaterialPostLoad)
		{
			bool bMaterialChange = false;
			TArray<FStaticMaterial> CompactedMaterial;
			for (int32 LODIndex = 0; LODIndex < RenderData->LODResources.Num(); ++LODIndex)
			{
				if (RenderData->LODResources.IsValidIndex(LODIndex))
				{
					FStaticMeshLODResources& LOD = RenderData->LODResources[LODIndex];
					const int32 NumSections = LOD.Sections.Num();
					for (int32 SectionIndex = 0; SectionIndex < NumSections; ++SectionIndex)
					{
						const int32 MaterialIndex = LOD.Sections[SectionIndex].MaterialIndex;
						if (StaticMaterials.IsValidIndex(MaterialIndex))
						{
							if (LODIndex == 0)
							{
								//We do not compact LOD 0 material
								CompactedMaterial.Add(StaticMaterials[MaterialIndex]);
							}
							else
							{
								FMeshSectionInfo MeshSectionInfo = GetSectionInfoMap().Get(LODIndex, SectionIndex);
								int32 CompactedIndex = INDEX_NONE;
								if (StaticMaterials.IsValidIndex(MeshSectionInfo.MaterialIndex))
								{
									for (int32 CompactedMaterialIndex = 0; CompactedMaterialIndex < CompactedMaterial.Num(); ++CompactedMaterialIndex)
									{
										const FStaticMaterial& StaticMaterial = CompactedMaterial[CompactedMaterialIndex];
										if (StaticMaterials[MeshSectionInfo.MaterialIndex].MaterialInterface == StaticMaterial.MaterialInterface)
										{
											CompactedIndex = CompactedMaterialIndex;
											break;
										}
									}
								}

								if (CompactedIndex == INDEX_NONE)
								{
									CompactedIndex = CompactedMaterial.Add(StaticMaterials[MaterialIndex]);
								}
								if (MeshSectionInfo.MaterialIndex != CompactedIndex)
								{
									MeshSectionInfo.MaterialIndex = CompactedIndex;
									GetSectionInfoMap().Set(LODIndex, SectionIndex, MeshSectionInfo);
									bMaterialChange = true;
								}
							}
						}
					}
				}
			}
			//If we change some section material index or there is unused material, we must use the new compacted material list.
			if (bMaterialChange || CompactedMaterial.Num() < StaticMaterials.Num())
			{
				StaticMaterials.Empty(CompactedMaterial.Num());
				for (const FStaticMaterial &Material : CompactedMaterial)
				{
					StaticMaterials.Add(Material);
				}
				//Make sure the physic data is recompute
				if (BodySetup)
				{
					BodySetup->InvalidatePhysicsData();
				}
			}
			bCleanUpRedundantMaterialPostLoad = false;
		}

		if (RenderData && GStaticMeshesThatNeedMaterialFixup.Get(this))
		{
			FixupZeroTriangleSections();
		}
	}

	if (RenderData)
	{
		if (bSupportGpuUniformlyDistributedSampling)
		{
			// Initialise pointers to samplers
			for (FStaticMeshLODResources &LOD : RenderData->LODResources)
			{
				LOD.AreaWeightedSectionSamplersBuffer.Init(&LOD.AreaWeightedSectionSamplers);
			}
		}

		// check the MinLOD values are all within range
		bool bFixedMinLOD = false;
		int32 MinAvailableLOD = FMath::Max<int32>(RenderData->LODResources.Num() - 1, 0);
		if (!RenderData->LODResources.IsValidIndex(MinLOD.Default))
		{
			FFormatNamedArguments Arguments;
			Arguments.Add(TEXT("MinLOD"), FText::AsNumber(MinLOD.Default));
			Arguments.Add(TEXT("MinAvailLOD"), FText::AsNumber(MinAvailableLOD));
			FMessageLog("LoadErrors").Warning()
				->AddToken(FUObjectToken::Create(this))
				->AddToken(FTextToken::Create(FText::Format(LOCTEXT("LoadError_BadMinLOD", "Min LOD value of {MinLOD} is out of range 0..{MinAvailLOD} and has been adjusted to {MinAvailLOD}. Please verify and resave the asset."), Arguments)));

			MinLOD.Default = MinAvailableLOD;
			bFixedMinLOD = true;
		}
		for (TMap<FName, int32>::TIterator It(MinLOD.PerPlatform); It; ++It)
		{
			if (!RenderData->LODResources.IsValidIndex(It.Value()))
			{
				FFormatNamedArguments Arguments;
				Arguments.Add(TEXT("MinLOD"), FText::AsNumber(It.Value()));
				Arguments.Add(TEXT("MinAvailLOD"), FText::AsNumber(MinAvailableLOD));
				Arguments.Add(TEXT("Platform"), FText::FromString(It.Key().ToString()));
				FMessageLog("LoadErrors").Warning()
					->AddToken(FUObjectToken::Create(this))
					->AddToken(FTextToken::Create(FText::Format(LOCTEXT("LoadError_BadMinLODOverride", "Min LOD override of {MinLOD} for {Platform} is out of range 0..{MinAvailLOD} and has been adjusted to {MinAvailLOD}. Please verify and resave the asset."), Arguments)));

				It.Value() = MinAvailableLOD;
				bFixedMinLOD = true;
			}
		}
		if (bFixedMinLOD)
		{
			FMessageLog("LoadErrors").Open();
		}
	}


#endif // #if WITH_EDITOR

#if WITH_EDITORONLY_DATA
	if (GetLinkerCustomVersion(FRenderingObjectVersion::GUID) < FRenderingObjectVersion::FixedMeshUVDensity)
	{
		UpdateUVChannelData(true);
	}
#endif

	EnforceLightmapRestrictions();

	if( FApp::CanEverRender() && !HasAnyFlags(RF_ClassDefaultObject) )
	{
		InitResources();
	}
	else
	{
		// Update any missing data when cooking.
		UpdateUVChannelData(false);
#if WITH_EDITOR
		if (RenderData)
		{
			RenderData->ResolveSectionInfo(this);
		}
#endif
	}

#if WITH_EDITOR
	// Fix extended bounds if needed
	const int32 CustomVersion = GetLinkerCustomVersion(FReleaseObjectVersion::GUID);
	if (GetLinkerUE4Version() < VER_UE4_STATIC_MESH_EXTENDED_BOUNDS || CustomVersion < FReleaseObjectVersion::StaticMeshExtendedBoundsFix)
	{
		CalculateExtendedBounds();
	}
	//Conversion of LOD distance need valid bounds it must be call after the extended Bounds fixup
	// Only required in an editor build as other builds process this in a different place
	if (bRequiresLODDistanceConversion)
	{
		// Convert distances to Display Factors
		ConvertLegacyLODDistance();
	}

	if (bRequiresLODScreenSizeConversion)
	{
		// Convert screen area to screen size
		ConvertLegacyLODScreenArea();
	}

	//Always redo the whole SectionInfoMap to be sure it contain only valid data
	//This will reuse everything valid from the just serialize SectionInfoMap.
	FMeshSectionInfoMap TempOldSectionInfoMap = GetSectionInfoMap();
	GetSectionInfoMap().Clear();
	for (int32 LODResourceIndex = 0; LODResourceIndex < RenderData->LODResources.Num(); ++LODResourceIndex)
	{
		FStaticMeshLODResources& LOD = RenderData->LODResources[LODResourceIndex];
		for (int32 SectionIndex = 0; SectionIndex < LOD.Sections.Num(); ++SectionIndex)
		{
			if (TempOldSectionInfoMap.IsValidSection(LODResourceIndex, SectionIndex))
			{
				FMeshSectionInfo Info = TempOldSectionInfoMap.Get(LODResourceIndex, SectionIndex);
				if (StaticMaterials.IsValidIndex(Info.MaterialIndex))
				{
					//Reuse the valid data that come from the serialize
					GetSectionInfoMap().Set(LODResourceIndex, SectionIndex, Info);
				}
				else
				{
					//Use the render data material index, but keep the flags (collision, shadow...)
					const int32 MaterialIndex = LOD.Sections[SectionIndex].MaterialIndex;
					if (StaticMaterials.IsValidIndex(MaterialIndex))
					{
						Info.MaterialIndex = MaterialIndex;
						GetSectionInfoMap().Set(LODResourceIndex, SectionIndex, Info);
					}
				}
			}
			else
			{
				//Create a new SectionInfoMap from the render data
				const int32 MaterialIndex = LOD.Sections[SectionIndex].MaterialIndex;
				if (StaticMaterials.IsValidIndex(MaterialIndex))
				{
					GetSectionInfoMap().Set(LODResourceIndex, SectionIndex, FMeshSectionInfo(MaterialIndex));
				}
			}
			//Make sure the OriginalSectionInfoMap has some information, the post load only add missing slot, this data should be set when importing/re-importing the asset
			if (!GetOriginalSectionInfoMap().IsValidSection(LODResourceIndex, SectionIndex))
			{
				GetOriginalSectionInfoMap().Set(LODResourceIndex, SectionIndex, GetSectionInfoMap().Get(LODResourceIndex, SectionIndex));
			}
		}
	}
#endif // #if WITH_EDITOR

	// We want to always have a BodySetup, its used for per-poly collision as well
	if(BodySetup == NULL)
	{
		CreateBodySetup();
	}

#if WITH_EDITOR
	// Release cached mesh descriptions until they are loaded on demand
	ClearMeshDescriptions();
#endif

	CreateNavCollision();
}


void UStaticMesh::BuildFromMeshDescription(const FMeshDescription& MeshDescription, FStaticMeshLODResources& LODResources)
{
	FStaticMeshConstAttributes MeshDescriptionAttributes(MeshDescription);

	// Fill vertex buffers

	int32 NumVertexInstances = MeshDescription.VertexInstances().GetArraySize();
	int32 NumTriangles = MeshDescription.Triangles().Num();

	if (NumVertexInstances == 0 || NumTriangles == 0)
	{
		return;
	}

	TArray<FStaticMeshBuildVertex> StaticMeshBuildVertices;
	StaticMeshBuildVertices.SetNum(NumVertexInstances);

	TVertexAttributesConstRef<FVector> VertexPositions = MeshDescriptionAttributes.GetVertexPositions();
	TVertexInstanceAttributesConstRef<FVector> VertexInstanceNormals = MeshDescriptionAttributes.GetVertexInstanceNormals();
	TVertexInstanceAttributesConstRef<FVector> VertexInstanceTangents = MeshDescriptionAttributes.GetVertexInstanceTangents();
	TVertexInstanceAttributesConstRef<float> VertexInstanceBinormalSigns = MeshDescriptionAttributes.GetVertexInstanceBinormalSigns();
	TVertexInstanceAttributesConstRef<FVector4> VertexInstanceColors = MeshDescriptionAttributes.GetVertexInstanceColors();
	TVertexInstanceAttributesConstRef<FVector2D> VertexInstanceUVs = MeshDescriptionAttributes.GetVertexInstanceUVs();

	for (FVertexInstanceID VertexInstanceID : MeshDescription.VertexInstances().GetElementIDs())
	{
		FStaticMeshBuildVertex& StaticMeshVertex = StaticMeshBuildVertices[VertexInstanceID.GetValue()];

		StaticMeshVertex.Position = VertexPositions[MeshDescription.GetVertexInstanceVertex(VertexInstanceID)];
		StaticMeshVertex.TangentX = VertexInstanceTangents[VertexInstanceID];
		StaticMeshVertex.TangentY = FVector::CrossProduct(VertexInstanceNormals[VertexInstanceID], VertexInstanceTangents[VertexInstanceID]).GetSafeNormal() * VertexInstanceBinormalSigns[VertexInstanceID];
		StaticMeshVertex.TangentZ = VertexInstanceNormals[VertexInstanceID];

		for (int32 UVIndex = 0; UVIndex < VertexInstanceUVs.GetNumIndices(); ++UVIndex)
		{
			StaticMeshVertex.UVs[UVIndex] = VertexInstanceUVs.Get(VertexInstanceID, UVIndex);
		}
	}

	bool bHasVertexColors = false;
	if (VertexInstanceColors.IsValid())
	{
		for (FVertexInstanceID VertexInstanceID : MeshDescription.VertexInstances().GetElementIDs())
		{
			FStaticMeshBuildVertex& StaticMeshVertex = StaticMeshBuildVertices[VertexInstanceID.GetValue()];

			FLinearColor Color(VertexInstanceColors[VertexInstanceID]);
			if (Color != FLinearColor::White)
			{
				bHasVertexColors = true;
			}

			StaticMeshVertex.Color = Color.ToFColor(true);
		}
	}

	LODResources.VertexBuffers.PositionVertexBuffer.Init(StaticMeshBuildVertices);
	LODResources.VertexBuffers.StaticMeshVertexBuffer.Init(StaticMeshBuildVertices, VertexInstanceUVs.GetNumIndices());

	FColorVertexBuffer& ColorVertexBuffer = LODResources.VertexBuffers.ColorVertexBuffer;
	if (bHasVertexColors)
	{
		ColorVertexBuffer.Init(StaticMeshBuildVertices);
	}
	else
	{
		ColorVertexBuffer.InitFromSingleColor(FColor::White, NumVertexInstances);
	}

	// Fill index buffer and sections array

	int32 NumPolygonGroups = MeshDescription.PolygonGroups().Num();

	TPolygonGroupAttributesConstRef<FName> MaterialSlotNames = MeshDescriptionAttributes.GetPolygonGroupMaterialSlotNames();

	TArray<uint32> IndexBuffer;
	IndexBuffer.SetNumZeroed(NumTriangles * 3);

	TArray<FStaticMeshSection>& Sections = LODResources.Sections;

	int32 SectionIndex = 0;
	int32 IndexBufferIndex = 0;
	EIndexBufferStride::Type IndexBufferStride = EIndexBufferStride::Force16Bit;

	for (FPolygonGroupID PolygonGroupID : MeshDescription.PolygonGroups().GetElementIDs())
	{
		// Skip empty polygon groups - we do not want to build empty sections
		if (MeshDescription.GetNumPolygonGroupPolygons(PolygonGroupID) == 0)
		{
			continue;
		}

		FStaticMeshSection& Section = Sections.AddDefaulted_GetRef();
		Section.FirstIndex = IndexBufferIndex;

		int32 TriangleCount = 0;
		uint32 MinVertexIndex = TNumericLimits<uint32>::Max();
		uint32 MaxVertexIndex = TNumericLimits<uint32>::Min();

		for (FPolygonID PolygonID : MeshDescription.GetPolygonGroupPolygons(PolygonGroupID))
		{
			for (FTriangleID TriangleID : MeshDescription.GetPolygonTriangleIDs(PolygonID))
			{
				for (FVertexInstanceID TriangleVertexInstanceIDs : MeshDescription.GetTriangleVertexInstances(TriangleID))
				{
					uint32 VertexIndex = static_cast<uint32>(TriangleVertexInstanceIDs.GetValue());
					MinVertexIndex = FMath::Min(MinVertexIndex, VertexIndex);
					MaxVertexIndex = FMath::Max(MaxVertexIndex, VertexIndex);
					IndexBuffer[IndexBufferIndex] = VertexIndex;
					IndexBufferIndex++;
				}

				TriangleCount++;
			}
		}

		Section.NumTriangles = TriangleCount;
		Section.MinVertexIndex = MinVertexIndex;
		Section.MaxVertexIndex = MaxVertexIndex;

		const int32 MaterialIndex = StaticMaterials.IndexOfByPredicate(
			[&MaterialSlotName = MaterialSlotNames[PolygonGroupID]](const FStaticMaterial& StaticMaterial) { return StaticMaterial.MaterialSlotName == MaterialSlotName; }
		);

		Section.MaterialIndex = MaterialIndex;
		Section.bEnableCollision = true;
		Section.bCastShadow = true;

		if (MaxVertexIndex > TNumericLimits<uint16>::Max())
		{
			IndexBufferStride = EIndexBufferStride::Force32Bit;
		}

		SectionIndex++;
	}
	check(IndexBufferIndex == NumTriangles * 3);

	LODResources.IndexBuffer.SetIndices(IndexBuffer, IndexBufferStride);

	// Fill depth only index buffer

	TArray<uint32> DepthOnlyIndexBuffer(IndexBuffer);
	for (uint32& Index : DepthOnlyIndexBuffer)
	{
		// Compress all vertex instances into the same instance for each vertex
		Index = MeshDescription.GetVertexVertexInstances(MeshDescription.GetVertexInstanceVertex(FVertexInstanceID(Index)))[0].GetValue();
	}

	LODResources.bHasDepthOnlyIndices = true;
	LODResources.DepthOnlyIndexBuffer.SetIndices(DepthOnlyIndexBuffer, IndexBufferStride);

	// Fill reversed index buffer
	TArray<uint32> ReversedIndexBuffer(IndexBuffer);
	for (int32 ReversedIndexBufferIndex = 0; ReversedIndexBufferIndex < IndexBuffer.Num(); ReversedIndexBufferIndex += 3)
	{
		Swap(ReversedIndexBuffer[ReversedIndexBufferIndex + 0], ReversedIndexBuffer[ReversedIndexBufferIndex + 2]);
	}

	LODResources.AdditionalIndexBuffers = new FAdditionalStaticMeshIndexBuffers();
	LODResources.bHasReversedIndices = true;
	LODResources.AdditionalIndexBuffers->ReversedIndexBuffer.SetIndices(ReversedIndexBuffer, IndexBufferStride);

	// Fill reversed depth index buffer
	TArray<uint32> ReversedDepthOnlyIndexBuffer(DepthOnlyIndexBuffer);
	for (int32 ReversedIndexBufferIndex = 0; ReversedIndexBufferIndex < IndexBuffer.Num(); ReversedIndexBufferIndex += 3)
	{
		Swap(ReversedDepthOnlyIndexBuffer[ReversedIndexBufferIndex + 0], ReversedDepthOnlyIndexBuffer[ReversedIndexBufferIndex + 2]);
	}

	LODResources.bHasReversedDepthOnlyIndices = true;
	LODResources.AdditionalIndexBuffers->ReversedDepthOnlyIndexBuffer.SetIndices(ReversedIndexBuffer, IndexBufferStride);

	LODResources.bHasAdjacencyInfo = false;
}


UStaticMeshDescription* UStaticMesh::CreateStaticMeshDescription(UObject* Outer)
{
	if (Outer == nullptr)
	{
		Outer = GetTransientPackage();
	}

	UStaticMeshDescription* StaticMeshDescription = NewObject<UStaticMeshDescription>(Outer, NAME_None, RF_Transient);
	StaticMeshDescription->RegisterAttributes();
	return StaticMeshDescription;
}


void UStaticMesh::BuildFromStaticMeshDescriptions(const TArray<UStaticMeshDescription*>& StaticMeshDescriptions)
{
	TArray<const FMeshDescription*> MeshDescriptions;
	MeshDescriptions.Reserve(StaticMeshDescriptions.Num());

	for (UStaticMeshDescription* StaticMeshDescription : StaticMeshDescriptions)
	{
		MeshDescriptions.Emplace(&StaticMeshDescription->GetMeshDescription());
	}

	BuildFromMeshDescriptions(MeshDescriptions);
}


bool UStaticMesh::BuildFromMeshDescriptions(const TArray<const FMeshDescription*>& MeshDescriptions)
{
	// Set up

	bIsBuiltAtRuntime = true;
	NeverStream = true;

	TOptional<FStaticMeshComponentRecreateRenderStateContext> RecreateRenderStateContext;
	
	if (RenderData.IsValid())
	{
		const bool bInvalidateLighting = true;
		const bool bRefreshBounds = true;
		RecreateRenderStateContext = FStaticMeshComponentRecreateRenderStateContext(this, bInvalidateLighting, bRefreshBounds);
	}

	ReleaseResources();
	ReleaseResourcesFence.Wait();

	RenderData = MakeUnique<FStaticMeshRenderData>();
	RenderData->AllocateLODResources(MeshDescriptions.Num());

	// Build render data from each mesh description

	int32 LODIndex = 0;
	for (const FMeshDescription* MeshDescriptionPtr : MeshDescriptions)
	{
#if WITH_EDITOR
		// Editor builds cache the mesh description so that it can be preserved during map reloads etc
		SetNumSourceModels(MeshDescriptions.Num());
		CreateMeshDescription(LODIndex, *MeshDescriptionPtr);
		CommitMeshDescription(LODIndex);
#endif
		check(MeshDescriptionPtr != nullptr);
		FStaticMeshLODResources& LODResources = RenderData->LODResources[LODIndex];

		BuildFromMeshDescription(*MeshDescriptionPtr, LODResources);

#if WITH_EDITOR
		for (int32 SectionIndex = 0; SectionIndex < LODResources.Sections.Num(); SectionIndex++)
		{
			const FStaticMeshSection& StaticMeshSection = LODResources.Sections[SectionIndex];
			FMeshSectionInfo SectionInfo;
			SectionInfo.MaterialIndex = StaticMeshSection.MaterialIndex;
			SectionInfo.bEnableCollision = StaticMeshSection.bEnableCollision;
			SectionInfo.bCastShadow = StaticMeshSection.bCastShadow;
			GetSectionInfoMap().Set(LODIndex, SectionIndex, SectionInfo);
		}
#endif
		LODIndex++;
	}

	InitResources();

	// Set up RenderData bounds and LOD data
	RenderData->Bounds = MeshDescriptions[0]->GetBounds();
	CalculateExtendedBounds();

	for (int32 LOD = 0; LOD < MeshDescriptions.Num(); ++LOD)
	{
		// @todo: some way of customizing LOD screen size and/or calculate it based on mesh bounds
		if (true)
		{
			const float LODPowerBase = 0.75f;
			RenderData->ScreenSize[LOD].Default = FMath::Pow(LODPowerBase, LOD);
		}
		else
		{
			// Possible model for flexible LODs
			const float MaxDeviation = 100.0f; // specify
			const float PixelError = SMALL_NUMBER;
			const float ViewDistance = (MaxDeviation * 960.0f) / PixelError;

			// Generate a projection matrix.
			const float HalfFOV = PI * 0.25f;
			const float ScreenWidth = 1920.0f;
			const float ScreenHeight = 1080.0f;
			const FPerspectiveMatrix ProjMatrix(HalfFOV, ScreenWidth, ScreenHeight, 1.0f);

			RenderData->ScreenSize[LOD].Default = ComputeBoundsScreenSize(FVector::ZeroVector, RenderData->Bounds.SphereRadius, FVector(0.0f, 0.0f, ViewDistance + RenderData->Bounds.SphereRadius), ProjMatrix);
		}
	}

	// Set up physics-related data
	CreateBodySetup();
	check(BodySetup);
	BodySetup->InvalidatePhysicsData();
	BodySetup->CreatePhysicsMeshes();

	return true;
}


bool UStaticMesh::CanBeClusterRoot() const
{
	return false;
}

int32 UStaticMesh::GetLODGroupForStreaming() const
{
	// TODO: mesh LOD streaming may need to know LOD group settings
	return 0;
}

int32 UStaticMesh::GetNumMipsForStreaming() const
{
	check(RenderData);
	return GetNumLODs();
}

int32 UStaticMesh::GetNumNonStreamingMips() const
{
	check(RenderData);
	return RenderData->NumInlinedLODs;
}

int32 UStaticMesh::CalcNumOptionalMips() const
{
#if !WITH_EDITOR
	return MinLOD.Default;
#else
	int32 NumOptionalLODs = 0;
	if (RenderData)
	{
		const TIndirectArray<FStaticMeshLODResources>& LODResources = RenderData->LODResources;
		for (int32 Idx = 0; Idx < LODResources.Num(); ++Idx)
		{
			const FStaticMeshLODResources& Resource = LODResources[Idx];
			if (Resource.bIsOptionalLOD)
			{
				++NumOptionalLODs;
			}
			else
			{
				break;
			}
		}
	}
	return NumOptionalLODs;
#endif
}

int32 UStaticMesh::CalcCumulativeLODSize(int32 NumLODs) const
{
	uint32 Accum = 0;
	const int32 LODCount = GetNumLODs();
	const int32 LastLODIdx = LODCount - NumLODs;
	for (int32 Idx = LODCount - 1; Idx >= LastLODIdx; --Idx)
	{
		Accum += RenderData->LODResources[Idx].BuffersSize;
	}
	return Accum;
}

bool UStaticMesh::GetMipDataFilename(const int32 MipIndex, FString& OutBulkDataFilename) const
{
	// TODO: this is slow. Should cache the name once per mesh
	FString PackageName = GetOutermost()->FileName.ToString();
	// Handle name redirection and localization
	const FCoreRedirectObjectName RedirectedName =
		FCoreRedirects::GetRedirectedName(
			ECoreRedirectFlags::Type_Package,
			FCoreRedirectObjectName(NAME_None, NAME_None, *PackageName));
	FString LocalizedName;
	LocalizedName = FPackageName::GetDelegateResolvedPackagePath(RedirectedName.PackageName.ToString());
	LocalizedName = FPackageName::GetLocalizedPackagePath(LocalizedName);
	bool bSucceed = FPackageName::DoesPackageExist(LocalizedName, nullptr, &OutBulkDataFilename);
	check(bSucceed);
	OutBulkDataFilename = FPaths::ChangeExtension(OutBulkDataFilename, MipIndex < MinLOD.Default ? TEXT(".uptnl") : TEXT(".ubulk"));
	check(MipIndex < MinLOD.Default || IFileManager::Get().FileExists(*OutBulkDataFilename));
	return true;
}

bool UStaticMesh::DoesMipDataExist(const int32 MipIndex) const
{
	check(MipIndex < MinLOD.Default);

#if !USE_BULKDATA_STREAMING_TOKEN	
	return RenderData->LODResources[MipIndex].StreamingBulkData.DoesExist();
#else
	checkf(false, TEXT("Should not be possible to reach this path, if USE_NEW_BULKDATA is enabled then USE_BULKDATA_STREAMING_TOKEN should be disabled!"));
	return false;
#endif
}

bool UStaticMesh::IsReadyForStreaming() const
{
	return RenderData && RenderData->bReadyForStreaming;
}

int32 UStaticMesh::GetNumResidentMips() const
{
	check(RenderData);
	return GetNumLODs() - RenderData->CurrentFirstLODIdx;
}

int32 UStaticMesh::GetNumRequestedMips() const
{
	if (PendingUpdate && !PendingUpdate->IsCancelled())
	{
		return PendingUpdate->GetNumRequestedMips();
	}
	else
	{
		return GetCachedNumResidentLODs();
	}
}

bool UStaticMesh::CancelPendingMipChangeRequest()
{
	if (PendingUpdate)
	{
		if (!PendingUpdate->IsCancelled())
		{
			PendingUpdate->Abort();
		}
		return true;
	}
	return false;
}

bool UStaticMesh::HasPendingUpdate() const
{
	return !!PendingUpdate;
}

bool UStaticMesh::IsPendingUpdateLocked() const 
{ 
	return PendingUpdate && PendingUpdate->IsLocked(); 
}

bool UStaticMesh::StreamOut(int32 NewMipCount)
{
	check(IsInGameThread());
	if (bIsStreamable && !PendingUpdate && RenderData.IsValid() && RenderData->bReadyForStreaming && NewMipCount < GetNumResidentMips())
	{
		PendingUpdate = new FStaticMeshStreamOut(this, NewMipCount);
		return !PendingUpdate->IsCancelled();
	}
	return false;
}

bool UStaticMesh::StreamIn(int32 NewMipCount, bool bHighPrio)
{
	check(IsInGameThread());
	if (bIsStreamable && !PendingUpdate && RenderData.IsValid() && RenderData->bReadyForStreaming && NewMipCount > GetNumResidentMips())
	{
#if WITH_EDITOR
		if (FPlatformProperties::HasEditorOnlyData())
		{
			if (GRHISupportsAsyncTextureCreation)
			{
				PendingUpdate = new FStaticMeshStreamIn_DDC_Async(this, NewMipCount);
			}
			else
			{
				PendingUpdate = new FStaticMeshStreamIn_DDC_RenderThread(this, NewMipCount);
			}
		}
		else
#endif
		{
			if (GRHISupportsAsyncTextureCreation)
			{
				PendingUpdate = new FStaticMeshStreamIn_IO_Async(this, NewMipCount, bHighPrio);
			}
			else
			{
				PendingUpdate = new FStaticMeshStreamIn_IO_RenderThread(this, NewMipCount, bHighPrio);
			}
		}
		return !PendingUpdate->IsCancelled();
	}
	return false;
}

bool UStaticMesh::UpdateStreamingStatus(bool bWaitForMipFading)
{
	// if resident and requested mip counts match then no pending request is in flight
	if (PendingUpdate)
	{
		if (IsEngineExitRequested() || !RenderData)
		{
			PendingUpdate->Abort();
		}

		// When there is no renderthread, allow the gamethread to tick as the renderthread.
		FRenderAssetUpdate::EThreadType TickThread = GIsThreadedRendering ? FRenderAssetUpdate::TT_None : FRenderAssetUpdate::TT_Render;
		if (HasAnyFlags(RF_BeginDestroyed) && PendingUpdate->GetRelevantThread() == FRenderAssetUpdate::TT_Async)
		{
			// To avoid async tasks from timing out the GC, we tick as Async to force completion if this is relevant.
			// This could lead the asset from releasing the PendingUpdate, which will be deleted once the async task completes.
			TickThread = FRenderAssetUpdate::TT_GameRunningAsync;
		}
		PendingUpdate->Tick(TickThread);

		if (!PendingUpdate->IsCompleted())
		{
			return true;
		}

#if WITH_EDITOR
		const bool bRebuildPlatformData = PendingUpdate->DDCIsInvalid() && !IsPendingKillOrUnreachable();
#endif

		PendingUpdate.SafeRelease();

#if WITH_EDITOR
		if (GIsEditor)
		{
			// When all the requested mips are streamed in, generate an empty property changed event, to force the
			// ResourceSize asset registry tag to be recalculated.
			FPropertyChangedEvent EmptyPropertyChangedEvent(nullptr);
			FCoreUObjectDelegates::OnObjectPropertyChanged.Broadcast(this, EmptyPropertyChangedEvent);

			// We can't load the source art from a bulk data object if the mesh itself is pending kill because the linker will have been detached.
			// In this case we don't rebuild the data and instead let the streaming request be cancelled. This will let the garbage collector finish
			// destroying the object.
			if (bRebuildPlatformData)
			{
				// TODO: force rebuild even if DDC keys match
				ITargetPlatformManagerModule& TargetPlatformManager = GetTargetPlatformManagerRef();
				ITargetPlatform* TargetPlatform = TargetPlatformManager.GetRunningTargetPlatform();
				check(TargetPlatform);
				const FStaticMeshLODSettings& LODSettings = TargetPlatform->GetStaticMeshLODSettings();
				RenderData->Cache(this, LODSettings);
				// @TODO this can not be called from this callstack since the entry needs to be removed completely from the streamer.
				// UpdateResource();
			}
		}
#endif
	}

	// TODO: LOD fading?

	return false;
}

void UStaticMesh::LinkStreaming()
{
	if (!IsTemplate() && IStreamingManager::Get().IsTextureStreamingEnabled() && IsStreamingRenderAsset(this))
	{
		IStreamingManager::Get().GetTextureStreamingManager().AddStreamingRenderAsset(this);
	}
	else
	{
		StreamingIndex = INDEX_NONE;
	}
}

void UStaticMesh::UnlinkStreaming()
{
	if (!IsTemplate() && IStreamingManager::Get().IsTextureStreamingEnabled())
	{
		IStreamingManager::Get().GetTextureStreamingManager().RemoveStreamingRenderAsset(this);
	}
}

void UStaticMesh::CancelAllPendingStreamingActions()
{
	FlushRenderingCommands();

	for (TObjectIterator<UStaticMesh> It; It; ++It)
	{
		UStaticMesh* StaticMesh = *It;
		StaticMesh->CancelPendingMipChangeRequest();
	}

	FlushRenderingCommands();
}

//
//	UStaticMesh::GetDesc
//

/** 
 * Returns a one line description of an object for viewing in the thumbnail view of the generic browser
 */
FString UStaticMesh::GetDesc()
{
	int32 NumTris = 0;
	int32 NumVerts = 0;
	int32 NumLODs = RenderData ? RenderData->LODResources.Num() : 0;
	if (NumLODs > 0)
	{
		NumTris = RenderData->LODResources[0].GetNumTriangles();
		NumVerts = RenderData->LODResources[0].GetNumVertices();
	}
	return FString::Printf(
		TEXT("%d LODs, %d Tris, %d Verts"),
		NumLODs,
		NumTris,
		NumVerts
		);
}


static int32 GetCollisionVertIndexForMeshVertIndex(int32 MeshVertIndex, TMap<int32, int32>& MeshToCollisionVertMap, TArray<FVector>& OutPositions, TArray< TArray<FVector2D> >& OutUVs, FPositionVertexBuffer& InPosVertBuffer, FStaticMeshVertexBuffer& InVertBuffer)
{
	int32* CollisionIndexPtr = MeshToCollisionVertMap.Find(MeshVertIndex);
	if (CollisionIndexPtr != nullptr)
	{
		return *CollisionIndexPtr;
	}
	else
	{
		// Copy UVs for vert if desired
		for (int32 ChannelIdx = 0; ChannelIdx < OutUVs.Num(); ChannelIdx++)
		{
			check(OutPositions.Num() == OutUVs[ChannelIdx].Num());
			OutUVs[ChannelIdx].Add(InVertBuffer.GetVertexUV(MeshVertIndex, ChannelIdx));
		}

		// Copy position
		int32 CollisionVertIndex = OutPositions.Add(InPosVertBuffer.VertexPosition(MeshVertIndex));

		// Add indices to map
		MeshToCollisionVertMap.Add(MeshVertIndex, CollisionVertIndex);

		return CollisionVertIndex;
	}
}

bool UStaticMesh::GetPhysicsTriMeshData(struct FTriMeshCollisionData* CollisionData, bool bInUseAllTriData)
{
#if WITH_EDITORONLY_DATA
	if (ComplexCollisionMesh && ComplexCollisionMesh != this)
	{
		return ComplexCollisionMesh->GetPhysicsTriMeshData(CollisionData, bInUseAllTriData);
	}
#else // #if WITH_EDITORONLY_DATA
	// the static mesh needs to be tagged for CPUAccess in order to access TriMeshData in runtime mode : 
	if (!bAllowCPUAccess)
	{
		UE_LOG(LogStaticMesh, Warning, TEXT("UStaticMesh::GetPhysicsTriMeshData: Triangle data from '%s' cannot be accessed at runtime on a mesh that isn't flagged as Allow CPU Access. This asset needs to be flagged as such (in the Advanced section)."), *GetFullName());
		return false;
	}

	// without editor data, we can't selectively generate a physics mesh for a given LOD index (we're missing access to GetSectionInfoMap()) so force bInUseAllTriData in order to use LOD index 0
	bInUseAllTriData = true;
#endif // #if !WITH_EDITORONLY_DATA

	check(HasValidRenderData());

	// Get the LOD level to use for collision
	// Always use 0 if asking for 'all tri data'
	const int32 UseLODIndex = bInUseAllTriData ? 0 : FMath::Clamp(LODForCollision, 0, RenderData->LODResources.Num()-1);

	FStaticMeshLODResources& LOD = RenderData->LODResources[UseLODIndex];

	FIndexArrayView Indices = LOD.IndexBuffer.GetArrayView();

	TMap<int32, int32> MeshToCollisionVertMap; // map of static mesh verts to collision verts

	// If the mesh enables physical material masks, override the physics setting since UVs are always required 
	// bool bCopyUVs = GetEnablePhysicalMaterialMask() || UPhysicsSettings::Get()->bSupportUVFromHitResults; // See if we should copy UVs
	bool bCopyUVs = UPhysicsSettings::Get()->bSupportUVFromHitResults; // See if we should copy UVs

	// If copying UVs, allocate array for storing them
	if (bCopyUVs)
	{
		CollisionData->UVs.AddZeroed(LOD.GetNumTexCoords());
	}

	for(int32 SectionIndex = 0; SectionIndex < LOD.Sections.Num(); ++SectionIndex)
	{
		const FStaticMeshSection& Section = LOD.Sections[SectionIndex];

#if WITH_EDITORONLY_DATA
		// we can only use GetSectionInfoMap() in WITH_EDITORONLY_DATA mode, otherwise, assume bInUseAllTriData :
		if (bInUseAllTriData || GetSectionInfoMap().Get(UseLODIndex, SectionIndex).bEnableCollision)
#else // #if WITH_EDITORONLY_DATA
		check(bInUseAllTriData && bAllowCPUAccess);
#endif // #if !WITH_EDITORONLY_DATA
		{
			const uint32 OnePastLastIndex  = Section.FirstIndex + Section.NumTriangles*3;

			for (uint32 TriIdx = Section.FirstIndex; TriIdx < OnePastLastIndex; TriIdx += 3)
			{
				FTriIndices TriIndex;
				TriIndex.v0 = GetCollisionVertIndexForMeshVertIndex(Indices[TriIdx +0], MeshToCollisionVertMap, CollisionData->Vertices, CollisionData->UVs, LOD.VertexBuffers.PositionVertexBuffer, LOD.VertexBuffers.StaticMeshVertexBuffer);
				TriIndex.v1 = GetCollisionVertIndexForMeshVertIndex(Indices[TriIdx +1], MeshToCollisionVertMap, CollisionData->Vertices, CollisionData->UVs, LOD.VertexBuffers.PositionVertexBuffer, LOD.VertexBuffers.StaticMeshVertexBuffer);
				TriIndex.v2 = GetCollisionVertIndexForMeshVertIndex(Indices[TriIdx +2], MeshToCollisionVertMap, CollisionData->Vertices, CollisionData->UVs, LOD.VertexBuffers.PositionVertexBuffer, LOD.VertexBuffers.StaticMeshVertexBuffer);

				CollisionData->Indices.Add(TriIndex);
				CollisionData->MaterialIndices.Add(Section.MaterialIndex);
			}
		}
	}
	CollisionData->bFlipNormals = true;
	
	// We only have a valid TriMesh if the CollisionData has vertices AND indices. For meshes with disabled section collision, it
	// can happen that the indices will be empty, in which case we do not want to consider that as valid trimesh data
	return CollisionData->Vertices.Num() > 0 && CollisionData->Indices.Num() > 0;
}

bool UStaticMesh::ContainsPhysicsTriMeshData(bool bInUseAllTriData) const 
{
#if WITH_EDITORONLY_DATA
	if (ComplexCollisionMesh && ComplexCollisionMesh != this)
	{
		return ComplexCollisionMesh->ContainsPhysicsTriMeshData(bInUseAllTriData);
	}
#else // #if WITH_EDITORONLY_DATA
	// without editor data, we can't selectively generate a physics mesh for a given LOD index (we're missing access to GetSectionInfoMap()) so force bInUseAllTriData in order to use LOD index 0
	bInUseAllTriData = true;
#endif // #if !WITH_EDITORONLY_DATA
	
	if(RenderData == nullptr || RenderData->LODResources.Num() == 0)
	{
		return false;
	}

	// Get the LOD level to use for collision
	// Always use 0 if asking for 'all tri data'
	const int32 UseLODIndex = bInUseAllTriData ? 0 : FMath::Clamp(LODForCollision, 0, RenderData->LODResources.Num() - 1);

	if (RenderData->LODResources[UseLODIndex].VertexBuffers.PositionVertexBuffer.GetNumVertices() > 0)
	{
		// Get the LOD level to use for collision
		FStaticMeshLODResources& LOD = RenderData->LODResources[UseLODIndex];
		for (int32 SectionIndex = 0; SectionIndex < LOD.Sections.Num(); ++SectionIndex)
		{
			const FStaticMeshSection& Section = LOD.Sections[SectionIndex];
#if WITH_EDITORONLY_DATA
			// we can only use GetSectionInfoMap() in WITH_EDITORONLY_DATA mode, otherwise, assume bInUseAllTriData :
			if ((bInUseAllTriData || GetSectionInfoMap().Get(UseLODIndex, SectionIndex).bEnableCollision) && Section.NumTriangles > 0)
			{
				return true;
			}
#else // #if WITH_EDITORONLY_DATA
			return true;
#endif // #if WITH_EDITORONLY_DATA
		}
	}
	return false; 
}

void UStaticMesh::GetMeshId(FString& OutMeshId)
{
#if WITH_EDITORONLY_DATA
	if (RenderData)
	{
		OutMeshId = RenderData->DerivedDataKey;
	}
#endif
}

void UStaticMesh::AddAssetUserData(UAssetUserData* InUserData)
{
	if(InUserData != NULL)
	{
		UAssetUserData* ExistingData = GetAssetUserDataOfClass(InUserData->GetClass());
		if(ExistingData != NULL)
		{
			AssetUserData.Remove(ExistingData);
		}
		AssetUserData.Add(InUserData);
	}
}

UAssetUserData* UStaticMesh::GetAssetUserDataOfClass(TSubclassOf<UAssetUserData> InUserDataClass)
{
	for(int32 DataIdx=0; DataIdx<AssetUserData.Num(); DataIdx++)
	{
		UAssetUserData* Datum = AssetUserData[DataIdx];
		if(Datum != NULL && Datum->IsA(InUserDataClass))
		{
			return Datum;
		}
	}
	return NULL;
}

void UStaticMesh::RemoveUserDataOfClass(TSubclassOf<UAssetUserData> InUserDataClass)
{
	for(int32 DataIdx=0; DataIdx<AssetUserData.Num(); DataIdx++)
	{
		UAssetUserData* Datum = AssetUserData[DataIdx];
		if(Datum != NULL && Datum->IsA(InUserDataClass))
		{
			AssetUserData.RemoveAt(DataIdx);
			return;
		}
	}
}

const TArray<UAssetUserData*>* UStaticMesh::GetAssetUserDataArray() const 
{
	return &AssetUserData;
}

/**
 * Create BodySetup for this staticmesh 
 */
void UStaticMesh::CreateBodySetup()
{
	if (BodySetup==NULL)
	{
		BodySetup = NewObject<UBodySetup>(this);
		BodySetup->DefaultInstance.SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	}
}

void UStaticMesh::CreateNavCollision(const bool bIsUpdate)
{
	if (bHasNavigationData && BodySetup != nullptr)
	{
		if (NavCollision == nullptr)
		{
			NavCollision = UNavCollisionBase::ConstructNew(*this);
		}

		if (NavCollision)
		{
#if WITH_EDITOR
			if (bIsUpdate)
			{
				NavCollision->InvalidateCollision();
			}
#endif // WITH_EDITOR
			NavCollision->Setup(BodySetup);
		}
	}
	else
	{
		NavCollision = nullptr;
	}
}

void UStaticMesh::MarkAsNotHavingNavigationData()
{
	bHasNavigationData = false;
	NavCollision = nullptr;
}

/**
 * Returns vertex color data by position.
 * For matching to reimported meshes that may have changed or copying vertex paint data from mesh to mesh.
 *
 *	@param	VertexColorData		(out)A map of vertex position data and its color. The method fills this map.
 */
void UStaticMesh::GetVertexColorData(TMap<FVector, FColor>& VertexColorData)
{
	VertexColorData.Empty();
#if WITH_EDITOR
	// What LOD to get vertex colors from.  
	// Currently mesh painting only allows for painting on the first lod.
	const uint32 PaintingMeshLODIndex = 0;
	if (IsSourceModelValid(PaintingMeshLODIndex))
	{
		if (!GetSourceModel(PaintingMeshLODIndex).IsRawMeshEmpty())
		{
			// Extract the raw mesh.
			FRawMesh Mesh;
			GetSourceModel(PaintingMeshLODIndex).LoadRawMesh(Mesh);
			// Nothing to copy if there are no colors stored.
			if (Mesh.WedgeColors.Num() != 0 && Mesh.WedgeColors.Num() == Mesh.WedgeIndices.Num())
			{
				// Build a mapping of vertex positions to vertex colors.
				for (int32 WedgeIndex = 0; WedgeIndex < Mesh.WedgeIndices.Num(); ++WedgeIndex)
				{
					FVector Position = Mesh.VertexPositions[Mesh.WedgeIndices[WedgeIndex]];
					FColor Color = Mesh.WedgeColors[WedgeIndex];
					if (!VertexColorData.Contains(Position))
					{
						VertexColorData.Add(Position, Color);
					}
				}
			}
		}
	}
#endif // #if WITH_EDITORONLY_DATA
}

/**
 * Sets vertex color data by position.
 * Map of vertex color data by position is matched to the vertex position in the mesh
 * and nearest matching vertex color is used.
 *
 *	@param	VertexColorData		A map of vertex position data and color.
 */
void UStaticMesh::SetVertexColorData(const TMap<FVector, FColor>& VertexColorData)
{
#if WITH_EDITOR
	// What LOD to get vertex colors from.  
	// Currently mesh painting only allows for painting on the first lod.
	const uint32 PaintingMeshLODIndex = 0;
	if (IsSourceModelValid(PaintingMeshLODIndex))
	{
		if (GetSourceModel(PaintingMeshLODIndex).IsRawMeshEmpty() == false)
		{
			// Extract the raw mesh.
			FRawMesh Mesh;
			GetSourceModel(PaintingMeshLODIndex).LoadRawMesh(Mesh);

			// Reserve space for the new vertex colors.
			if (Mesh.WedgeColors.Num() == 0 || Mesh.WedgeColors.Num() != Mesh.WedgeIndices.Num())
			{
				Mesh.WedgeColors.Empty(Mesh.WedgeIndices.Num());
				Mesh.WedgeColors.AddUninitialized(Mesh.WedgeIndices.Num());
			}

			// Build a mapping of vertex positions to vertex colors.
			for (int32 WedgeIndex = 0; WedgeIndex < Mesh.WedgeIndices.Num(); ++WedgeIndex)
			{
				FVector Position = Mesh.VertexPositions[Mesh.WedgeIndices[WedgeIndex]];
				const FColor* Color = VertexColorData.Find(Position);
				if (Color)
				{
					Mesh.WedgeColors[WedgeIndex] = *Color;
				}
				else
				{
					Mesh.WedgeColors[WedgeIndex] = FColor(255, 255, 255, 255);
				}
			}

			// Save the new raw mesh.
			GetSourceModel(PaintingMeshLODIndex).SaveRawMesh(Mesh);
		}
	}
	// TODO_STATICMESH: Build?
#endif // #if WITH_EDITOR
}

ENGINE_API void UStaticMesh::RemoveVertexColors()
{
#if WITH_EDITOR
	bool bRemovedVertexColors = false;

	for (FStaticMeshSourceModel& SourceModel : GetSourceModels())
	{
		if (!SourceModel.IsRawMeshEmpty())
		{
			FRawMesh RawMesh;
			SourceModel.LoadRawMesh(RawMesh);

			if (RawMesh.WedgeColors.Num() > 0)
			{
				RawMesh.WedgeColors.Empty();

				SourceModel.SaveRawMesh(RawMesh);

				bRemovedVertexColors = true;
			}
		}
	}

	if (bRemovedVertexColors)
	{
		Build();
		MarkPackageDirty();
	}
#endif
}

void UStaticMesh::EnforceLightmapRestrictions(bool bUseRenderData)
{
	// Legacy content may contain a lightmap resolution of 0, which was valid when vertex lightmaps were supported, but not anymore with only texture lightmaps
	LightMapResolution = FMath::Max(LightMapResolution, 4);

	// Lightmass only supports 4 UVs
	int32 NumUVs = 4;

#if !WITH_EDITORONLY_DATA
	if (!bUseRenderData)
	{
		//The source models are only available in the editor, fallback on the render data.
		UE_ASSET_LOG(LogStaticMesh, Warning, this, TEXT("Trying to enforce lightmap restrictions using the static mesh SourceModels outside of the Editor."))
		bUseRenderData = true;
	}
#endif //WITH_EDITORONLY_DATA

	if (bUseRenderData)
	{
		if (RenderData)
		{
			for (int32 LODIndex = 0; LODIndex < RenderData->LODResources.Num(); ++LODIndex)
			{
				const FStaticMeshLODResources& LODResource = RenderData->LODResources[LODIndex];
				if (LODResource.GetNumVertices() > 0) // skip LOD that was stripped (eg. MinLOD)
				{
					NumUVs = FMath::Min(LODResource.GetNumTexCoords(), NumUVs);
				}
			}
		}
		else
		{
			NumUVs = 1;
		}
	}
#if WITH_EDITORONLY_DATA
	else
	{
		for (int32 LODIndex = 0; LODIndex < GetNumSourceModels(); ++LODIndex)
		{
			if (const FMeshDescription* MeshDescription = GetMeshDescription(LODIndex))
			{
				const TVertexInstanceAttributesConstRef<FVector2D> UVChannels = MeshDescription->VertexInstanceAttributes().GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate);

				// skip empty LODs
				if (UVChannels.GetNumElements() > 0)
				{
					int NumChannelsInLOD = UVChannels.GetNumIndices();
					const FStaticMeshSourceModel& SourceModel = GetSourceModel(LODIndex);

					if (SourceModel.BuildSettings.bGenerateLightmapUVs)
					{
						NumChannelsInLOD = FMath::Max(NumChannelsInLOD, SourceModel.BuildSettings.DstLightmapIndex + 1);
					}

					NumUVs = FMath::Min(NumChannelsInLOD, NumUVs);
				}
			}
			else
			{
				NumUVs = 1;
				break;
			}
		}

		if (GetNumSourceModels() == 0)
		{
			NumUVs = 1;
		}
	}
#endif //WITH_EDITORONLY_DATA

	// do not allow LightMapCoordinateIndex go negative
	check(NumUVs > 0);

	// Clamp LightMapCoordinateIndex to be valid for all lightmap uvs
	LightMapCoordinateIndex = FMath::Clamp(LightMapCoordinateIndex, 0, NumUVs - 1);
}

/**
 * Static: Processes the specified static mesh for light map UV problems
 *
 * @param	InStaticMesh					Static mesh to process
 * @param	InOutAssetsWithMissingUVSets	Array of assets that we found with missing UV sets
 * @param	InOutAssetsWithBadUVSets		Array of assets that we found with bad UV sets
 * @param	InOutAssetsWithValidUVSets		Array of assets that we found with valid UV sets
 * @param	bInVerbose						If true, log the items as they are found
 */
void UStaticMesh::CheckLightMapUVs( UStaticMesh* InStaticMesh, TArray< FString >& InOutAssetsWithMissingUVSets, TArray< FString >& InOutAssetsWithBadUVSets, TArray< FString >& InOutAssetsWithValidUVSets, bool bInVerbose )
{
	static const auto AllowStaticLightingVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.AllowStaticLighting"));
	const bool bAllowStaticLighting = (!AllowStaticLightingVar || AllowStaticLightingVar->GetValueOnGameThread() != 0);
	if (!bAllowStaticLighting)
	{
		// We do not need to check for lightmap UV problems when we do not allow static lighting
		return;
	}

	struct FLocal
	{
		/**
		 * Checks to see if a point overlaps a triangle
		 *
		 * @param	P	Point
		 * @param	A	First triangle vertex
		 * @param	B	Second triangle vertex
		 * @param	C	Third triangle vertex
		 *
		 * @return	true if the point overlaps the triangle
		 */
		bool IsPointInTriangle( const FVector& P, const FVector& A, const FVector& B, const FVector& C, const float Epsilon )
		{
			struct
			{
				bool SameSide( const FVector& P1, const FVector& P2, const FVector& InA, const FVector& InB, const float InEpsilon )
				{
					const FVector Cross1((InB - InA) ^ (P1 - InA));
					const FVector Cross2((InB - InA) ^ (P2 - InA));
					return (Cross1 | Cross2) >= -InEpsilon;
				}
			} Local;

			return ( Local.SameSide( P, A, B, C, Epsilon ) &&
					 Local.SameSide( P, B, A, C, Epsilon ) &&
					 Local.SameSide( P, C, A, B, Epsilon ) );
		}
		
		/**
		 * Checks to see if a point overlaps a triangle
		 *
		 * @param	P	Point
		 * @param	Triangle	triangle vertices
		 *
		 * @return	true if the point overlaps the triangle
		 */
		bool IsPointInTriangle(const FVector2D & P, const FVector2D (&Triangle)[3])
		{
			// Bias toward non-overlapping so sliver triangles won't overlap their adjoined neighbors
			const float TestEpsilon = -0.001f;
			// Test for overlap
			if( IsPointInTriangle(
				FVector( P, 0.0f ),
				FVector( Triangle[0], 0.0f ),
				FVector( Triangle[1], 0.0f ),
				FVector( Triangle[2], 0.0f ),
				TestEpsilon ) )
			{
				return true;
			}
			return false;
		}

		/**
		 * Checks for UVs outside of a 0.0 to 1.0 range.
		 *
		 * @param	TriangleUVs	a referenced array of 3 UV coordinates.
		 *
		 * @return	true if UVs are <0.0 or >1.0
		 */
		bool AreUVsOutOfRange(const FVector2D (&TriangleUVs)[3])
		{
			// Test for UVs outside of the 0.0 to 1.0 range (wrapped/clamped)
			for(int32 UVIndex = 0; UVIndex < 3; UVIndex++)
			{
				const FVector2D& CurVertUV = TriangleUVs[UVIndex];
				const float TestEpsilon = 0.001f;
				for( int32 CurDimIndex = 0; CurDimIndex < 2; ++CurDimIndex )
				{
					if( CurVertUV[ CurDimIndex ] < ( 0.0f - TestEpsilon ) || CurVertUV[ CurDimIndex ] > ( 1.0f + TestEpsilon ) )
					{
						return true;
					}
				}
			}
			return false;
		}

		/**
		 * Fills an array with 3 UV coordinates for a specified triangle from a FStaticMeshLODResources object.
		 *
		 * @param	MeshLOD	Source mesh.
		 * @param	TriangleIndex	triangle to get UV data from
		 * @param	UVChannel UV channel to extract
		 * @param	TriangleUVsOUT an array which is filled with the UV data
		 */
		void GetTriangleUVs( const FStaticMeshLODResources& MeshLOD, const int32 TriangleIndex, const int32 UVChannel, FVector2D (&TriangleUVsOUT)[3])
		{
			check( TriangleIndex < MeshLOD.GetNumTriangles());
			
			FIndexArrayView Indices = MeshLOD.IndexBuffer.GetArrayView();
			const int32 StartIndex = TriangleIndex*3;			
			const uint32 VertexIndices[] = {Indices[StartIndex + 0], Indices[StartIndex + 1], Indices[StartIndex + 2]};
			for(int i = 0; i<3;i++)
			{
				TriangleUVsOUT[i] = MeshLOD.VertexBuffers.StaticMeshVertexBuffer.GetVertexUV(VertexIndices[i], UVChannel);		
			}
		}

		enum UVCheckResult { UVCheck_Missing, UVCheck_Bad, UVCheck_OK, UVCheck_NoTriangles};
		/**
		 * Performs a UV check on a specific LOD from a UStaticMesh.
		 *
		 * @param	MeshLOD	a referenced array of 3 UV coordinates.
		 * @param	LightMapCoordinateIndex The UV channel containing the light map UVs.
		 * @param	OverlappingLightMapUVTriangleCountOUT Filled with the number of triangles that overlap one another.
		 * @param	OutOfBoundsTriangleCountOUT Filled with the number of triangles whose UVs are out of 0..1 range.
		 * @return	UVCheckResult UVCheck_Missing: light map UV channel does not exist in the data. UVCheck_Bad: one or more triangles break UV mapping rules. UVCheck_NoTriangle: The specified mesh has no triangles. UVCheck_OK: no problems were found.
		 */
		UVCheckResult CheckLODLightMapUVs( const FStaticMeshLODResources& MeshLOD, const int32 InLightMapCoordinateIndex, int32& OverlappingLightMapUVTriangleCountOUT, int32& OutOfBoundsTriangleCountOUT)
		{
			const int32 TriangleCount = MeshLOD.GetNumTriangles();
			if(TriangleCount==0)
			{
				return UVCheck_NoTriangles;
			}
			OverlappingLightMapUVTriangleCountOUT = 0;
			OutOfBoundsTriangleCountOUT = 0;

			TArray< int32 > TriangleOverlapCounts;
			TriangleOverlapCounts.AddZeroed( TriangleCount );

			if (InLightMapCoordinateIndex >= MeshLOD.GetNumTexCoords())
			{
				return UVCheck_Missing;
			}

			for(int32 CurTri = 0; CurTri<TriangleCount;CurTri++)
			{
				FVector2D CurTriangleUVs[3];
				GetTriangleUVs(MeshLOD, CurTri, InLightMapCoordinateIndex, CurTriangleUVs);
				FVector2D CurTriangleUVCentroid = ( CurTriangleUVs[0] + CurTriangleUVs[1] + CurTriangleUVs[2] ) / 3.0f;
		
				if( AreUVsOutOfRange(CurTriangleUVs) )
				{
					++OutOfBoundsTriangleCountOUT;
				}

				if(TriangleOverlapCounts[CurTri] != 0)
				{
					continue;
				}
				for(int32 OtherTri = CurTri+1; OtherTri<TriangleCount;OtherTri++)
				{
					if(TriangleOverlapCounts[OtherTri] != 0)
					{
						continue;
					}

					FVector2D OtherTriangleUVs[3];
					GetTriangleUVs(MeshLOD, OtherTri, InLightMapCoordinateIndex, OtherTriangleUVs);
					FVector2D OtherTriangleUVCentroid = ( OtherTriangleUVs[0] + OtherTriangleUVs[1] + OtherTriangleUVs[2] ) / 3.0f;

					bool result1 = IsPointInTriangle(CurTriangleUVCentroid, OtherTriangleUVs );
					bool result2 = IsPointInTriangle(OtherTriangleUVCentroid, CurTriangleUVs );

					if( result1 || result2)
					{
						++OverlappingLightMapUVTriangleCountOUT;
						++TriangleOverlapCounts[ CurTri ];
						++OverlappingLightMapUVTriangleCountOUT;
						++TriangleOverlapCounts[ OtherTri ];
					}
				}
			}

			return (OutOfBoundsTriangleCountOUT != 0 || OverlappingLightMapUVTriangleCountOUT !=0 ) ? UVCheck_Bad : UVCheck_OK;
		}
	} Local;

	check( InStaticMesh != NULL );

	TArray< int32 > TriangleOverlapCounts;

	const int32 NumLods = InStaticMesh->GetNumLODs();
	for( int32 CurLODModelIndex = 0; CurLODModelIndex < NumLods; ++CurLODModelIndex )
	{
		const FStaticMeshLODResources& RenderData = InStaticMesh->RenderData->LODResources[CurLODModelIndex];
		int32 LightMapTextureCoordinateIndex = InStaticMesh->LightMapCoordinateIndex;

		// We expect the light map texture coordinate to be greater than zero, as the first UV set
		// should never really be used for light maps, unless this mesh was exported as a light mapped uv set.
		if( LightMapTextureCoordinateIndex <= 0 && RenderData.GetNumTexCoords() > 1 )
		{	
			LightMapTextureCoordinateIndex = 1;
		}

		int32 OverlappingLightMapUVTriangleCount = 0;
		int32 OutOfBoundsTriangleCount = 0;

		const FLocal::UVCheckResult result = Local.CheckLODLightMapUVs( RenderData, LightMapTextureCoordinateIndex, OverlappingLightMapUVTriangleCount, OutOfBoundsTriangleCount);
		switch(result)
		{
			case FLocal::UVCheck_OK:
				InOutAssetsWithValidUVSets.Add( InStaticMesh->GetFullName() );
			break;
			case FLocal::UVCheck_Bad:
				InOutAssetsWithBadUVSets.Add( InStaticMesh->GetFullName() );
			break;
			case FLocal::UVCheck_Missing:
				InOutAssetsWithMissingUVSets.Add( InStaticMesh->GetFullName() );
			break;
			default:
			break;
		}

		if(bInVerbose == true)
		{
			switch(result)
			{
				case FLocal::UVCheck_OK:
					UE_LOG(LogStaticMesh, Log, TEXT( "[%s, LOD %i] light map UVs OK" ), *InStaticMesh->GetName(), CurLODModelIndex );
					break;
				case FLocal::UVCheck_Bad:
					if( OverlappingLightMapUVTriangleCount > 0 )
					{
						UE_LOG(LogStaticMesh, Warning, TEXT( "[%s, LOD %i] %i triangles with overlapping UVs (of %i) (UV set %i)" ), *InStaticMesh->GetName(), CurLODModelIndex, OverlappingLightMapUVTriangleCount, RenderData.GetNumTriangles(), LightMapTextureCoordinateIndex );
					}
					if( OutOfBoundsTriangleCount > 0 )
					{
						UE_LOG(LogStaticMesh, Warning, TEXT( "[%s, LOD %i] %i triangles with out-of-bound UVs (of %i) (UV set %i)" ), *InStaticMesh->GetName(), CurLODModelIndex, OutOfBoundsTriangleCount, RenderData.GetNumTriangles(), LightMapTextureCoordinateIndex );
					}
					break;
				case FLocal::UVCheck_Missing:
					UE_LOG(LogStaticMesh, Warning, TEXT( "[%s, LOD %i] missing light map UVs (Res %i, CoordIndex %i)" ), *InStaticMesh->GetName(), CurLODModelIndex, InStaticMesh->LightMapResolution, InStaticMesh->LightMapCoordinateIndex );
					break;
				case FLocal::UVCheck_NoTriangles:
					UE_LOG(LogStaticMesh, Warning, TEXT( "[%s, LOD %i] doesn't have any triangles" ), *InStaticMesh->GetName(), CurLODModelIndex );
					break;
				default:
					break;
			}
		}
	}
}

UMaterialInterface* UStaticMesh::GetMaterial(int32 MaterialIndex) const
{
	if (StaticMaterials.IsValidIndex(MaterialIndex))
	{
		return StaticMaterials[MaterialIndex].MaterialInterface;
	}

	return NULL;
}


FName UStaticMesh::AddMaterial(UMaterialInterface* Material)
{
	if (Material == nullptr)
	{
		return NAME_None;
	}

	// Create a unique slot name for the material
	FName MaterialName = Material->GetFName();
	for (const FStaticMaterial& StaticMaterial : StaticMaterials)
	{
		const FName ExistingName = StaticMaterial.MaterialSlotName;
		if (ExistingName.GetComparisonIndex() == MaterialName.GetComparisonIndex())
		{
			MaterialName = FName(MaterialName, FMath::Max(MaterialName.GetNumber(), ExistingName.GetNumber() + 1));
		}
	}

#if WITH_EDITORONLY_DATA
	StaticMaterials.Emplace(Material, MaterialName, MaterialName);
#else
	StaticMaterials.Emplace(Material, MaterialName);
#endif

	return MaterialName;
}


int32 UStaticMesh::GetMaterialIndex(FName MaterialSlotName) const
{
	for (int32 MaterialIndex = 0; MaterialIndex < StaticMaterials.Num(); ++MaterialIndex)
	{
		const FStaticMaterial &StaticMaterial = StaticMaterials[MaterialIndex];
		if (StaticMaterial.MaterialSlotName == MaterialSlotName)
		{
			return MaterialIndex;
		}
	}
	return -1;
}

#if WITH_EDITOR
void UStaticMesh::SetMaterial(int32 MaterialIndex, UMaterialInterface* NewMaterial)
{
	static FName NAME_StaticMaterials = GET_MEMBER_NAME_CHECKED(UStaticMesh, StaticMaterials);

	if (StaticMaterials.IsValidIndex(MaterialIndex))
	{
		FScopedTransaction ScopeTransaction(LOCTEXT("StaticMeshMaterialChanged", "StaticMesh: Material changed"));

		// flag the property (Materials) we're modifying so that not all of the object is rebuilt.
		FProperty* ChangedProperty = FindField<FProperty>(UStaticMesh::StaticClass(), NAME_StaticMaterials);
		check(ChangedProperty);
		PreEditChange(ChangedProperty);
		UMaterialInterface* CancelOldMaterial = StaticMaterials[MaterialIndex].MaterialInterface;
		StaticMaterials[MaterialIndex].MaterialInterface = NewMaterial;
		if (NewMaterial != nullptr)
		{
			//Set the Material slot name to a good default one
			if (StaticMaterials[MaterialIndex].MaterialSlotName == NAME_None)
			{
				StaticMaterials[MaterialIndex].MaterialSlotName = NewMaterial->GetFName();
			}
			
			//Set the original fbx material name so we can re-import correctly, ensure the name is unique
			if (StaticMaterials[MaterialIndex].ImportedMaterialSlotName == NAME_None)
			{
				auto IsMaterialNameUnique = [this, MaterialIndex](const FName TestName)
				{
					for (int32 MatIndex = 0; MatIndex < StaticMaterials.Num(); ++MatIndex)
					{
						if (MatIndex == MaterialIndex)
						{
							continue;
						}
						if (StaticMaterials[MatIndex].ImportedMaterialSlotName == TestName)
						{
							return false;
						}
					}
					return true;
				};

				int32 MatchNameCounter = 0;
				//Make sure the name is unique for imported material slot name
				bool bUniqueName = false;
				FString MaterialSlotName = NewMaterial->GetName();
				while (!bUniqueName)
				{
					bUniqueName = true;
					if (!IsMaterialNameUnique(FName(*MaterialSlotName)))
					{
						bUniqueName = false;
						MatchNameCounter++;
						MaterialSlotName = NewMaterial->GetName() + TEXT("_") + FString::FromInt(MatchNameCounter);
					}
				}
				StaticMaterials[MaterialIndex].ImportedMaterialSlotName = FName(*MaterialSlotName);
			}

			//Make sure adjacency information fit new material change
			TArray<bool> FixLODAdjacencyOption;
			FixLODAdjacencyOption.AddZeroed(GetNumLODs());
			bool bPromptUser = false;
			for (int32 LODIndex = 0; LODIndex < GetNumLODs(); ++LODIndex)
			{
				FixLODAdjacencyOption[LODIndex] = FixLODRequiresAdjacencyInformation(LODIndex);
				bPromptUser |= FixLODAdjacencyOption[LODIndex];
			}

			//Prompt the user only once
			if (bPromptUser)
			{
				FText ConfirmRequiredAdjacencyText = FText::Format(LOCTEXT("ConfirmRequiredAdjacencyNoLODIndex", "Using a tessellation material required the adjacency buffer to be computed.\nDo you want to set the adjacency options to true?\n\n\tSaticMesh: {0}\n\tMaterial: {1}"), FText::FromString(GetPathName()), FText::FromString(StaticMaterials[MaterialIndex].MaterialInterface->GetPathName()));
				EAppReturnType::Type Result = FMessageDialog::Open(EAppMsgType::YesNoCancel, ConfirmRequiredAdjacencyText);
				bool bRevertAdjacency = false;
				switch(Result)
				{
					//Handle cancel and negative answer
					case EAppReturnType::Cancel:
					{
						StaticMaterials[MaterialIndex].MaterialInterface = CancelOldMaterial;
						bRevertAdjacency = true;
					}
					case EAppReturnType::No:
					{
						bRevertAdjacency = true;
					}
				}
				if (bRevertAdjacency)
				{
					//Revert previous change since the material was reverse
					for (int32 FixLODIndex = 0; FixLODIndex < FixLODAdjacencyOption.Num(); ++FixLODIndex)
					{
						if (FixLODAdjacencyOption[FixLODIndex])
						{
							GetSourceModel(FixLODIndex).BuildSettings.bBuildAdjacencyBuffer = false;
						}
					}
				}
			}
		}

		if (ChangedProperty)
		{
			FPropertyChangedEvent PropertyUpdateStruct(ChangedProperty);
			PostEditChangeProperty(PropertyUpdateStruct);
		}
		else
		{
			Modify();
			PostEditChange();
		}
		if (BodySetup)
		{
			BodySetup->CreatePhysicsMeshes();
		}
	}
}
#endif //WITH_EDITOR

int32 UStaticMesh::GetMaterialIndexFromImportedMaterialSlotName(FName ImportedMaterialSlotName) const
{
	for (int32 MaterialIndex = 0; MaterialIndex < StaticMaterials.Num(); ++MaterialIndex)
	{
		const FStaticMaterial &StaticMaterial = StaticMaterials[MaterialIndex];
		if (StaticMaterial.ImportedMaterialSlotName == ImportedMaterialSlotName)
		{
			return MaterialIndex;
		}
	}
	return INDEX_NONE;
}

/**
 * Returns the render data to use for exporting the specified LOD. This method should always
 * be called when exporting a static mesh.
 */
const FStaticMeshLODResources& UStaticMesh::GetLODForExport(int32 LODIndex) const
{
	check(RenderData);
	LODIndex = FMath::Clamp<int32>( LODIndex, 0, RenderData->LODResources.Num()-1 );
	// TODO_STATICMESH: Don't allow exporting simplified meshes?
	return RenderData->LODResources[LODIndex];
}

#if WITH_EDITOR
bool UStaticMesh::CanLODsShareStaticLighting() const
{
	bool bCanShareData = true;
	for (int32 LODIndex = 1; bCanShareData && LODIndex < GetNumSourceModels(); ++LODIndex)
	{
		bCanShareData = bCanShareData && !IsMeshDescriptionValid(LODIndex);
	}

	if (SpeedTreeWind.IsValid())
	{
		// SpeedTrees are set up for lighting to share between LODs
		bCanShareData = true;
	}

	return bCanShareData;
}

void UStaticMesh::ConvertLegacyLODDistance()
{
	const int32 NumSourceModels = GetNumSourceModels();
	check(NumSourceModels > 0);
	check(NumSourceModels <= MAX_STATIC_MESH_LODS);

	if(NumSourceModels == 1)
	{
		// Only one model, 
		GetSourceModel(0).ScreenSize.Default = 1.0f;
	}
	else
	{
		// Multiple models, we should have LOD distance data.
		// Assuming an FOV of 90 and a screen size of 1920x1080 to estimate an appropriate display factor.
		const float HalfFOV = PI / 4.0f;
		const float ScreenWidth = 1920.0f;
		const float ScreenHeight = 1080.0f;

		for(int32 ModelIndex = 0 ; ModelIndex < NumSourceModels ; ++ModelIndex)
		{
			FStaticMeshSourceModel& SrcModel = GetSourceModel(ModelIndex);

			if(SrcModel.LODDistance_DEPRECATED == 0.0f)
			{
				SrcModel.ScreenSize.Default = 1.0f;
				RenderData->ScreenSize[ModelIndex] = SrcModel.ScreenSize.Default;
			}
			else
			{
				// Create a screen position from the LOD distance
				const FVector4 PointToTest(0.0f, 0.0f, SrcModel.LODDistance_DEPRECATED, 1.0f);
				FPerspectiveMatrix ProjMatrix(HalfFOV, ScreenWidth, ScreenHeight, 1.0f);
				FVector4 ScreenPosition = ProjMatrix.TransformFVector4(PointToTest);
				// Convert to a percentage of the screen
				const float ScreenMultiple = ScreenWidth / 2.0f * ProjMatrix.M[0][0];
				const float ScreenRadius = ScreenMultiple * GetBounds().SphereRadius / FMath::Max(ScreenPosition.W, 1.0f);
				const float ScreenArea = ScreenWidth * ScreenHeight;
				const float BoundsArea = PI * ScreenRadius * ScreenRadius;
				SrcModel.ScreenSize.Default = FMath::Clamp(BoundsArea / ScreenArea, 0.0f, 1.0f);
				RenderData->ScreenSize[ModelIndex] = SrcModel.ScreenSize.Default;
			}
		}
	}
}

void UStaticMesh::ConvertLegacyLODScreenArea()
{
	const int32 NumSourceModels = GetNumSourceModels();
	check(NumSourceModels > 0);
	check(NumSourceModels <= MAX_STATIC_MESH_LODS);

	if (NumSourceModels == 1)
	{
		// Only one model, 
		GetSourceModel(0).ScreenSize.Default = 1.0f;
	}
	else
	{
		// Use 1080p, 90 degree FOV as a default, as this should not cause runtime regressions in the common case.
		const float HalfFOV = PI * 0.25f;
		const float ScreenWidth = 1920.0f;
		const float ScreenHeight = 1080.0f;
		const FPerspectiveMatrix ProjMatrix(HalfFOV, ScreenWidth, ScreenHeight, 1.0f);
		FBoxSphereBounds Bounds = GetBounds();

		// Multiple models, we should have LOD screen area data.
		for (int32 ModelIndex = 0; ModelIndex < NumSourceModels; ++ModelIndex)
		{
			FStaticMeshSourceModel& SrcModel = GetSourceModel(ModelIndex);

			if (SrcModel.ScreenSize.Default == 0.0f)
			{
				SrcModel.ScreenSize.Default = 1.0f;
				RenderData->ScreenSize[ModelIndex] = SrcModel.ScreenSize.Default;
			}
			else
			{
				// legacy transition screen size was previously a screen AREA fraction using resolution-scaled values, so we need to convert to distance first to correctly calculate the threshold
				const float ScreenArea = SrcModel.ScreenSize.Default * (ScreenWidth * ScreenHeight);
				const float ScreenRadius = FMath::Sqrt(ScreenArea / PI);
				const float ScreenDistance = FMath::Max(ScreenWidth / 2.0f * ProjMatrix.M[0][0], ScreenHeight / 2.0f * ProjMatrix.M[1][1]) * Bounds.SphereRadius / ScreenRadius;

				// Now convert using the query function
				SrcModel.ScreenSize.Default = ComputeBoundsScreenSize(FVector::ZeroVector, Bounds.SphereRadius, FVector(0.0f, 0.0f, ScreenDistance), ProjMatrix);
				RenderData->ScreenSize[ModelIndex] = SrcModel.ScreenSize.Default;
			}
		}
	}
}

void UStaticMesh::GenerateLodsInPackage()
{
	FFormatNamedArguments Args;
	Args.Add(TEXT("StaticMeshName"), FText::FromString(GetName()));
	FStaticMeshStatusMessageContext StatusContext(FText::Format(NSLOCTEXT("Engine", "SavingStaticMeshLODsStatus", "Saving generated LODs for static mesh {StaticMeshName}..."), Args));

	// Get LODGroup info
	ITargetPlatformManagerModule& TargetPlatformManager = GetTargetPlatformManagerRef();
	ITargetPlatform* RunningPlatform = TargetPlatformManager.GetRunningTargetPlatform();
	check(RunningPlatform);
	const FStaticMeshLODSettings& LODSettings = RunningPlatform->GetStaticMeshLODSettings();

	// Generate the reduced models
	IMeshUtilities& MeshUtilities = FModuleManager::Get().LoadModuleChecked<IMeshUtilities>(TEXT("MeshUtilities"));
	if (MeshUtilities.GenerateStaticMeshLODs(this, LODSettings.GetLODGroup(LODGroup)))
	{
		// Clear LOD settings
		LODGroup = NAME_None;
		const auto& NewGroup = LODSettings.GetLODGroup(LODGroup);
		for (int32 Index = 0; Index < GetNumSourceModels(); ++Index)
		{
			GetSourceModel(Index).ReductionSettings = NewGroup.GetDefaultSettings(0);
		}

		Build(true);

		// Raw mesh is now dirty, so the package has to be resaved
		MarkPackageDirty();
	}
}

#endif // #if WITH_EDITOR

void UStaticMesh::AddSocket(UStaticMeshSocket* Socket)
{
	Sockets.AddUnique(Socket);
}

UStaticMeshSocket* UStaticMesh::FindSocket(FName InSocketName) const
{
	if(InSocketName == NAME_None)
	{
		return NULL;
	}

	for(int32 i=0; i<Sockets.Num(); i++)
	{
		UStaticMeshSocket* Socket = Sockets[i];
		if(Socket && Socket->SocketName == InSocketName)
		{
			return Socket;
		}
	}
	return NULL;
}

void UStaticMesh::RemoveSocket(UStaticMeshSocket* Socket)
{
	Sockets.Remove(Socket);
}

/*-----------------------------------------------------------------------------
UStaticMeshSocket
-----------------------------------------------------------------------------*/

UStaticMeshSocket::UStaticMeshSocket(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	RelativeScale = FVector(1.0f, 1.0f, 1.0f);
#if WITH_EDITORONLY_DATA
	bSocketCreatedAtImport = false;
#endif
}

/** Utility that returns the current matrix for this socket. */
bool UStaticMeshSocket::GetSocketMatrix(FMatrix& OutMatrix, UStaticMeshComponent const* MeshComp) const
{
	check( MeshComp );
	OutMatrix = FScaleRotationTranslationMatrix( RelativeScale, RelativeRotation, RelativeLocation ) * MeshComp->GetComponentTransform().ToMatrixWithScale();
	return true;
}

bool UStaticMeshSocket::GetSocketTransform(FTransform& OutTransform, class UStaticMeshComponent const* MeshComp) const
{
	check( MeshComp );
	OutTransform = FTransform(RelativeRotation, RelativeLocation, RelativeScale) * MeshComp->GetComponentTransform();
	return true;
}

bool UStaticMeshSocket::AttachActor(AActor* Actor,  UStaticMeshComponent* MeshComp) const
{
	bool bAttached = false;

	// Don't support attaching to own socket
	if (Actor != MeshComp->GetOwner() && Actor->GetRootComponent())
	{
		FMatrix SocketTM;
		if( GetSocketMatrix( SocketTM, MeshComp ) )
		{
			Actor->Modify();

			Actor->SetActorLocation(SocketTM.GetOrigin(), false);
			Actor->SetActorRotation(SocketTM.Rotator());
			Actor->GetRootComponent()->AttachToComponent(MeshComp, FAttachmentTransformRules::SnapToTargetNotIncludingScale, SocketName);

#if WITH_EDITOR
			if (GIsEditor)
			{
				Actor->PreEditChange(NULL);
				Actor->PostEditChange();
			}
#endif // WITH_EDITOR

			bAttached = true;
		}
	}
	return bAttached;
}

#if WITH_EDITOR
void UStaticMeshSocket::PostEditChangeProperty( FPropertyChangedEvent& PropertyChangedEvent )
{
	Super::PostEditChangeProperty( PropertyChangedEvent );

	if( PropertyChangedEvent.Property )
	{
		ChangedEvent.Broadcast( this, PropertyChangedEvent.MemberProperty );
	}
}
#endif

void UStaticMeshSocket::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	Ar.UsingCustomVersion(FFrameworkObjectVersion::GUID);

	if(Ar.CustomVer(FFrameworkObjectVersion::GUID) < FFrameworkObjectVersion::MeshSocketScaleUtilization)
	{
		// Set the relative scale to 1.0. As it was not used before this should allow existing data
		// to work as expected.
		RelativeScale = FVector(1.0f, 1.0f, 1.0f);
	}
}

#undef LOCTEXT_NAMESPACE

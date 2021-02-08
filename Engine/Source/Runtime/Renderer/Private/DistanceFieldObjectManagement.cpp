// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	DistanceFieldObjectManagement.cpp
=============================================================================*/

#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "HAL/IConsoleManager.h"
#include "Async/ParallelFor.h"
#include "RHI.h"
#include "RenderResource.h"
#include "ShaderParameters.h"
#include "RendererInterface.h"
#include "Shader.h"
#include "SceneUtils.h"
#include "GlobalShader.h"
#include "DeferredShadingRenderer.h"
#include "ScenePrivate.h"
#include "DistanceFieldLightingShared.h"
#include "DistanceFieldAmbientOcclusion.h"

extern void LumenUpdateDFObjectIndex(FScene* Scene, int32 MappingIndex);

float GAOMaxObjectBoundingRadius = 50000;
FAutoConsoleVariableRef CVarAOMaxObjectBoundingRadius(
	TEXT("r.AOMaxObjectBoundingRadius"),
	GAOMaxObjectBoundingRadius,
	TEXT("Objects larger than this will not contribute to AO calculations, to improve performance."),
	ECVF_RenderThreadSafe
	);

int32 GAOLogObjectBufferReallocation = 0;
FAutoConsoleVariableRef CVarAOLogObjectBufferReallocation(
	TEXT("r.AOLogObjectBufferReallocation"),
	GAOLogObjectBufferReallocation,
	TEXT(""),
	ECVF_RenderThreadSafe
	);

int32 GDFParallelUpdate = 0;
FAutoConsoleVariableRef CVarDFParallelUpdate(
	TEXT("r.DF.ParallelUpdate"),
	GDFParallelUpdate,
	TEXT(""),
	ECVF_RenderThreadSafe
);

// Must match equivalent shader defines
template<> int32 TDistanceFieldObjectBuffers<DFPT_SignedDistanceField>::ObjectDataStride = 11;
template<> int32 TDistanceFieldObjectBuffers<DFPT_SignedDistanceField>::ObjectBoundsStride = 2;
template<> int32 TDistanceFieldObjectBuffers<DFPT_HeightField>::ObjectDataStride = 6;
template<> int32 TDistanceFieldObjectBuffers<DFPT_HeightField>::ObjectBoundsStride = 2;
template<> int32 TDistanceFieldCulledObjectBuffers<DFPT_SignedDistanceField>::ObjectDataStride = TDistanceFieldObjectBuffers<DFPT_SignedDistanceField>::ObjectDataStride;
template<> int32 TDistanceFieldCulledObjectBuffers<DFPT_SignedDistanceField>::ObjectBoxBoundsStride = 5;
template<> int32 TDistanceFieldCulledObjectBuffers<DFPT_HeightField>::ObjectDataStride = 6;
template<> int32 TDistanceFieldCulledObjectBuffers<DFPT_HeightField>::ObjectBoxBoundsStride = 5;

template <EDistanceFieldPrimitiveType PrimitiveType>
void TDistanceFieldObjectBuffers<PrimitiveType>::Initialize()
{
}

const uint32 UpdateObjectsGroupSize = 64;

struct FParallelUpdateRangeDFO
{
	int32 ItemStart;
	int32 ItemCount;
};

struct FParallelUpdateRangesDFO
{
	FParallelUpdateRangeDFO Range[4];
};

// TODO: Improve and move to shared utility location.
static int32 PartitionUpdateRangesDFO(FParallelUpdateRangesDFO& Ranges, int32 ItemCount, bool bAllowParallel)
{
	if (ItemCount < 256 || !bAllowParallel)
	{
		Ranges.Range[0].ItemStart = 0;
		Ranges.Range[0].ItemCount = ItemCount;
		return 1;
	}

	const int32 RangeCount = Align(ItemCount, 4) >> 2;

	Ranges.Range[0].ItemCount = RangeCount;
	Ranges.Range[1].ItemCount = RangeCount;
	Ranges.Range[2].ItemCount = RangeCount;

	Ranges.Range[0].ItemStart = 0;
	Ranges.Range[1].ItemStart = RangeCount;
	Ranges.Range[2].ItemStart = RangeCount * 2;
	Ranges.Range[3].ItemStart = RangeCount * 3;
	Ranges.Range[3].ItemCount = ItemCount - Ranges.Range[3].ItemStart;

	return Ranges.Range[3].ItemCount > 0 ? 4 : 3;
}

void AddModifiedBoundsForLumen(FScene* Scene, FGlobalDFCacheType CacheType, const FBox& Bounds)
{
	FLumenSceneData& LumenData = *Scene->LumenSceneData;
	LumenData.PrimitiveModifiedBounds.Add(Bounds);
}

void AddModifiedBounds(FScene* Scene, FGlobalDFCacheType CacheType, const FBox& Bounds)
{
	FDistanceFieldSceneData& DistanceFieldData = Scene->DistanceFieldSceneData;
	DistanceFieldData.PrimitiveModifiedBounds[CacheType].Add(Bounds);
	AddModifiedBoundsForLumen(Scene, CacheType, Bounds);
}

void UpdateGlobalDistanceFieldObjectRemoves(FScene* Scene)
{
	FDistanceFieldSceneData& DistanceFieldSceneData = Scene->DistanceFieldSceneData;

	if (DistanceFieldSceneData.PendingRemoveOperations.Num() > 0)
	{
		TArray<int32, SceneRenderingAllocator> PendingRemoveOperations;

		for (int32 RemoveIndex = 0; RemoveIndex < DistanceFieldSceneData.PendingRemoveOperations.Num(); RemoveIndex++)
		{
			// Can't dereference the primitive here, it has already been deleted
			const FPrimitiveSceneInfo* Primitive = DistanceFieldSceneData.PendingRemoveOperations[RemoveIndex].Primitive;
			const TArray<int32, TInlineAllocator<1>>& DistanceFieldInstanceIndices = DistanceFieldSceneData.PendingRemoveOperations[RemoveIndex].DistanceFieldInstanceIndices;

			for (int32 RemoveInstanceIndex = 0; RemoveInstanceIndex < DistanceFieldInstanceIndices.Num(); RemoveInstanceIndex++)
			{
				const int32 InstanceIndex = DistanceFieldInstanceIndices[RemoveInstanceIndex];

				// InstanceIndex will be -1 with zero scale meshes
				if (InstanceIndex >= 0)
				{
					FGlobalDFCacheType CacheType = DistanceFieldSceneData.PendingRemoveOperations[RemoveIndex].bOftenMoving ? GDF_Full : GDF_MostlyStatic;
					AddModifiedBounds(Scene, CacheType, DistanceFieldSceneData.PrimitiveInstanceMapping[InstanceIndex].WorldBounds);
					PendingRemoveOperations.Add(InstanceIndex);
				}
			}
		}

		DistanceFieldSceneData.PendingRemoveOperations.Reset();

		if (PendingRemoveOperations.Num() > 0)
		{
			check(DistanceFieldSceneData.NumObjectsInBuffer >= PendingRemoveOperations.Num());

			// Sort from largest to smallest so we can safely RemoveAtSwap without invalidating indices in this array
			PendingRemoveOperations.Sort(TGreater<int32>());

			for (int32 RemoveIndex : PendingRemoveOperations)
			{
				--DistanceFieldSceneData.NumObjectsInBuffer;
				const int32 MoveFromIndex = DistanceFieldSceneData.NumObjectsInBuffer;

				FPrimitiveAndInstance& PrimitiveAndInstanceBeingMoved = DistanceFieldSceneData.PrimitiveInstanceMapping[MoveFromIndex];
				if (RemoveIndex < DistanceFieldSceneData.NumObjectsInBuffer)
				{
					// Fixup indices of the primitive that is being moved
					check(PrimitiveAndInstanceBeingMoved.Primitive && PrimitiveAndInstanceBeingMoved.Primitive->DistanceFieldInstanceIndices.Num() > 0);
					PrimitiveAndInstanceBeingMoved.Primitive->DistanceFieldInstanceIndices[PrimitiveAndInstanceBeingMoved.InstanceIndex] = RemoveIndex;
				}

				DistanceFieldSceneData.PrimitiveInstanceMapping.RemoveAtSwap(RemoveIndex, 1, false);

				LumenUpdateDFObjectIndex(Scene, RemoveIndex);
				DistanceFieldSceneData.IndicesToUpdateInObjectBuffers.Add(RemoveIndex);

				// Lumen caches distance field indices, which requires an update after a swap
				if (RemoveIndex < DistanceFieldSceneData.PrimitiveInstanceMapping.Num())
				{
					const FPrimitiveAndInstance& Swapped = DistanceFieldSceneData.PrimitiveInstanceMapping[RemoveIndex];
					const FGlobalDFCacheType CacheType = Swapped.Primitive->Proxy->IsOftenMoving() ? GDF_Full : GDF_MostlyStatic;
					AddModifiedBoundsForLumen(Scene, CacheType, Swapped.WorldBounds);
				}
			}

			PendingRemoveOperations.Reset();
		}
	}
}

void UpdateGlobalHeightFieldObjectRemoves(FScene* Scene)
{
	FDistanceFieldSceneData& SceneData = Scene->DistanceFieldSceneData;

	if (SceneData.PendingHeightFieldRemoveOps.Num())
	{
		TArray<int32, SceneRenderingAllocator> PendingRemoveObjectIndices;

		for (int32 Idx = 0; Idx < SceneData.PendingHeightFieldRemoveOps.Num(); ++Idx)
		{
			const FHeightFieldPrimitiveRemoveInfo& RemoveInfo = SceneData.PendingHeightFieldRemoveOps[Idx];
			check(RemoveInfo.DistanceFieldInstanceIndices.Num() == 1);
			const int32 ObjectIdx = RemoveInfo.DistanceFieldInstanceIndices[0];

			if (ObjectIdx >= 0)
			{
				const FGlobalDFCacheType CacheType = RemoveInfo.bOftenMoving ? GDF_Full : GDF_MostlyStatic;
				AddModifiedBounds(Scene, CacheType, RemoveInfo.WorldBounds);
				PendingRemoveObjectIndices.Add(ObjectIdx);
			}
		}

		SceneData.PendingHeightFieldRemoveOps.Reset();

		if (PendingRemoveObjectIndices.Num())
		{
			check(SceneData.NumHeightFieldObjectsInBuffer >= PendingRemoveObjectIndices.Num());
			check(SceneData.NumHeightFieldObjectsInBuffer == SceneData.HeightfieldPrimitives.Num());

			// Sort from largest to smallest so we can safely RemoveAtSwap without invalidating indices in this array
			PendingRemoveObjectIndices.Sort(TGreater<int32>());

			// Next RemoveAtSwap
			for (int32 RemoveIndex : PendingRemoveObjectIndices)
			{
				--SceneData.NumHeightFieldObjectsInBuffer;
				const int32 MoveFromIndex = SceneData.NumHeightFieldObjectsInBuffer;

				FPrimitiveSceneInfo* PrimitiveBeingMoved = SceneData.HeightfieldPrimitives[MoveFromIndex];
				if (RemoveIndex < SceneData.NumHeightFieldObjectsInBuffer)
				{
					// Fixup indices of the primitive that is being moved
					check(PrimitiveBeingMoved && PrimitiveBeingMoved->DistanceFieldInstanceIndices.Num() == 1);
					PrimitiveBeingMoved->DistanceFieldInstanceIndices[0] = RemoveIndex;
				}

				SceneData.HeightfieldPrimitives.RemoveAtSwap(RemoveIndex, 1, false);

				SceneData.IndicesToUpdateInHeightFieldObjectBuffers.Add(RemoveIndex);
			}

			PendingRemoveObjectIndices.Reset();
		}
	}
}

void LogDistanceFieldUpdate(FPrimitiveSceneInfo const* PrimitiveSceneInfo, float BoundingRadius, bool bIsAddOperation)
{
	extern int32 GAOLogGlobalDistanceFieldModifiedPrimitives;

	if (GAOLogGlobalDistanceFieldModifiedPrimitives == 1
		|| (GAOLogGlobalDistanceFieldModifiedPrimitives == 2 && !PrimitiveSceneInfo->Proxy->IsOftenMoving()))
	{
		UE_LOG(LogDistanceField, Log,
			TEXT("Global Distance Field %s primitive %s %s %s bounding radius %.1f"),
			PrimitiveSceneInfo->Proxy->IsOftenMoving() ? TEXT("Movable") : TEXT("CACHED"),
			(bIsAddOperation ? TEXT("add") : TEXT("update")),
			*PrimitiveSceneInfo->Proxy->GetOwnerName().ToString(),
			*PrimitiveSceneInfo->Proxy->GetResourceName().ToString(),
			BoundingRadius);
	}
}

/** Gathers the information needed to represent a single object's distance field and appends it to the upload buffers. */
bool ProcessPrimitiveUpdate(
	bool bIsAddOperation,
	FSceneRenderer& SceneRenderer, 
	FPrimitiveSceneInfo* PrimitiveSceneInfo,
	TArray<FMatrix>& ObjectLocalToWorldTransforms,
	TArray<int32>& IndicesToUpdateInObjectBuffers)
{
	FScene* Scene = SceneRenderer.Scene;
	FDistanceFieldSceneData& DistanceFieldSceneData = Scene->DistanceFieldSceneData;

	ObjectLocalToWorldTransforms.Reset();

	FBox LocalVolumeBounds;
	FVector2D DistanceMinMax;
	FIntVector BlockMin;
	FIntVector BlockSize;
	bool bBuiltAsIfTwoSided;
	float SelfShadowBias;
	bool bThrottled;
	PrimitiveSceneInfo->Proxy->GetDistancefieldAtlasData(LocalVolumeBounds, DistanceMinMax, BlockMin, BlockSize, bBuiltAsIfTwoSided, SelfShadowBias, bThrottled);
	PrimitiveSceneInfo->Proxy->GetDistancefieldInstanceData(ObjectLocalToWorldTransforms);	

	if (bThrottled)
	{
		return false;
	}

	if (BlockMin.X >= 0 
		&& BlockMin.Y >= 0 
		&& BlockMin.Z >= 0 
		&& ObjectLocalToWorldTransforms.Num() > 0)
	{
		const float BoundingRadius = PrimitiveSceneInfo->Proxy->GetBounds().SphereRadius;
		const FGlobalDFCacheType CacheType = PrimitiveSceneInfo->Proxy->IsOftenMoving() ? GDF_Full : GDF_MostlyStatic;

		// Proxy bounds are only useful if single instance
		if (ObjectLocalToWorldTransforms.Num() > 1 || BoundingRadius < GAOMaxObjectBoundingRadius)
		{
			if (bIsAddOperation)
			{
				PrimitiveSceneInfo->DistanceFieldInstanceIndices.Empty(ObjectLocalToWorldTransforms.Num());
				PrimitiveSceneInfo->DistanceFieldInstanceIndices.AddZeroed(ObjectLocalToWorldTransforms.Num());
			}

			for (int32 TransformIndex = 0; TransformIndex < ObjectLocalToWorldTransforms.Num(); TransformIndex++)
			{
				const FMatrix& LocalToWorld = ObjectLocalToWorldTransforms[TransformIndex];
				const float MaxScale = LocalToWorld.GetMaximumAxisScale();

				// Skip degenerate primitives
				if (MaxScale > 0)
				{
					uint32 UploadIndex;

					if (bIsAddOperation)
					{
						UploadIndex = DistanceFieldSceneData.NumObjectsInBuffer;
						++DistanceFieldSceneData.NumObjectsInBuffer;
					}
					else
					{
						UploadIndex = PrimitiveSceneInfo->DistanceFieldInstanceIndices[TransformIndex];
					}

					IndicesToUpdateInObjectBuffers.Add(UploadIndex);

					const FBox WorldBounds = LocalVolumeBounds.TransformBy(LocalToWorld);


					if (bIsAddOperation)
					{
						const int32 AddIndex = UploadIndex;
						const int32 MappingIndex = DistanceFieldSceneData.PrimitiveInstanceMapping.Add(FPrimitiveAndInstance(LocalToWorld, WorldBounds, PrimitiveSceneInfo, TransformIndex));
						PrimitiveSceneInfo->DistanceFieldInstanceIndices[TransformIndex] = AddIndex;

						LumenUpdateDFObjectIndex(Scene, MappingIndex);
						AddModifiedBounds(Scene, CacheType, WorldBounds);
						LogDistanceFieldUpdate(PrimitiveSceneInfo, BoundingRadius, bIsAddOperation);
					}
					else 
					{
						// InstanceIndex will be -1 with zero scale meshes
						const int32 InstanceIndex = PrimitiveSceneInfo->DistanceFieldInstanceIndices[TransformIndex];
						if (InstanceIndex >= 0)
						{
							FPrimitiveAndInstance& Mapping = DistanceFieldSceneData.PrimitiveInstanceMapping[InstanceIndex];

							// Filter out global distance field updates which were too small
							if (!Mapping.WorldBounds.GetExtent().Equals(WorldBounds.GetExtent(), 0.01f)
								|| !Mapping.LocalToWorld.Equals(LocalToWorld, 0.01f))
							{
								// decide if we want to make a single global distance field update or two updates for large movement (teleport) case
								const FBox MergedBounds = Mapping.WorldBounds + WorldBounds;
								const FVector MergedExtentIncrease = MergedBounds.GetExtent() - Mapping.WorldBounds.GetExtent() - WorldBounds.GetExtent();
								if (MergedExtentIncrease.GetMax() < 100.0f)
								{
									AddModifiedBounds(Scene, CacheType, MergedBounds);
								}
								else
								{
									AddModifiedBounds(Scene, CacheType, Mapping.WorldBounds);
									AddModifiedBounds(Scene, CacheType, WorldBounds);
								}
								LogDistanceFieldUpdate(PrimitiveSceneInfo, BoundingRadius, bIsAddOperation);

								Mapping.LocalToWorld = LocalToWorld;
								Mapping.WorldBounds = WorldBounds;
							}
						}
					}
				}
				else if (bIsAddOperation)
				{
					// Set to -1 for zero scale meshes
					PrimitiveSceneInfo->DistanceFieldInstanceIndices[TransformIndex] = -1;
				}
			}
		}
		else
		{
			UE_LOG(LogDistanceField,Verbose,TEXT("Primitive %s %s excluded due to huge bounding radius %f"), *PrimitiveSceneInfo->Proxy->GetOwnerName().ToString(), *PrimitiveSceneInfo->Proxy->GetResourceName().ToString(), BoundingRadius);
		}
	}
	return true;
}

bool ProcessHeightFieldPrimitiveUpdate(
	bool bIsAddOperation,
	FScene* Scene,
	FPrimitiveSceneInfo* PrimitiveSceneInfo,
	TArray<int32>& IndicesToUpdateInObjectBuffers)
{
	FDistanceFieldSceneData& SceneData = Scene->DistanceFieldSceneData;

	UTexture2D* HeightNormalTexture;
	UTexture2D* DiffuseColorTexture;
	UTexture2D* VisibilityTexture;
	FHeightfieldComponentDescription HeightFieldCompDesc(PrimitiveSceneInfo->Proxy->GetLocalToWorld());
	PrimitiveSceneInfo->Proxy->GetHeightfieldRepresentation(HeightNormalTexture, DiffuseColorTexture, VisibilityTexture, HeightFieldCompDesc);

	const uint32 Handle = GHeightFieldTextureAtlas.GetAllocationHandle(HeightNormalTexture);
	if (Handle == INDEX_NONE)
	{
		return false;
	}

	uint32 UploadIdx;
	if (bIsAddOperation)
	{
		UploadIdx = SceneData.NumHeightFieldObjectsInBuffer;
		++SceneData.NumHeightFieldObjectsInBuffer;
		SceneData.HeightfieldPrimitives.Add(PrimitiveSceneInfo);

		const FGlobalDFCacheType CacheType = PrimitiveSceneInfo->Proxy->IsOftenMoving() ? GDF_Full : GDF_MostlyStatic;
		const FBoxSphereBounds Bounds = PrimitiveSceneInfo->Proxy->GetBounds();
		AddModifiedBounds(Scene, CacheType, Bounds.GetBox());

		PrimitiveSceneInfo->DistanceFieldInstanceIndices.Empty(1);
		PrimitiveSceneInfo->DistanceFieldInstanceIndices.Add(UploadIdx);
	}
	else
	{
		UploadIdx = PrimitiveSceneInfo->DistanceFieldInstanceIndices[0];
	}

	IndicesToUpdateInObjectBuffers.Add(UploadIdx);

	return true;
}

bool bVerifySceneIntegrity = false;

void FDeferredShadingSceneRenderer::UpdateGlobalDistanceFieldObjectBuffers(FRDGBuilder& GraphBuilder)
{
	const bool bExecuteInParallel = GDFParallelUpdate != 0 && FApp::ShouldUseThreadingForPerformance();

	FDistanceFieldSceneData& DistanceFieldSceneData = Scene->DistanceFieldSceneData;

	if (GDistanceFieldVolumeTextureAtlas.VolumeTextureRHI
		&& (DistanceFieldSceneData.HasPendingOperations() ||
			DistanceFieldSceneData.PendingThrottledOperations.Num() > 0 ||
			DistanceFieldSceneData.AtlasGeneration != GDistanceFieldVolumeTextureAtlas.GetGeneration()))
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_UpdateObjectData);
		// Multi-GPU support : Updating on all GPUs may be inefficient for AFR. Work is
		// wasted for any objects that update on consecutive frames.
		RDG_GPU_MASK_SCOPE(GraphBuilder, FRHIGPUMask::All());
		RDG_EVENT_SCOPE(GraphBuilder, "UpdateSceneObjectData");

		if (DistanceFieldSceneData.ObjectBuffers == nullptr)
		{
			DistanceFieldSceneData.ObjectBuffers = new FDistanceFieldObjectBuffers();
		}

		if (DistanceFieldSceneData.PendingAddOperations.Num() > 0)
		{
			DistanceFieldSceneData.PendingThrottledOperations.Reserve(DistanceFieldSceneData.PendingThrottledOperations.Num() + DistanceFieldSceneData.PendingAddOperations.Num());
		}

		DistanceFieldSceneData.PendingAddOperations.Append(DistanceFieldSceneData.PendingThrottledOperations);
		DistanceFieldSceneData.PendingThrottledOperations.Reset();

		if (DistanceFieldSceneData.AtlasGeneration != GDistanceFieldVolumeTextureAtlas.GetGeneration())
		{
			DistanceFieldSceneData.AtlasGeneration = GDistanceFieldVolumeTextureAtlas.GetGeneration();

			for (int32 PrimitiveInstanceIndex = 0; PrimitiveInstanceIndex < DistanceFieldSceneData.PrimitiveInstanceMapping.Num(); PrimitiveInstanceIndex++)
			{
				FPrimitiveAndInstance& PrimitiveInstance = DistanceFieldSceneData.PrimitiveInstanceMapping[PrimitiveInstanceIndex];

				// Queue an update of all primitives, since the atlas layout has changed
				if (PrimitiveInstance.InstanceIndex == 0
					&& !DistanceFieldSceneData.HasPendingRemovePrimitive(PrimitiveInstance.Primitive)
					&& !DistanceFieldSceneData.PendingAddOperations.Contains(PrimitiveInstance.Primitive)
					&& !DistanceFieldSceneData.PendingUpdateOperations.Contains(PrimitiveInstance.Primitive))
				{
					DistanceFieldSceneData.PendingUpdateOperations.Add(PrimitiveInstance.Primitive);
				}
			}
		}

		// Process removes before adds, as the adds will overwrite primitive allocation info in DistanceFieldSceneData.SurfelAllocations
		UpdateGlobalDistanceFieldObjectRemoves(Scene);

		if (DistanceFieldSceneData.PendingAddOperations.Num() > 0 || DistanceFieldSceneData.PendingUpdateOperations.Num() > 0)
		{
			TArray<FMatrix> ObjectLocalToWorldTransforms;

			const int32 NumTexelsOneDimX = GDistanceFieldVolumeTextureAtlas.GetSizeX();
			const int32 NumTexelsOneDimY = GDistanceFieldVolumeTextureAtlas.GetSizeY();
			const int32 NumTexelsOneDimZ = GDistanceFieldVolumeTextureAtlas.GetSizeZ();
			const FVector InvTextureDim(1.0f / NumTexelsOneDimX, 1.0f / NumTexelsOneDimY, 1.0f / NumTexelsOneDimZ);

			int32 OriginalNumObjects = DistanceFieldSceneData.NumObjectsInBuffer;
			for (int32 UploadPrimitiveIndex = 0; UploadPrimitiveIndex < DistanceFieldSceneData.PendingAddOperations.Num(); UploadPrimitiveIndex++)
			{
				FPrimitiveSceneInfo* PrimitiveSceneInfo = DistanceFieldSceneData.PendingAddOperations[UploadPrimitiveIndex];

				if (!ProcessPrimitiveUpdate(
					true,
					*this,
					PrimitiveSceneInfo,
					ObjectLocalToWorldTransforms,
					DistanceFieldSceneData.IndicesToUpdateInObjectBuffers))
				{
					DistanceFieldSceneData.PendingThrottledOperations.Add(PrimitiveSceneInfo);
				}
			}

			for (TSet<FPrimitiveSceneInfo*>::TIterator It(DistanceFieldSceneData.PendingUpdateOperations); It; ++It)
			{
				FPrimitiveSceneInfo* PrimitiveSceneInfo = *It;

				ProcessPrimitiveUpdate(
					false,
					*this,
					PrimitiveSceneInfo,
					ObjectLocalToWorldTransforms,
					DistanceFieldSceneData.IndicesToUpdateInObjectBuffers);
			}

			DistanceFieldSceneData.PendingAddOperations.Reset();
			DistanceFieldSceneData.PendingUpdateOperations.Reset();
			if (DistanceFieldSceneData.PendingThrottledOperations.Num() == 0)
			{
				DistanceFieldSceneData.PendingThrottledOperations.Reset();
			}
		}

		// Upload buffer changes
		if (DistanceFieldSceneData.IndicesToUpdateInObjectBuffers.Num() > 0)
		{
			QUICK_SCOPE_CYCLE_COUNTER(UpdateDFObjectBuffers);

			FDistanceFieldObjectBuffers* ObjectBuffers = DistanceFieldSceneData.ObjectBuffers;

			const int32 NumTexelsOneDimX = GDistanceFieldVolumeTextureAtlas.GetSizeX();
			const int32 NumTexelsOneDimY = GDistanceFieldVolumeTextureAtlas.GetSizeY();
			const int32 NumTexelsOneDimZ = GDistanceFieldVolumeTextureAtlas.GetSizeZ();
			const FVector InvTextureDim(1.0f / NumTexelsOneDimX, 1.0f / NumTexelsOneDimY, 1.0f / NumTexelsOneDimZ);

			bool bAnyViewEnabledDistanceCulling = !WITH_EDITOR;
#if WITH_EDITOR
			for (const FViewInfo& ViewInfo : Views)
			{
				if (!ViewInfo.Family->EngineShowFlags.DistanceCulledPrimitives)
				{
					bAnyViewEnabledDistanceCulling = true;
					break;
				}
			}
#endif

			// Upload DF object data and bounds
			{
				const uint32 NumDFObjects = DistanceFieldSceneData.NumObjectsInBuffer;

				const uint32 DFObjectDataNumFloat4s = FMath::RoundUpToPowerOfTwo(NumDFObjects * FDistanceFieldObjectBuffers::ObjectDataStride);
				const uint32 DFObjectDataNumBytes = DFObjectDataNumFloat4s * sizeof(FVector4);
				ResizeResourceIfNeeded(GraphBuilder.RHICmdList, ObjectBuffers->Data, DFObjectDataNumBytes, TEXT("DFObjectData"));

				const uint32 DFObjectBoundsNumFloat4s = FMath::RoundUpToPowerOfTwo(NumDFObjects * FDistanceFieldObjectBuffers::ObjectBoundsStride);
				const uint32 DFObjectBoundsNumBytes = DFObjectBoundsNumFloat4s * sizeof(FVector4);
				ResizeResourceIfNeeded(GraphBuilder.RHICmdList, ObjectBuffers->Bounds, DFObjectBoundsNumBytes, TEXT("DFObjectBounds"));

				const int32 NumDFObjectUploads = DistanceFieldSceneData.IndicesToUpdateInObjectBuffers.Num();

				static FCriticalSection DFUpdateCS;

				if (NumDFObjectUploads > 0)
				{
					DistanceFieldSceneData.UploadDistanceFieldDataBuffer.Init(NumDFObjectUploads, FDistanceFieldObjectBuffers::ObjectDataStride * sizeof(FVector4), true, TEXT("DFObjectDataUploadBuffer"));
					DistanceFieldSceneData.UploadDistanceFieldBoundsBuffer.Init(NumDFObjectUploads, FDistanceFieldObjectBuffers::ObjectBoundsStride * sizeof(FVector4), true, TEXT("DFObjectBoundsUploadBuffer"));

					const TArray<FPrimitiveBounds>& PrimitiveBounds = Scene->PrimitiveBounds;

					FParallelUpdateRangesDFO ParallelRanges;

					int32 RangeCount = PartitionUpdateRangesDFO(ParallelRanges, DistanceFieldSceneData.IndicesToUpdateInObjectBuffers.Num(), bExecuteInParallel);

					ParallelFor(RangeCount,
						[&DistanceFieldSceneData, &ParallelRanges, PrimitiveBounds, InvTextureDim, bAnyViewEnabledDistanceCulling, RangeCount](int32 RangeIndex)
						{
							for (int32 ItemIndex = ParallelRanges.Range[RangeIndex].ItemStart; ItemIndex < ParallelRanges.Range[RangeIndex].ItemStart + ParallelRanges.Range[RangeIndex].ItemCount; ++ItemIndex)
							{
								const int32 Index = DistanceFieldSceneData.IndicesToUpdateInObjectBuffers[ItemIndex];
								if (Index >= 0 && Index < DistanceFieldSceneData.PrimitiveInstanceMapping.Num())
								{
									const FPrimitiveAndInstance& PrimAndInst = DistanceFieldSceneData.PrimitiveInstanceMapping[Index];

									if (RangeCount > 1)
									{
										DFUpdateCS.Lock();
									}

									FVector4* UploadObjectData = (FVector4*)DistanceFieldSceneData.UploadDistanceFieldDataBuffer.Add_GetRef(Index);
									FVector4* UploadObjectBounds = (FVector4*)DistanceFieldSceneData.UploadDistanceFieldBoundsBuffer.Add_GetRef(Index);

									if (RangeCount > 1)
									{
										DFUpdateCS.Unlock();
									}

									FBox LocalVolumeBounds;
									FVector2D DistanceMinMax;
									FIntVector BlockMin;
									FIntVector BlockSize;
									bool bBuiltAsIfTwoSided;
									float SelfShadowBias;
									bool bThrottled;
									PrimAndInst.Primitive->Proxy->GetDistancefieldAtlasData(LocalVolumeBounds, DistanceMinMax, BlockMin, BlockSize, bBuiltAsIfTwoSided, SelfShadowBias, bThrottled);

									const FMatrix LocalToWorld = PrimAndInst.LocalToWorld;

									const FMatrix VolumeToWorld = FScaleMatrix(LocalVolumeBounds.GetExtent())
										* FTranslationMatrix(LocalVolumeBounds.GetCenter())
										* LocalToWorld;

									const FVector4 ObjectBoundingSphere(VolumeToWorld.GetOrigin(), VolumeToWorld.GetScaleVector().Size());
									const FBox WorldSpaceVolumeBounds = LocalVolumeBounds.TransformBy(LocalToWorld);

									UploadObjectBounds[0] = ObjectBoundingSphere;

									const FGlobalDFCacheType CacheType = PrimAndInst.Primitive->Proxy->IsOftenMoving() ? GDF_Full : GDF_MostlyStatic;
									const float OftenMovingValue = CacheType == GDF_Full ? 1.0f : 0.0f;

									const FVector4 ObjectWorldExtent(WorldSpaceVolumeBounds.GetExtent(), OftenMovingValue);
									UploadObjectBounds[1] = ObjectWorldExtent;

									const float MaxExtent = LocalVolumeBounds.GetExtent().GetMax();

									const FMatrix UniformScaleVolumeToWorld =
										  FScaleMatrix(MaxExtent)
										* FTranslationMatrix(LocalVolumeBounds.GetCenter())
										* LocalToWorld;

									const FVector InvBlockSize(1.0f / BlockSize.X, 1.0f / BlockSize.Y, 1.0f / BlockSize.Z);

									//float3 VolumeUV = (VolumePosition / LocalPositionExtent * .5f * UVScale + .5f * UVScale + UVAdd;
									const FVector LocalPositionExtent = LocalVolumeBounds.GetExtent() / FVector(MaxExtent);
									const float VolumeMinScale = UniformScaleVolumeToWorld.GetMinimumAxisScale();

									const FMatrix WorldToVolumeT = UniformScaleVolumeToWorld.Inverse().GetTransposed();
									// WorldToVolumeT
									UploadObjectData[0] = (*(FVector4*)&WorldToVolumeT.M[0]);
									UploadObjectData[1] = (*(FVector4*)&WorldToVolumeT.M[1]);
									UploadObjectData[2] = (*(FVector4*)&WorldToVolumeT.M[2]);

									// Minimal surface bias which increases chance that ray hit will a surface located between two texels
									float ExpandSurfaceDistance = (0.25f * InvBlockSize * LocalVolumeBounds.GetSize()).Size() / MaxExtent;
									if (bBuiltAsIfTwoSided)
									{
										ExpandSurfaceDistance *= 2.0f;
									}

									// LocalPositionExtent
									UploadObjectData[3] = FVector4(LocalPositionExtent, ExpandSurfaceDistance);

									// UVScale, VolumeScale and sign gives bGeneratedAsTwoSided
									const FVector UVScale = FVector(BlockSize) * InvTextureDim * ((FVector(BlockSize) - 2.0f * DistanceField::MeshDistanceFieldBorder) / FVector(BlockSize));
									const float WSign = bBuiltAsIfTwoSided ? -1 : 1;
									UploadObjectData[4] = FVector4(UVScale * 0.5f / LocalPositionExtent, WSign * VolumeMinScale);

									// UVAdd
									UploadObjectData[5] = FVector4((FVector(BlockMin) + DistanceField::MeshDistanceFieldBorder) * InvTextureDim + 0.5f * UVScale, SelfShadowBias);

									// xy - DistanceFieldMAD
									// zw - MinDrawDistance^2, MaxDrawDistance^2
									// [0, 1] -> [MinVolumeDistance, MaxVolumeDistance]
									const int32 PrimIdx = PrimAndInst.Primitive->GetIndex();
									const FPrimitiveBounds& PrimBounds = PrimitiveBounds[PrimIdx];
									float MinDrawDist2 = PrimBounds.MinDrawDistanceSq;
									// For IEEE compatible machines, float operations goes to inf if overflow
									// In this case, it will effectively disable max draw distance culling
									float MaxDrawDist = FMath::Max(PrimBounds.MaxCullDistance, 0.f) * GetCachedScalabilityCVars().ViewDistanceScale;

								#if WITH_EDITOR
									if (!bAnyViewEnabledDistanceCulling)
									{
										MinDrawDist2 = 0.f;
										MaxDrawDist = 0.f;
									}
								#endif

									// This is needed to bypass the check Nan/Inf behavior of FVector4.
									// If the check is turned on, FVector4 constructor automatically converts
									// the FVector4 being constructed to (0, 0, 0, 1) when any of inputs
									// to the constructor contains Nan/Inf
									FVector4& TmpVec4 = UploadObjectData[6];
									TmpVec4.X = DistanceMinMax.Y - DistanceMinMax.X;
									TmpVec4.Y = DistanceMinMax.X;
									TmpVec4.Z = MinDrawDist2;
									TmpVec4.W = MaxDrawDist * MaxDrawDist;

									FMatrix VolumeToWorldT = UniformScaleVolumeToWorld.GetTransposed();
									UploadObjectData[7] = *(FVector4*)&VolumeToWorldT.M[0];
									UploadObjectData[8] = *(FVector4*)&VolumeToWorldT.M[1];
									UploadObjectData[9] = *(FVector4*)&VolumeToWorldT.M[2];

									// Scale from a local space to a volume space
									UploadObjectData[10] = FVector4(MaxExtent * LocalToWorld.GetScaleVector(), 0.0f);
								}
							}
						},
						RangeCount == 1
					);

					AddPass(GraphBuilder, [&DistanceFieldSceneData, ObjectBuffers](FRHICommandListImmediate& RHICmdList)
					{
						RHICmdList.Transition({
							FRHITransitionInfo(ObjectBuffers->Data.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute),
							FRHITransitionInfo(ObjectBuffers->Bounds.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute)
						});

						DistanceFieldSceneData.UploadDistanceFieldDataBuffer.ResourceUploadTo(RHICmdList, ObjectBuffers->Data, false);
						DistanceFieldSceneData.UploadDistanceFieldBoundsBuffer.ResourceUploadTo(RHICmdList, ObjectBuffers->Bounds, false);

						RHICmdList.Transition({
							FRHITransitionInfo(ObjectBuffers->Data.UAV, ERHIAccess::UAVCompute, ERHIAccess::SRVMask),
							FRHITransitionInfo(ObjectBuffers->Bounds.UAV, ERHIAccess::UAVCompute, ERHIAccess::SRVMask)
						});
					});
				}
			}
		}

		check(DistanceFieldSceneData.NumObjectsInBuffer == DistanceFieldSceneData.PrimitiveInstanceMapping.Num());

		if (bVerifySceneIntegrity)
		{
			QUICK_SCOPE_CYCLE_COUNTER(STAT_UpdateObjectData_VerifyIntegrity);
			DistanceFieldSceneData.VerifyIntegrity();
		}
	}

	DistanceFieldSceneData.IndicesToUpdateInObjectBuffers.Reset();
}

void FDeferredShadingSceneRenderer::UpdateGlobalHeightFieldObjectBuffers(FRDGBuilder& GraphBuilder)
{
	FDistanceFieldSceneData& DistanceFieldSceneData = Scene->DistanceFieldSceneData;

	if (GHeightFieldTextureAtlas.GetAtlasTexture()
		&& (DistanceFieldSceneData.HasPendingHeightFieldOperations()
			|| DistanceFieldSceneData.HeightFieldAtlasGeneration != GHeightFieldTextureAtlas.GetGeneration()
			|| DistanceFieldSceneData.HFVisibilityAtlasGenerattion != GHFVisibilityTextureAtlas.GetGeneration()))
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_UpdateHeightFieldSceneObjectData);
		RDG_EVENT_SCOPE(GraphBuilder, "UpdateHeightFieldSceneObjectData");

		if (!DistanceFieldSceneData.HeightFieldObjectBuffers)
		{
			AddOrRemoveSceneHeightFieldPrimitives(true);

			for (int32 Idx = 0; Idx < DistanceFieldSceneData.HeightfieldPrimitives.Num(); ++Idx)
			{
				FPrimitiveSceneInfo* Primitive = DistanceFieldSceneData.HeightfieldPrimitives[Idx];
				check(!DistanceFieldSceneData.PendingHeightFieldAddOps.Contains(Primitive));
				DistanceFieldSceneData.PendingHeightFieldAddOps.Add(Primitive);
			}
			DistanceFieldSceneData.HeightfieldPrimitives.Reset();
			DistanceFieldSceneData.HeightFieldObjectBuffers = new FHeightFieldObjectBuffers;
		}

		if (DistanceFieldSceneData.HeightFieldAtlasGeneration != GHeightFieldTextureAtlas.GetGeneration()
			|| DistanceFieldSceneData.HFVisibilityAtlasGenerattion != GHFVisibilityTextureAtlas.GetGeneration())
		{
			DistanceFieldSceneData.HeightFieldAtlasGeneration = GHeightFieldTextureAtlas.GetGeneration();
			DistanceFieldSceneData.HFVisibilityAtlasGenerattion = GHFVisibilityTextureAtlas.GetGeneration();

			for (int32 Idx = 0; Idx < DistanceFieldSceneData.HeightfieldPrimitives.Num(); ++Idx)
			{
				FPrimitiveSceneInfo* Primitive = DistanceFieldSceneData.HeightfieldPrimitives[Idx];

				if (!DistanceFieldSceneData.HasPendingRemoveHeightFieldPrimitive(Primitive)
					&& !DistanceFieldSceneData.PendingHeightFieldAddOps.Contains(Primitive)
					&& !DistanceFieldSceneData.PendingHeightFieldUpdateOps.Contains(Primitive))
				{
					DistanceFieldSceneData.PendingHeightFieldUpdateOps.Add(Primitive);
				}
			}
		}

		UpdateGlobalHeightFieldObjectRemoves(Scene);

		if (DistanceFieldSceneData.PendingHeightFieldAddOps.Num() || DistanceFieldSceneData.PendingHeightFieldUpdateOps.Num())
		{
			const int32 NumAddOps = DistanceFieldSceneData.PendingHeightFieldAddOps.Num();
			const int32 NumUpdateOps = DistanceFieldSceneData.PendingHeightFieldUpdateOps.Num();
			const int32 NumUploadOps = NumAddOps + NumUpdateOps;
			const int32 OriginalNumObjects = DistanceFieldSceneData.NumHeightFieldObjectsInBuffer;

			for (int32 Idx = 0; Idx < NumAddOps; ++Idx)
			{
				FPrimitiveSceneInfo* PrimitiveSceneInfo = DistanceFieldSceneData.PendingHeightFieldAddOps[Idx];
				ProcessHeightFieldPrimitiveUpdate(true, Scene, PrimitiveSceneInfo, DistanceFieldSceneData.IndicesToUpdateInHeightFieldObjectBuffers);
			}

			for (int32 Idx = 0; Idx < NumUpdateOps; ++Idx)
			{
				FPrimitiveSceneInfo* PrimitiveSceneInfo = DistanceFieldSceneData.PendingHeightFieldUpdateOps[Idx];
				ProcessHeightFieldPrimitiveUpdate(false, Scene, PrimitiveSceneInfo, DistanceFieldSceneData.IndicesToUpdateInHeightFieldObjectBuffers);
			}

			DistanceFieldSceneData.PendingHeightFieldAddOps.Reset();
			DistanceFieldSceneData.PendingHeightFieldUpdateOps.Empty();

			FHeightFieldObjectBuffers*& ObjectBuffers = DistanceFieldSceneData.HeightFieldObjectBuffers;

			// Upload height field object data and bounds
			{
				const uint32 NumHeightFieldObjects = DistanceFieldSceneData.NumHeightFieldObjectsInBuffer;

				const uint32 HeighFieldObjectDataNumFloat4s = FMath::RoundUpToPowerOfTwo(NumHeightFieldObjects * FHeightFieldObjectBuffers::ObjectDataStride);
				const uint32 HeighFieldObjectDataNumBytes = HeighFieldObjectDataNumFloat4s * sizeof(FVector4);
				ResizeResourceIfNeeded(GraphBuilder.RHICmdList, ObjectBuffers->Data, HeighFieldObjectDataNumBytes, TEXT("HeighFieldObjectData"));

				const uint32 HeighFieldObjectBoundsNumFloat4s = FMath::RoundUpToPowerOfTwo(NumHeightFieldObjects * FHeightFieldObjectBuffers::ObjectBoundsStride);
				const uint32 HeighFieldObjectBoundsNumBytes = HeighFieldObjectBoundsNumFloat4s * sizeof(FVector4);
				ResizeResourceIfNeeded(GraphBuilder.RHICmdList, ObjectBuffers->Bounds, HeighFieldObjectBoundsNumBytes, TEXT("HeighFieldObjectBounds"));

				const int32 NumHeighFieldObjectUploads = DistanceFieldSceneData.IndicesToUpdateInHeightFieldObjectBuffers.Num();

				if (NumHeighFieldObjectUploads > 0)
				{
					DistanceFieldSceneData.UploadHeightFieldDataBuffer.Init(NumHeighFieldObjectUploads, FHeightFieldObjectBuffers::ObjectDataStride * sizeof(FVector4), true, TEXT("HeighFieldObjectDataUploadBuffer"));
					DistanceFieldSceneData.UploadHeightFieldBoundsBuffer.Init(NumHeighFieldObjectUploads, FHeightFieldObjectBuffers::ObjectBoundsStride * sizeof(FVector4), true, TEXT("HeighFieldObjectBoundsUploadBuffer"));

					AddPass(GraphBuilder, [this, &DistanceFieldSceneData, &ObjectBuffers](FRHICommandListImmediate& RHICmdList)
					{
						for (int32 Index : DistanceFieldSceneData.IndicesToUpdateInHeightFieldObjectBuffers)
						{
							if (Index >= 0 && Index < DistanceFieldSceneData.HeightfieldPrimitives.Num())
							{
								FPrimitiveSceneInfo* Primitive = DistanceFieldSceneData.HeightfieldPrimitives[Index];

								FVector4* UploadObjectData = (FVector4*)DistanceFieldSceneData.UploadHeightFieldDataBuffer.Add_GetRef(Index);
								FVector4* UploadObjectBounds = (FVector4*)DistanceFieldSceneData.UploadHeightFieldBoundsBuffer.Add_GetRef(Index);

								UTexture2D* HeightNormalTexture;
								UTexture2D* DiffuseColorTexture;
								UTexture2D* VisibilityTexture;
								FHeightfieldComponentDescription HeightFieldCompDesc(Primitive->Proxy->GetLocalToWorld());
								Primitive->Proxy->GetHeightfieldRepresentation(HeightNormalTexture, DiffuseColorTexture, VisibilityTexture, HeightFieldCompDesc);

								const FBoxSphereBounds& Bounds = Primitive->Proxy->GetBounds();
								const FBox BoxBound = Bounds.GetBox();
								UploadObjectBounds[0] = FVector4(BoxBound.GetCenter(), Bounds.SphereRadius);
								UploadObjectBounds[1] = FVector4(BoxBound.GetExtent(), 0.f);

								const FMatrix& LocalToWorld = HeightFieldCompDesc.LocalToWorld;
								check(LocalToWorld.GetMaximumAxisScale() > 0.f);
								const FMatrix WorldToLocalT = LocalToWorld.Inverse().GetTransposed();
								UploadObjectData[0] = *(const FVector4*)&WorldToLocalT.M[0];
								UploadObjectData[1] = *(const FVector4*)&WorldToLocalT.M[1];
								UploadObjectData[2] = *(const FVector4*)&WorldToLocalT.M[2];

								const FIntRect& HeightFieldRect = HeightFieldCompDesc.HeightfieldRect;
								const float WorldToLocalScale = FMath::Min3(
									WorldToLocalT.GetColumn(0).Size(),
									WorldToLocalT.GetColumn(1).Size(),
									WorldToLocalT.GetColumn(2).Size());
								UploadObjectData[3] = FVector4(HeightFieldRect.Width(), HeightFieldRect.Height(), WorldToLocalScale, 0.f);

								FVector4 HeightUVScaleBias(ForceInitToZero);
								if (HeightNormalTexture)
								{
									const uint32 HeightNormalTextureHandle = GHeightFieldTextureAtlas.GetAllocationHandle(HeightNormalTexture);
									if (HeightNormalTextureHandle != INDEX_NONE)
									{
										const FVector4& HeightFieldScaleBias = HeightFieldCompDesc.HeightfieldScaleBias;
										check(HeightFieldScaleBias.Y >= 0.f && HeightFieldScaleBias.Z >= 0.f && HeightFieldScaleBias.W >= 0.f);

										const FVector4 ScaleBias = GHeightFieldTextureAtlas.GetAllocationScaleBias(HeightNormalTextureHandle);
										HeightUVScaleBias.Set(FMath::Abs(HeightFieldScaleBias.X) * ScaleBias.X,
											HeightFieldScaleBias.Y * ScaleBias.Y,
											HeightFieldScaleBias.Z * ScaleBias.X + ScaleBias.Z,
											HeightFieldScaleBias.W * ScaleBias.Y + ScaleBias.W);
									}
								}
								UploadObjectData[4] = HeightUVScaleBias;

								FVector4 VisUVScaleBias(ForceInitToZero);
								if (VisibilityTexture)
								{
									const uint32 VisHandle = GHFVisibilityTextureAtlas.GetAllocationHandle(VisibilityTexture);
									if (VisHandle != INDEX_NONE)
									{
										const FVector4 ScaleBias = GHFVisibilityTextureAtlas.GetAllocationScaleBias(VisHandle);
										VisUVScaleBias = FVector4(1.f / HeightFieldRect.Width() * ScaleBias.X, 1.f / HeightFieldRect.Height() * ScaleBias.Y, ScaleBias.Z, ScaleBias.W);
									}
								}
								UploadObjectData[5] = VisUVScaleBias;
							}
						}

						RHICmdList.Transition({
							FRHITransitionInfo(ObjectBuffers->Data.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute),
							FRHITransitionInfo(ObjectBuffers->Bounds.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute)
						});

						DistanceFieldSceneData.UploadHeightFieldDataBuffer.ResourceUploadTo(RHICmdList, ObjectBuffers->Data, false);
						DistanceFieldSceneData.UploadHeightFieldBoundsBuffer.ResourceUploadTo(RHICmdList, ObjectBuffers->Bounds, false);

						RHICmdList.Transition({
							FRHITransitionInfo(ObjectBuffers->Data.UAV, ERHIAccess::UAVCompute, ERHIAccess::SRVMask),
							FRHITransitionInfo(ObjectBuffers->Bounds.UAV, ERHIAccess::UAVCompute, ERHIAccess::SRVMask)
						});
					});
				}
			}
		}
	}
}

void FDeferredShadingSceneRenderer::AddOrRemoveSceneHeightFieldPrimitives(bool bSkipAdd)
{
	FDistanceFieldSceneData& SceneData = Scene->DistanceFieldSceneData;

	if (SceneData.HeightFieldObjectBuffers)
	{
		delete SceneData.HeightFieldObjectBuffers;
		SceneData.HeightFieldObjectBuffers = nullptr;
		SceneData.NumHeightFieldObjectsInBuffer = 0;
		SceneData.HeightFieldAtlasGeneration = 0;
		SceneData.HFVisibilityAtlasGenerattion = 0;
	}

	TArray<int32, SceneRenderingAllocator> PendingRemoveIndices;
	for (int32 Idx = 0; Idx < SceneData.PendingHeightFieldRemoveOps.Num(); ++Idx)
	{
		const FHeightFieldPrimitiveRemoveInfo& RemoveInfo = SceneData.PendingHeightFieldRemoveOps[Idx];
		check(RemoveInfo.DistanceFieldInstanceIndices.Num() == 1);
		PendingRemoveIndices.Add(RemoveInfo.DistanceFieldInstanceIndices[0]);
		const FGlobalDFCacheType CacheType = RemoveInfo.bOftenMoving ? GDF_Full : GDF_MostlyStatic;
		AddModifiedBounds(Scene, CacheType, RemoveInfo.WorldBounds);
	}
	SceneData.PendingHeightFieldRemoveOps.Reset();
	Algo::Sort(PendingRemoveIndices);
	for (int32 Idx = PendingRemoveIndices.Num() - 1; Idx >= 0; --Idx)
	{
		const int32 RemoveIdx = PendingRemoveIndices[Idx];
		const int32 LastObjectIdx = SceneData.HeightfieldPrimitives.Num() - 1;
		if (RemoveIdx != LastObjectIdx)
		{
			SceneData.HeightfieldPrimitives[LastObjectIdx]->DistanceFieldInstanceIndices[0] = RemoveIdx;
		}
		SceneData.HeightfieldPrimitives.RemoveAtSwap(RemoveIdx);
	}

	if (!bSkipAdd)
	{
		for (int32 Idx = 0; Idx < SceneData.PendingHeightFieldAddOps.Num(); ++Idx)
		{
			FPrimitiveSceneInfo* Primitive = SceneData.PendingHeightFieldAddOps[Idx];
			const int32 HFIdx = SceneData.HeightfieldPrimitives.Add(Primitive);
			Primitive->DistanceFieldInstanceIndices.Empty(1);
			Primitive->DistanceFieldInstanceIndices.Add(HFIdx);
			const FGlobalDFCacheType CacheType = Primitive->Proxy->IsOftenMoving() ? GDF_Full : GDF_MostlyStatic;
			const FBoxSphereBounds& Bounds = Primitive->Proxy->GetBounds();
			AddModifiedBounds(Scene, CacheType, Bounds.GetBox());
		}
		SceneData.PendingHeightFieldAddOps.Reset();
	}

	SceneData.PendingHeightFieldUpdateOps.Empty();
}

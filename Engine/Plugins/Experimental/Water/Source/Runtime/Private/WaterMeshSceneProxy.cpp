// Copyright Epic Games, Inc. All Rights Reserved.

#include "WaterMeshSceneProxy.h"
#include "WaterMeshComponent.h"
#include "WaterVertexFactory.h"
#include "WaterInstanceDataBuffer.h"
#include "WaterSubsystem.h"
#include "SceneManagement.h"
#include "Engine/Engine.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "DrawDebugHelpers.h"
#include "Math/ColorList.h"

DECLARE_STATS_GROUP(TEXT("Water Mesh"), STATGROUP_WaterMesh, STATCAT_Advanced);

DECLARE_DWORD_COUNTER_STAT(TEXT("Tiles Drawn"), STAT_WaterTilesDrawn, STATGROUP_WaterMesh);
DECLARE_DWORD_COUNTER_STAT(TEXT("Draw Calls"), STAT_WaterDrawCalls, STATGROUP_WaterMesh);
DECLARE_DWORD_COUNTER_STAT(TEXT("Vertices Drawn"), STAT_WaterVerticesDrawn, STATGROUP_WaterMesh);
DECLARE_DWORD_COUNTER_STAT(TEXT("Number Drawn Materials"), STAT_WaterDrawnMats, STATGROUP_WaterMesh);

/** Scalability CVars */
static TAutoConsoleVariable<int32> CVarWaterMeshLODMorphEnabled(
	TEXT("r.Water.WaterMesh.LODMorphEnabled"), 1,
	TEXT("If the smooth LOD morph is enabled. Turning this off may cause slight popping between LOD levels but will skip the calculations in the vertex shader, making it cheaper"),
	ECVF_Scalability | ECVF_RenderThreadSafe);

/** Debug CVars */
static TAutoConsoleVariable<int32> CVarWaterMeshShowWireframe(
	TEXT("r.Water.WaterMesh.ShowWireframe"),
	0,
	TEXT("Forces wireframe rendering on for water"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarWaterMeshShowWireframeAtBaseHeight(
	TEXT("r.Water.WaterMesh.ShowWireframeAtBaseHeight"),
	0,
	TEXT("When rendering in wireframe, show the mesh with no displacement"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarWaterMeshEnableRendering(
	TEXT("r.Water.WaterMesh.EnableRendering"),
	1,
	TEXT("Turn off all water rendering from within the scene proxy"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarWaterMeshShowLODLevels(
	TEXT("r.Water.WaterMesh.ShowLODLevels"),
	0,
	TEXT("Shows the LOD levels as concentric squares around the observer position at height 0"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarWaterMeshShowTileBounds(
	TEXT("r.Water.WaterMesh.ShowTileBounds"),
	0,
	TEXT("Shows the tile bounds colored by r.Water.WaterMesh.TileBoundsColor"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarWaterMeshTileBoundsColor(
	TEXT("r.Water.WaterMesh.TileBoundsColor"),
	1,
	TEXT("Color of the tile bounds visualized by r.Water.WaterMesh.ShowTileBounds. 0 is by LOD, 1 is by water body type"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarWaterMeshPreAllocStagingInstanceMemory(
	TEXT("r.Water.WaterMesh.PreAllocStagingInstanceMemory"),
	0,
	TEXT("Pre-allocates staging instance data memory according to historical max. This reduces the overhead when the array needs to grow but may use more memory"),
	ECVF_RenderThreadSafe);


// ----------------------------------------------------------------------------------

bool IsWaterMeshRenderingEnabled_RenderThread()
{
	return IsWaterEnabled(/*bIsRenderThread = */true)
		&& IsWaterMeshEnabled(/*bIsRenderThread = */true)
		&& !!CVarWaterMeshEnableRendering.GetValueOnRenderThread();
}


// ----------------------------------------------------------------------------------

FWaterMeshSceneProxy::FWaterMeshSceneProxy(UWaterMeshComponent* Component)
	: FPrimitiveSceneProxy(Component)
	, MaterialRelevance(Component->GetWaterMaterialRelevance(GetScene().GetFeatureLevel()))
{
	// Cache the tiles and settings
	WaterQuadTree = Component->GetWaterQuadTree();
	// Leaf size * 0.5 equals the tightest possible LOD Scale that doesn't break the morphing. Can be scaled larger
	LODScale = WaterQuadTree.GetLeafSize() * FMath::Max(Component->GetLODScale(), 0.5f);

	// Assign the force collapse level if there is one, otherwise leave it at the default
	if (Component->ForceCollapseDensityLevel > -1)
	{
		ForceCollapseDensityLevel = Component->ForceCollapseDensityLevel;
	}

	int32 NumQuads = (int32)FMath::Pow(2.0f, (float)Component->GetTessellationFactor());

	WaterVertexFactories.Reserve(WaterQuadTree.GetTreeDepth());
	for (uint8 i = 0; i < WaterQuadTree.GetTreeDepth(); i++)
	{
		WaterVertexFactories.Add(new WaterVertexFactoryType(GetScene().GetFeatureLevel(), NumQuads, LODScale, FVector2D(WaterQuadTree.GetBounds().GetCenter())));
		BeginInitResource(WaterVertexFactories.Last());

		NumQuads /= 2;

		// If LODs become too small, early out
		if (NumQuads <= 1)
		{
			break;
		}
	}

	WaterVertexFactories.Shrink();
	DensityCount = WaterVertexFactories.Num();

	const int32 TotalLeafNodes = WaterQuadTree.GetMaxLeafCount();
	WaterInstanceDataBuffers = new WaterInstanceDataBuffersType(TotalLeafNodes);

	WaterMeshUserDataBuffers = new WaterMeshUserDataBuffersType(WaterInstanceDataBuffers);

	// Far distance mesh
	FarDistanceWaterInstanceData = Component->GetFarDistanceInstanceData();
	FarDistanceMaterial = Component->FarDistanceMaterial;

	FarDistanceMaterialIndex = WaterQuadTree.BuildMaterialIndices(FarDistanceMaterial);
}

FWaterMeshSceneProxy::~FWaterMeshSceneProxy()
{
	for (WaterVertexFactoryType* WaterFactory : WaterVertexFactories)
	{
		WaterFactory->ReleaseResource();
		delete WaterFactory;
	}

	delete WaterInstanceDataBuffers;

	delete WaterMeshUserDataBuffers;
}

void FWaterMeshSceneProxy::GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const
{
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(Water);
	TRACE_CPUPROFILER_EVENT_SCOPE(FWaterMeshSceneProxy::GetDynamicMeshElements);

	// The water render groups we have to render for this batch : 
	TArray<EWaterMeshRenderGroupType, TInlineAllocator<WaterVertexFactoryType::NumRenderGroups>> BatchRenderGroups;
	// By default, render all water tiles : 
	BatchRenderGroups.Add(EWaterMeshRenderGroupType::RG_RenderWaterTiles);

#if WITH_WATER_SELECTION_SUPPORT
	bool bHasSelectedInstances = IsSelected();
	const bool bSelectionRenderEnabled = GIsEditor && ViewFamily.EngineShowFlags.Selection;

	if (bSelectionRenderEnabled && bHasSelectedInstances)
	{
		// Don't render all in one group: instead, render 2 groups : first, the selected only then, the non-selected only :
		BatchRenderGroups[0] = EWaterMeshRenderGroupType::RG_RenderSelectedWaterTilesOnly;
		BatchRenderGroups.Add(EWaterMeshRenderGroupType::RG_RenderUnselectedWaterTilesOnly);
	}
#endif // WITH_WATER_SELECTION_SUPPORT

	if (WaterQuadTree.GetNodeCount() == 0 || DensityCount == 0 || !IsWaterMeshRenderingEnabled_RenderThread())
	{
		return;
	}

	// Set up wireframe material (if needed)
	const bool bWireframe = AllowDebugViewmodes() && (ViewFamily.EngineShowFlags.Wireframe || CVarWaterMeshShowWireframe.GetValueOnRenderThread() == 1);

	FColoredMaterialRenderProxy* WireframeMaterialInstance = nullptr;
	if (bWireframe && CVarWaterMeshShowWireframeAtBaseHeight.GetValueOnRenderThread() == 1)
	{
		WireframeMaterialInstance = new FColoredMaterialRenderProxy(
			GEngine->WireframeMaterial ? GEngine->WireframeMaterial->GetRenderProxy() : NULL,
			FColor::Cyan);

		Collector.RegisterOneFrameMaterialProxy(WireframeMaterialInstance);
	}

	const int32 NumBuckets = WaterQuadTree.GetWaterMaterials().Num() * DensityCount;
	const int32 NumFarInstances = FarDistanceWaterInstanceData.Streams[0].Num();
	const bool bHasFarWaterMesh = FarDistanceMaterial && NumFarInstances > 0;
	const int32 FarBucketIndex = FarDistanceMaterialIndex * DensityCount + DensityCount - 1;

	TArray<FWaterQuadTree::FTraversalOutput, TInlineAllocator<4>> WaterInstanceDataPerView;

	// Gather visible tiles, their lod and materials for all views 
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		if (VisibilityMap & (1 << ViewIndex))
		{
			const FSceneView* View = Views[ViewIndex];

			const FVector ObserverPosition = View->ViewMatrices.GetViewOrigin();

			float WaterHeightForLOD = 0.0f;
			WaterQuadTree.QueryInterpolatedTileBaseHeightAtLocation(FVector2D(ObserverPosition), WaterHeightForLOD);

			// Need to let the lowest LOD morph globally towards the next LOD. When the LOD is done morphing, simply clamp the LOD in the LOD selection to effectively promote the lowest LOD to the same LOD level as the one above
			float DistToWater = FMath::Abs(ObserverPosition.Z - WaterHeightForLOD) / LODScale;
			DistToWater = FMath::Max(DistToWater - 2.0f, 0.0f);
			DistToWater *= 2.0f;

			// Clamp to WaterTileQuadTree.GetLODCount() - 1.0f prevents the last LOD to morph
			float FloatLOD = FMath::Clamp(FMath::Log2(DistToWater), 0.0f, WaterQuadTree.GetTreeDepth() - 1.0f);
			float HeightLODFactor = FMath::Frac(FloatLOD);
			int32 LowestLOD = FMath::Clamp(FMath::FloorToInt(FloatLOD), 0, WaterQuadTree.GetTreeDepth() - 1);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			if (CVarWaterMeshShowLODLevels.GetValueOnRenderThread())
			{
				for (int32 i = LowestLOD; i < WaterQuadTree.GetTreeDepth(); i++)
				{
					float LODDist = FWaterQuadTree::GetLODDistance(i, LODScale);
					FVector Orig = FVector(FVector2D(ObserverPosition), WaterHeightForLOD);

					DrawCircle(Collector.GetPDI(ViewIndex), Orig, FVector::ForwardVector, FVector::RightVector, GColorList.GetFColorByIndex(i + 1), LODDist, 64, 0);
				}
			}
#endif
			TRACE_CPUPROFILER_EVENT_SCOPE(QuadTreeTraversalPerView);

			FWaterQuadTree::FTraversalOutput& WaterInstanceData = WaterInstanceDataPerView.Emplace_GetRef();
			WaterInstanceData.BucketInstanceCounts.Empty(NumBuckets);
			WaterInstanceData.BucketInstanceCounts.AddZeroed(NumBuckets);
			if (!!CVarWaterMeshPreAllocStagingInstanceMemory.GetValueOnRenderThread())
			{
				WaterInstanceData.StagingInstanceData.Empty(HistoricalMaxViewInstanceCount);
			}

			FWaterQuadTree::FTraversalDesc TraversalDesc;
			TraversalDesc.LowestLOD = LowestLOD;
			TraversalDesc.HeightMorph = HeightLODFactor;
			TraversalDesc.LODCount = WaterQuadTree.GetTreeDepth();
			TraversalDesc.DensityCount = DensityCount;
			TraversalDesc.ForceCollapseDensityLevel = ForceCollapseDensityLevel;
			TraversalDesc.Frustum = View->ViewFrustum;
			TraversalDesc.ObserverPosition = ObserverPosition;
			TraversalDesc.LODScale = LODScale;
			TraversalDesc.bLODMorphingEnabled = !!CVarWaterMeshLODMorphEnabled.GetValueOnRenderThread();

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			//Debug
			TraversalDesc.DebugPDI = Collector.GetPDI(ViewIndex);
			TraversalDesc.DebugShowTile = !!CVarWaterMeshShowTileBounds.GetValueOnRenderThread();
			TraversalDesc.DebugShowTypeColor = !!CVarWaterMeshTileBoundsColor.GetValueOnRenderThread();
#endif
			WaterQuadTree.BuildWaterTileInstanceData(TraversalDesc, WaterInstanceData);

			HistoricalMaxViewInstanceCount = FMath::Max(HistoricalMaxViewInstanceCount, WaterInstanceData.InstanceCount);

			// Add far distance mesh data to the instance tile data to have it render instanced together with the qater quad tree and possibly merge if it has the same material
			if (bHasFarWaterMesh)
			{
				check(FarDistanceMaterialIndex != INDEX_NONE);
				WaterInstanceData.BucketInstanceCounts[FarBucketIndex] += NumFarInstances;
				WaterInstanceData.InstanceCount += NumFarInstances;
			}
		}
	}

	// Get number of total instances for all views
	int32 TotalInstanceCount = 0;
	for (const FWaterQuadTree::FTraversalOutput& WaterInstanceData : WaterInstanceDataPerView)
	{
		TotalInstanceCount += WaterInstanceData.InstanceCount;
	}

	if (TotalInstanceCount == 0)
	{
		// no instance visible, early exit
		return;
	}

	WaterInstanceDataBuffers->Lock(TotalInstanceCount);

	int32 InstanceDataOffset = 0;

	// Go through all buckets and issue one batched draw call per LOD level per material per view
	int32 TraversalIndex = 0;
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		if (VisibilityMap & (1 << ViewIndex))
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(BucketsPerView);

			FWaterQuadTree::FTraversalOutput& WaterInstanceData = WaterInstanceDataPerView[TraversalIndex];
			const int32 NumWaterMaterials = WaterQuadTree.GetWaterMaterials().Num();
			TraversalIndex++;

			for (int32 MaterialIndex = 0; MaterialIndex < NumWaterMaterials; ++MaterialIndex)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(MaterialBucket);
				bool bMaterialDrawn = false;

				for (int32 DensityIndex = 0; DensityIndex < DensityCount; ++DensityIndex)
				{
					const int32 BucketIndex = MaterialIndex * DensityCount + DensityIndex;
					const int32 InstanceCount = WaterInstanceData.BucketInstanceCounts[BucketIndex];

					if (!InstanceCount)
					{
						continue;
					}

					TRACE_CPUPROFILER_EVENT_SCOPE(DensityBucket);

					FMaterialRenderProxy* MaterialRenderProxy = (WireframeMaterialInstance != nullptr) ? WireframeMaterialInstance : WaterQuadTree.GetWaterMaterials()[MaterialIndex];
					check (MaterialRenderProxy != nullptr);
					const FMaterial* BucketMaterial = MaterialRenderProxy->GetMaterialNoFallback(GetScene().GetFeatureLevel());

					// If the material is not ready for render, just skip :
					if (BucketMaterial == nullptr)
					{
						continue;
					}

					bMaterialDrawn = true;
					for (EWaterMeshRenderGroupType RenderGroup : BatchRenderGroups)
					{
						// Set up mesh batch
						FMeshBatch& Mesh = Collector.AllocateMesh();
						Mesh.bWireframe = bWireframe;
						Mesh.VertexFactory = WaterVertexFactories[DensityIndex];
						Mesh.MaterialRenderProxy = MaterialRenderProxy;
						Mesh.ReverseCulling = IsLocalToWorldDeterminantNegative();
						Mesh.Type = PT_TriangleList;
						Mesh.DepthPriorityGroup = SDPG_World;
						Mesh.bCanApplyViewModeOverrides = false;
						Mesh.bUseForMaterial = true;
						Mesh.CastShadow = false;
						// Preemptively turn off depth rendering for this mesh batch if the material doesn't need it
						Mesh.bUseForDepthPass = !BucketMaterial->GetShadingModels().HasShadingModel(MSM_SingleLayerWater) && BucketMaterial->GetBlendMode() != EBlendMode::BLEND_Translucent;
						Mesh.bUseAsOccluder = false;

#if WITH_WATER_SELECTION_SUPPORT
						Mesh.bUseSelectionOutline = (RenderGroup == EWaterMeshRenderGroupType::RG_RenderSelectedWaterTilesOnly);
						Mesh.bUseWireframeSelectionColoring = (RenderGroup == EWaterMeshRenderGroupType::RG_RenderSelectedWaterTilesOnly);
#endif // WITH_WATER_SELECTION_SUPPORT

						Mesh.Elements.SetNumZeroed(1);

						{
							TRACE_CPUPROFILER_EVENT_SCOPE("Setup batch element");

							// Set up one mesh batch element
							FMeshBatchElement& BatchElement = Mesh.Elements[0];

							// Set up for instancing
							//BatchElement.bIsInstancedMesh = true;
							BatchElement.NumInstances = InstanceCount;
							BatchElement.UserData = (void*)WaterMeshUserDataBuffers->GetUserData(RenderGroup);
							BatchElement.UserIndex = InstanceDataOffset;

							BatchElement.FirstIndex = 0;
							BatchElement.NumPrimitives = WaterVertexFactories[DensityIndex]->IndexBuffer->GetIndexCount() / 3;
							BatchElement.MinVertexIndex = 0;
							BatchElement.MaxVertexIndex = WaterVertexFactories[DensityIndex]->VertexBuffer->GetVertexCount() - 1;

							// Don't use primitive buffer
							BatchElement.IndexBuffer = WaterVertexFactories[DensityIndex]->IndexBuffer;
							BatchElement.PrimitiveIdMode = PrimID_ForceZero;
							BatchElement.PrimitiveUniformBufferResource = &GIdentityPrimitiveUniformBuffer;
						}

						{
							INC_DWORD_STAT_BY(STAT_WaterVerticesDrawn, WaterVertexFactories[DensityIndex]->VertexBuffer->GetVertexCount() * InstanceCount);
							INC_DWORD_STAT(STAT_WaterDrawCalls);
							INC_DWORD_STAT_BY(STAT_WaterTilesDrawn, InstanceCount);

							TRACE_CPUPROFILER_EVENT_SCOPE("Collector.AddMesh");

							Collector.AddMesh(ViewIndex, Mesh);
						}
					}

					WaterInstanceData.BucketInstanceCounts[BucketIndex] = InstanceDataOffset;
					InstanceDataOffset += InstanceCount;
				}

				INC_DWORD_STAT_BY(STAT_WaterDrawnMats, (int32)bMaterialDrawn);
			}

			const int32 NumStagingInstances = WaterInstanceData.StagingInstanceData.Num();
			for (int32 Idx = 0; Idx < NumStagingInstances; ++Idx)
			{
				const FWaterQuadTree::FStagingInstanceData& Data = WaterInstanceData.StagingInstanceData[Idx];
				const int32 WriteIndex = WaterInstanceData.BucketInstanceCounts[Data.BucketIndex]++;

				for (int32 StreamIdx = 0; StreamIdx < WaterInstanceDataBuffersType::NumBuffers; ++StreamIdx)
				{
					WaterInstanceDataBuffers->GetBufferMemory(StreamIdx)[WriteIndex] = Data.Data[StreamIdx];
				}
			}

			if (bHasFarWaterMesh)
			{
				check(FarDistanceWaterInstanceData.Streams[1].Num() == NumFarInstances);
				const int32 WriteStartOffset = WaterInstanceData.BucketInstanceCounts[FarBucketIndex];

				for (int32 StreamIdx = 0; StreamIdx < WaterInstanceDataBuffersType::NumBuffers; ++StreamIdx)
				{
					FMemory::Memcpy(WaterInstanceDataBuffers->GetBufferMemory(StreamIdx) + WriteStartOffset, FarDistanceWaterInstanceData.Streams[StreamIdx].GetData(), NumFarInstances * sizeof(FVector4));
				}
			}
		}
	}

	WaterInstanceDataBuffers->Unlock();
}

FPrimitiveViewRelevance FWaterMeshSceneProxy::GetViewRelevance(const FSceneView* View) const
{
	FPrimitiveViewRelevance Result;
	Result.bDrawRelevance = IsShown(View);
	Result.bShadowRelevance = false;
	Result.bDynamicRelevance = true;
	Result.bStaticRelevance = false;
	Result.bRenderInMainPass = ShouldRenderInMainPass();
	Result.bUsesLightingChannels = GetLightingChannelMask() != GetDefaultLightingChannelMask();
	Result.bRenderCustomDepth = ShouldRenderCustomDepth();
	Result.bTranslucentSelfShadow = bCastVolumetricTranslucentShadow;
	MaterialRelevance.SetPrimitiveViewRelevance(Result);
	Result.bVelocityRelevance = false;
	return Result;
}

#if WITH_WATER_SELECTION_SUPPORT
HHitProxy* FWaterMeshSceneProxy::CreateHitProxies(UPrimitiveComponent* Component, TArray<TRefCountPtr<HHitProxy> >& OutHitProxies)
{
	WaterQuadTree.GatherHitProxies(OutHitProxies);

	// No default hit proxy.
	return nullptr;
}
#endif // WITH_WATER_SELECTION_SUPPORT
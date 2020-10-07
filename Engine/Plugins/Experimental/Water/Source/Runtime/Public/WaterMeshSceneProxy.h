// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "PrimitiveSceneProxy.h"
#include "WaterQuadTree.h"
#include "Engine/Classes/Materials/Material.h"
#include "WaterVertexFactory.h"
#include "WaterInstanceDataBuffer.h"

class UWaterMeshComponent;

/** Water mesh scene proxy */

class FWaterMeshSceneProxy final : public FPrimitiveSceneProxy
{
public:
	SIZE_T GetTypeHash() const override
	{
		static size_t UniquePointer;
		return reinterpret_cast<size_t>(&UniquePointer);
	}

	FWaterMeshSceneProxy(UWaterMeshComponent* Component);

	virtual ~FWaterMeshSceneProxy();

	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override;

	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override;

	virtual bool CanBeOccluded() const override
	{
		return !MaterialRelevance.bDisableDepthTest;
	}

	virtual uint32 GetMemoryFootprint() const
	{
		return(sizeof(*this) + GetAllocatedSize());
	}

	uint32 GetAllocatedSize() const
	{
		return(FPrimitiveSceneProxy::GetAllocatedSize());
	}

#if WITH_WATER_SELECTION_SUPPORT
	virtual HHitProxy* CreateHitProxies(UPrimitiveComponent* Component, TArray<TRefCountPtr<HHitProxy> >& OutHitProxies) override;
#endif // WITH_WATER_SELECTION_SUPPORT

	// At runtime, we only ever need one version of the vertex factory : with selection support (editor) or without : 
	using WaterVertexFactoryType = TWaterVertexFactory<WITH_WATER_SELECTION_SUPPORT>;
	using WaterInstanceDataBuffersType = TWaterInstanceDataBuffers<WITH_WATER_SELECTION_SUPPORT>;
	using WaterMeshUserDataBuffersType = TWaterMeshUserDataBuffers<WITH_WATER_SELECTION_SUPPORT>;

private:

	FMaterialRelevance MaterialRelevance;

	// One vertex factory per LOD
	TArray<WaterVertexFactoryType*> WaterVertexFactories;

	/** Tiles containing water, stored in a quad tree */
	FWaterQuadTree WaterQuadTree;

	/** Unique Instance data buffer shared accross water batch draw calls */	
	WaterInstanceDataBuffersType* WaterInstanceDataBuffers;

	/** Per-"water render group" user data (the number of groups might vary depending on whether we're in the editor or not) */
	WaterMeshUserDataBuffersType* WaterMeshUserDataBuffers;

	/** Scale of the concentric LOD squares  */
	float LODScale = -1.0f;

	/** Number of densities (same as number of grid index/vertex buffers) */
	int32 DensityCount = 0;

	int32 ForceCollapseDensityLevel = TNumericLimits<int32>::Max();

	mutable int32 HistoricalMaxViewInstanceCount = 0;

	/** Instance data for the far distance mesh */
	int32 FarDistanceMaterialIndex;
	FWaterTileInstanceData FarDistanceWaterInstanceData;
	UMaterialInterface* FarDistanceMaterial = nullptr;
};

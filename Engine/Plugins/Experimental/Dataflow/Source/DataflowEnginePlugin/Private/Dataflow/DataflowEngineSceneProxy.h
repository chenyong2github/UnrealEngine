// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Async/ParallelFor.h"
#include "Engine/CollisionProfile.h"
#include "PrimitiveSceneProxy.h"
#include "Materials/Material.h"
#include "DynamicMeshBuilder.h"
#include "StaticMeshResources.h"

class UDataflowComponent;
struct FManagedArrayCollection;

struct FDataflowTriangleSetMeshBatchData
{
	FMaterialRenderProxy* MaterialProxy = nullptr;
	int32 FirstTriangleIndex = INDEX_NONE;
	int32 NumTriangles = INDEX_NONE;
	int32 MinVertexIndex = INDEX_NONE;
	int32 MaxVertexIndex = INDEX_NONE;
	int32 GeomIndex = INDEX_NONE;
};

class FDataflowEngineSceneProxy final : public FPrimitiveSceneProxy
{
public:

	FDataflowEngineSceneProxy(UDataflowComponent* Component);

	virtual ~FDataflowEngineSceneProxy();


	//~ FPrimitiveSceneProxy
	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override;
	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override;
	virtual bool CanBeOccluded() const override;
	virtual uint32 GetMemoryFootprint() const override { return sizeof(*this) + GetAllocatedSize(); }
	virtual SIZE_T GetTypeHash() const override;
	virtual void CreateRenderThreadResources() override;
	virtual void DestroyRenderThreadResources() override;

#if WITH_EDITOR
	virtual HHitProxy* CreateHitProxies(UPrimitiveComponent* Component, TArray<TRefCountPtr<HHitProxy> >& OutHitProxies) override;
#endif // WITH_EDITOR


protected:

	/** Create the rendering buffer resources */
	void InitResources();

	/** Return the rendering buffer resources */
	void ReleaseResources();

private:
	TArray<FDataflowTriangleSetMeshBatchData> MeshBatchDatas;
	FMaterialRelevance MaterialRelevance;
	FLocalVertexFactory VertexFactory;
	FStaticMeshVertexBuffers VertexBuffers;
	FDynamicMeshIndexBuffer32 IndexBuffer;
#if WITH_EDITOR
	FColorVertexBuffer HitProxyIdBuffer;
	TArray<TRefCountPtr<HHitProxy> > LocalHitProxies;
#endif // WITH_EDITOR

	// Render thread copy of data. 
	UMaterialInterface* RenderMaterial = nullptr;
	FManagedArrayCollection* ConstantData = nullptr;
};




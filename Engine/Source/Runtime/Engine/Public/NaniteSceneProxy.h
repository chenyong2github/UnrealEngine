// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PrimitiveSceneProxy.h"
#include "PrimitiveViewRelevance.h"
#include "Rendering/NaniteResources.h"
#include "RayTracingInstance.h"

namespace Nanite
{

class FSceneProxyBase : public FPrimitiveSceneProxy
{
public:
	struct FMaterialSection
	{
		UMaterialInterface* Material = nullptr;
	#if WITH_EDITOR
		HHitProxy* HitProxy = nullptr;
	#endif
		int32 MaterialIndex = INDEX_NONE;
	};

public:
	ENGINE_API SIZE_T GetTypeHash() const override;

	FSceneProxyBase(UPrimitiveComponent* Component)
	: FPrimitiveSceneProxy(Component)
	{
		bIsNaniteMesh  = true;
		bAlwaysVisible = true;
	}

	virtual ~FSceneProxyBase() = default;

	static bool IsNaniteRenderable(FMaterialRelevance MaterialRelevance)
	{
		return MaterialRelevance.bOpaque &&
			!MaterialRelevance.bDecal &&
			!MaterialRelevance.bMasked &&
			!MaterialRelevance.bNormalTranslucency &&
			!MaterialRelevance.bSeparateTranslucency;
	}

	virtual bool CanBeOccluded() const override
	{
		// Disable slow occlusion paths(Nanite does its own occlusion culling)
		return false;
	}

	inline const TArray<FMaterialSection>& GetMaterialSections() const
	{
		return MaterialSections;
	}

	inline int32 GetMaterialMaxIndex() const
	{
		return MaterialMaxIndex;
	}

	virtual const TArray<FPrimitiveInstance>* GetPrimitiveInstances() const
	{
		return &Instances;
	}

	virtual TArray<FPrimitiveInstance>* GetPrimitiveInstances()
	{
		return &Instances;
	}

	// Nanite always uses LOD 0, and performs custom LOD streaming.
	virtual uint8 GetCurrentFirstLODIdx_RenderThread() const override { return 0; }

protected:
	ENGINE_API void DrawStaticElementsInternal(FStaticPrimitiveDrawInterface* PDI, const FLightCacheInterface* LCI);

protected:
	TArray<FMaterialSection> MaterialSections;
	TArray<FPrimitiveInstance> Instances;
	int32 MaterialMaxIndex = INDEX_NONE;
};

class FSceneProxy : public FSceneProxyBase
{
public:
	FSceneProxy(UStaticMeshComponent* Component);
	FSceneProxy(UInstancedStaticMeshComponent* Component);
	FSceneProxy(UHierarchicalInstancedStaticMeshComponent* Component);

	virtual ~FSceneProxy() = default;

public:
	// FPrimitiveSceneProxy interface.
	virtual FPrimitiveViewRelevance	GetViewRelevance(const FSceneView* View) const override;
	virtual void GetLightRelevance(const FLightSceneProxy* LightSceneProxy, bool& bDynamic, bool& bRelevant, bool& bLightMapped, bool& bShadowMapped) const override;

#if WITH_EDITOR
	virtual HHitProxy* CreateHitProxies(UPrimitiveComponent* Component, TArray<TRefCountPtr<HHitProxy> >& OutHitProxies) override;
#endif
	virtual void DrawStaticElements(FStaticPrimitiveDrawInterface* PDI) override;
	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override;

#if RHI_RAYTRACING
	virtual bool IsRayTracingRelevant() const { return true; }
	virtual bool IsRayTracingStaticRelevant() const { return true; }
	virtual void GetDynamicRayTracingInstances(FRayTracingMaterialGatheringContext& Context, TArray<struct FRayTracingInstance>& OutRayTracingInstances) override;
	virtual ERayTracingPrimitiveFlags GetCachedRayTracingInstance(FRayTracingInstance& RayTracingInstance) override;
#endif

	virtual uint32 GetMemoryFootprint() const override;

	virtual void GetLCIs(FLCIArray& LCIs) override
	{
		FLightCacheInterface* LCI = &MeshInfo;
		LCIs.Add(LCI);
	}

	virtual void GetDistancefieldAtlasData(const FDistanceFieldVolumeData*& OutDistanceFieldData, float& SelfShadowBias) const override;
	virtual void GetDistancefieldInstanceData(TArray<FMatrix>& ObjectLocalToWorldTransforms) const override;
	virtual bool HasDistanceFieldRepresentation() const override;

	virtual const FCardRepresentationData* GetMeshCardRepresentation() const override;

	virtual int32 GetLightMapCoordinateIndex() const override;

	virtual void OnTransformChanged() override;

	const UStaticMesh* GetStaticMesh() const
	{
		return StaticMesh;
	}

protected:
	virtual void CreateRenderThreadResources() override;

	class FMeshInfo : public FLightCacheInterface
	{
	public:
		FMeshInfo(const UStaticMeshComponent* InComponent);

		// FLightCacheInterface.
		virtual FLightInteraction GetInteraction(const FLightSceneProxy* LightSceneProxy) const override;

	private:
		TArray<FGuid> IrrelevantLights;
	};

	bool IsCollisionView(const FEngineShowFlags& EngineShowFlags, bool& bDrawSimpleCollision, bool& bDrawComplexCollision) const;

protected:
	FMeshInfo MeshInfo;

	FResources* Resources = nullptr;

	const FStaticMeshRenderData* RenderData;
	const FDistanceFieldVolumeData* DistanceFieldData;
	const FCardRepresentationData* CardRepresentationData;

	// TODO: Should probably calculate this on the materials array above instead of on the component
	//       Null and !Opaque are assigned default material unlike the component material relevance.
	FMaterialRelevance MaterialRelevance;

	uint32 bReverseCulling : 1;
	uint32 bHasMaterialErrors : 1;

	const UStaticMesh* StaticMesh = nullptr;

#if RHI_RAYTRACING
	bool bHasRayTracingInstances = false;
	bool bCachedRayTracingInstanceTransformsValid = false;
	const FRayTracingGeometry* RayTracingGeometry = nullptr;
	TArray<FMatrix> CachedRayTracingInstanceTransforms;
	TArray<FMeshBatch> CachedRayTracingMaterials;
	FRayTracingMaskAndFlags CachedRayTracingInstanceMaskAndFlags;
#endif

#if NANITE_ENABLE_DEBUG_RENDERING
	AActor* Owner;

	/** LightMap resolution used for VMI_LightmapDensity */
	int32 LightMapResolution;

	/** Body setup for collision debug rendering */
	UBodySetup* BodySetup;

	/** Collision trace flags */
	ECollisionTraceFlag CollisionTraceFlag;

	/** Collision Response of this component */
	FCollisionResponseContainer CollisionResponse;

	/** LOD used for collision */
	int32 LODForCollision;

	/** Draw mesh collision if used for complex collision */
	uint32 bDrawMeshCollisionIfComplex : 1;

	/** Draw mesh collision if used for simple collision */
	uint32 bDrawMeshCollisionIfSimple : 1;
#endif
};

} // namespace Nanite


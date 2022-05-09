// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PrimitiveSceneProxy.h"
#include "PrimitiveViewRelevance.h"
#include "Rendering/NaniteResources.h"
#include "RayTracingInstance.h"

struct FPerInstanceRenderData;

namespace Nanite
{

struct FMaterialAuditEntry
{
	UMaterialInterface* Material = nullptr;

	FName MaterialSlotName;
	int32 MaterialIndex = INDEX_NONE;

	uint8 bHasAnyError				: 1;
	uint8 bHasNullMaterial			: 1;
	uint8 bHasWorldPositionOffset	: 1;
	uint8 bHasUnsupportedBlendMode	: 1;
	uint8 bHasPixelDepthOffset		: 1;
	uint8 bHasVertexInterpolator	: 1;
	uint8 bHasPerInstanceRandomID	: 1;
	uint8 bHasPerInstanceCustomData	: 1;
	uint8 bHasInvalidUsage			: 1;
};

struct FMaterialAudit
{
	FString AssetName;
	TArray<FMaterialAuditEntry, TInlineAllocator<4>> Entries;
	uint8 bHasAnyError : 1;

	FORCEINLINE UMaterialInterface* GetMaterial(int32 MaterialIndex) const
	{
		if (Entries.IsValidIndex(MaterialIndex))
		{
			return Entries[MaterialIndex].Material;
		}

		return nullptr;
	}

	FORCEINLINE bool HasPerInstanceRandomID(int32 MaterialIndex) const
	{
		if (Entries.IsValidIndex(MaterialIndex))
		{
			return Entries[MaterialIndex].bHasPerInstanceRandomID;
		}

		return false;
	}

	FORCEINLINE bool HasPerInstanceCustomData(int32 MaterialIndex) const
	{
		if (Entries.IsValidIndex(MaterialIndex))
		{
			return Entries[MaterialIndex].bHasPerInstanceCustomData;
		}

		return false;
	}
};

ENGINE_API void AuditMaterials(const UStaticMeshComponent* Component, FMaterialAudit& Audit);
ENGINE_API void FixupMaterials(FMaterialAudit& Audit);
ENGINE_API bool IsSupportedBlendMode(EBlendMode Mode);
ENGINE_API bool IsSupportedMaterialDomain(EMaterialDomain Domain);
ENGINE_API bool IsWorldPositionOffsetSupported();

enum class EFilterFlags : uint8
{
	None				= 0u,
	InstancedStaticMesh = (1u << 0u),
	Foliage				= (1u << 1u),
	Grass				= (1u << 2u),
};

ENUM_CLASS_FLAGS(EFilterFlags)

class FSceneProxyBase : public FPrimitiveSceneProxy
{
public:
#if WITH_EDITOR
	enum class EHitProxyMode : uint8
	{
		MaterialSection,
		PerInstance,
	};
#endif

	struct FMaterialSection
	{
		FMaterialRenderProxy* RasterMaterialProxy = nullptr;
		FMaterialRenderProxy* ShadingMaterialProxy = nullptr;

	#if WITH_EDITOR
		HHitProxy* HitProxy = nullptr;
	#endif
		int32 MaterialIndex = INDEX_NONE;

		FMaterialRelevance MaterialRelevance;

		uint8 bHasPerInstanceRandomID : 1;
		uint8 bHasPerInstanceCustomData : 1;
	};

public:
	ENGINE_API SIZE_T GetTypeHash() const override;

	ENGINE_API FSceneProxyBase(UPrimitiveComponent* Component)
	: FPrimitiveSceneProxy(Component)
	{
		bIsNaniteMesh  = true;
		bHasProgrammableRaster = false;
		bEvaluateWorldPositionOffset = false;
	}

	ENGINE_API virtual ~FSceneProxyBase() = default;

#if WITH_EDITOR
	ENGINE_API virtual HHitProxy* CreateHitProxies(UPrimitiveComponent* Component, TArray<TRefCountPtr<HHitProxy>>& OutHitProxies) override;
#endif

	ENGINE_API bool SetEvaluateWorldPositionOffset(bool NewValue);

	virtual bool CanBeOccluded() const override
	{
		// Disable slow occlusion paths(Nanite does its own occlusion culling)
		return false;
	}

	inline bool HasProgrammableRaster() const
	{
		return bHasProgrammableRaster;
	}

	inline bool EvaluateWorldPositionOffset() const
	{
		return bEvaluateWorldPositionOffset;
	}

	inline const TArray<FMaterialSection>& GetMaterialSections() const
	{
		return MaterialSections;
	}

	inline TArray<FMaterialSection>& GetMaterialSections()
	{
		return MaterialSections;
	}

	inline int32 GetMaterialMaxIndex() const
	{
		return MaterialMaxIndex;
	}

	inline EFilterFlags GetFilterFlags() const
	{
		return FilterFlags;
	}

#if WITH_EDITOR
	inline const TConstArrayView<const FHitProxyId> GetHitProxyIds() const
	{
		return HitProxyIds;
	}

	inline EHitProxyMode GetHitProxyMode() const
	{
		return HitProxyMode;
	}
#endif

	void UpdateMaterialDynamicDataUsage()
	{
		bHasPerInstanceCustomData	= false;
		bHasPerInstanceRandom		= false;

		// Checks if any assigned material uses special features
		for (const FMaterialSection& MaterialSection : MaterialSections)
		{
			bHasPerInstanceCustomData	|= MaterialSection.bHasPerInstanceCustomData;
			bHasPerInstanceRandom		|= MaterialSection.bHasPerInstanceRandomID;

			if (bHasPerInstanceCustomData && bHasPerInstanceRandom)
			{
				break;
			}
		}
	}

	// Nanite always uses LOD 0, and performs custom LOD streaming.
	virtual uint8 GetCurrentFirstLODIdx_RenderThread() const override { return 0; }

protected:
	ENGINE_API void DrawStaticElementsInternal(FStaticPrimitiveDrawInterface* PDI, const FLightCacheInterface* LCI);

protected:
	TArray<FMaterialSection> MaterialSections;
#if WITH_EDITOR
	TArray<FHitProxyId> HitProxyIds;
	EHitProxyMode HitProxyMode = EHitProxyMode::MaterialSection;
#endif
	int32 MaterialMaxIndex = INDEX_NONE;
	EFilterFlags FilterFlags = EFilterFlags::None;
	uint8 bHasProgrammableRaster : 1;
	uint8 bEvaluateWorldPositionOffset : 1;
};

class FSceneProxy : public FSceneProxyBase
{
public:
	using Super = FSceneProxyBase;

	FSceneProxy(UStaticMeshComponent* Component);
	FSceneProxy(UInstancedStaticMeshComponent* Component);
	FSceneProxy(UHierarchicalInstancedStaticMeshComponent* Component);

	virtual ~FSceneProxy();

public:
	// FPrimitiveSceneProxy interface.
	virtual FPrimitiveViewRelevance	GetViewRelevance(const FSceneView* View) const override;
	virtual void GetLightRelevance(const FLightSceneProxy* LightSceneProxy, bool& bDynamic, bool& bRelevant, bool& bLightMapped, bool& bShadowMapped) const override;

#if WITH_EDITOR
	virtual HHitProxy* CreateHitProxies(UPrimitiveComponent* Component, TArray<TRefCountPtr<HHitProxy>>& OutHitProxies) override;
#endif
	virtual void DrawStaticElements(FStaticPrimitiveDrawInterface* PDI) override;
	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override;

#if RHI_RAYTRACING
	virtual bool HasRayTracingRepresentation() const override;
	virtual bool IsRayTracingRelevant() const { return true; }
	virtual bool IsRayTracingStaticRelevant() const { return true; }
	virtual void GetDynamicRayTracingInstances(FRayTracingMaterialGatheringContext& Context, TArray<struct FRayTracingInstance>& OutRayTracingInstances) override;
	virtual ERayTracingPrimitiveFlags GetCachedRayTracingInstance(FRayTracingInstance& RayTracingInstance) override;
	virtual Nanite::CoarseMeshStreamingHandle GetCoarseMeshStreamingHandle() const override { return CoarseMeshStreamingHandle; }
#endif

	virtual uint32 GetMemoryFootprint() const override;

	virtual void GetLCIs(FLCIArray& LCIs) override
	{
		FLightCacheInterface* LCI = &MeshInfo;
		LCIs.Add(LCI);
	}

	virtual void GetDistanceFieldAtlasData(const FDistanceFieldVolumeData*& OutDistanceFieldData, float& SelfShadowBias) const override;
	virtual void GetDistanceFieldInstanceData(TArray<FRenderTransform>& InstanceLocalToPrimitiveTransforms) const override;
	virtual bool HasDistanceFieldRepresentation() const override;

	virtual const FCardRepresentationData* GetMeshCardRepresentation() const override;

	virtual int32 GetLightMapCoordinateIndex() const override;

	virtual void OnTransformChanged() override;

	virtual void GetNaniteResourceInfo(uint32& ResourceID, uint32& HierarchyOffset, uint32& ImposterIndex) const override
	{
		ResourceID = Resources->RuntimeResourceID;
		HierarchyOffset = Resources->HierarchyOffset;
		ImposterIndex = Resources->ImposterIndex;
	}

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

#if RHI_RAYTRACING
	int32 GetFirstValidRaytracingGeometryLODIndex() const;
	void SetupRayTracingMaterials(int32 LODIndex, TArray<FMeshBatch>& Materials) const;
#endif // RHI_RAYTRACING

protected:
	FMeshInfo MeshInfo;

	const FResources* Resources = nullptr;

	const FStaticMeshRenderData* RenderData;
	const FDistanceFieldVolumeData* DistanceFieldData;
	const FCardRepresentationData* CardRepresentationData;

	FMaterialRelevance CombinedMaterialRelevance;

	uint32 bReverseCulling : 1;
	uint32 bHasMaterialErrors : 1;

	const UStaticMesh* StaticMesh = nullptr;

	/** Per instance render data, could be shared with component */
	TSharedPtr<FPerInstanceRenderData, ESPMode::ThreadSafe> PerInstanceRenderData;

#if WITH_EDITOR
	/* If we we have any selected instances */
	bool bHasSelectedInstances;
#else
	static const bool bHasSelectedInstances = false;
#endif

#if RHI_RAYTRACING
	bool bHasRayTracingInstances = false;
	bool bCachedRayTracingInstanceTransformsValid = false;
	Nanite::CoarseMeshStreamingHandle CoarseMeshStreamingHandle = INDEX_NONE;
	int16 CachedRayTracingMaterialsLODIndex = INDEX_NONE;
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


// Copyright Epic Games, Inc. All Rights Reserved.

/*==============================================================================
NiagaraRendererSprites.h: Renderer for rendering Niagara particles as sprites.
==============================================================================*/

#pragma once

#include "NiagaraRenderer.h"
#include "NiagaraSpriteRendererProperties.h"

struct FNiagaraDynamicDataSprites;

/**
* FNiagaraRendererSprites renders an FNiagaraEmitterInstance as sprite particles
*/
class NIAGARA_API FNiagaraRendererSprites : public FNiagaraRenderer
{
public:
	FNiagaraRendererSprites(ERHIFeatureLevel::Type FeatureLevel, const UNiagaraRendererProperties *InProps, const FNiagaraEmitterInstance* Emitter);
	~FNiagaraRendererSprites();

	//FNiagaraRenderer interface
	virtual void CreateRenderThreadResources(NiagaraEmitterInstanceBatcher* Batcher)override;
	virtual void ReleaseRenderThreadResources()override;

	virtual int32 GetMaxIndirectArgs() const override;

	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector, const FNiagaraSceneProxy *SceneProxy) const override;
	virtual FNiagaraDynamicDataBase* GenerateDynamicData(const FNiagaraSceneProxy* Proxy, const UNiagaraRendererProperties* InProperties, const FNiagaraEmitterInstance* Emitter) const override;
	virtual int GetDynamicDataSize()const override;
	virtual bool IsMaterialValid(const UMaterialInterface* Mat)const override;

#if RHI_RAYTRACING
		virtual void GetDynamicRayTracingInstances(FRayTracingMaterialGatheringContext& Context, TArray<FRayTracingInstance>& OutRayTracingInstances, const FNiagaraSceneProxy* Proxy) final override;
#endif
	//FNiagaraRenderer interface END

private:
	struct FCPUSimParticleDataAllocation
	{
		FGlobalDynamicReadBuffer& DynamicReadBuffer;
		FParticleRenderData ParticleData;
		FGlobalDynamicReadBuffer::FAllocation IntData;
	};

	FCPUSimParticleDataAllocation ConditionalAllocateCPUSimParticleData(FNiagaraDynamicDataSprites *DynamicDataSprites, const FNiagaraRendererLayout* RendererLayout, FGlobalDynamicReadBuffer& DynamicReadBuffer, bool bNeedsGPUVis) const;
	TUniformBufferRef<class FNiagaraSpriteUniformParameters> CreatePerViewUniformBuffer(const FSceneView* View, const FSceneViewFamily& ViewFamily, const FNiagaraSceneProxy *SceneProxy, const FNiagaraRendererLayout* RendererLayout, const FNiagaraDynamicDataSprites* DynamicDataSprites) const;
	void SetVertexFactoryParticleData(
		class FNiagaraSpriteVertexFactory& VertexFactory,
		int32& OutCulledGPUParticleCountOffset,
		FNiagaraDynamicDataSprites* DynamicDataSprites,
		FCPUSimParticleDataAllocation& CPUSimParticleDataAllocation,
		const FSceneView* View,
		class FNiagaraSpriteVFLooseParameters& VFLooseParams,
		const FNiagaraSceneProxy* SceneProxy,
		const FNiagaraRendererLayout* RendererLayout
	) const;
	void CreateMeshBatchForView(
		const FSceneView* View,
		const FSceneViewFamily& ViewFamily,
		const FNiagaraSceneProxy* SceneProxy,
		int32 CulledGPUParticleCountOffset,
		FNiagaraDynamicDataSprites* DynamicDataSprites,
		FMeshBatch& OutMeshBatch,
		class FNiagaraSpriteVFLooseParameters& VFLooseParams,
		class FNiagaraMeshCollectorResourcesSprite& OutCollectorResources,
		const FNiagaraRendererLayout* RendererLayout
	) const;

	//Cached data from the properties struct.
	ENiagaraRendererSourceDataMode SourceMode;
	ENiagaraSpriteAlignment Alignment;
	ENiagaraSpriteFacingMode FacingMode;
	FVector2D PivotInUVSpace;
	ENiagaraSortMode SortMode;
	FVector2D SubImageSize;
	
	uint32 bSubImageBlend : 1;
	uint32 bRemoveHMDRollInVR : 1;
	uint32 bSortOnlyWhenTranslucent : 1;
	uint32 bGpuLowLatencyTranslucency : 1;
	uint32 bEnableCulling : 1;
	uint32 bEnableDistanceCulling : 1;
	uint32 bSetAnyBoundVars : 1;
	uint32 bVisTagInParamStore : 1;

	float MinFacingCameraBlendDistance;
	float MaxFacingCameraBlendDistance;
	FVector2D DistanceCullRange;
	FNiagaraCutoutVertexBuffer CutoutVertexBuffer;
	int32 NumCutoutVertexPerSubImage = 0;
	uint32 MaterialParamValidMask = 0;

	int32 RendererVisTagOffset;
	int32 RendererVisibility;

	int32 VFBoundOffsetsInParamStore[ENiagaraSpriteVFLayout::Type::Num];

	const FNiagaraRendererLayout* RendererLayoutWithCustomSort;
	const FNiagaraRendererLayout* RendererLayoutWithoutCustomSort;
};

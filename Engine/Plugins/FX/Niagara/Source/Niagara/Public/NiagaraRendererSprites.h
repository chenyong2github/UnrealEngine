// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*==============================================================================
NiagaraRendererSprites.h: Renderer for rendering Niagara particles as sprites.
==============================================================================*/

#pragma once

#include "NiagaraRenderer.h"

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
	virtual void ReleaseRenderThreadResources(NiagaraEmitterInstanceBatcher* Batcher)override;

	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector, const FNiagaraSceneProxy *SceneProxy) const override;
	virtual FNiagaraDynamicDataBase* GenerateDynamicData(const FNiagaraSceneProxy* Proxy, const UNiagaraRendererProperties* InProperties, const FNiagaraEmitterInstance* Emitter) const override;
	virtual int GetDynamicDataSize()const override;
	virtual void TransformChanged()override;
	virtual bool IsMaterialValid(UMaterialInterface* Mat)const override;

#if RHI_RAYTRACING
		virtual void GetDynamicRayTracingInstances(FRayTracingMaterialGatheringContext& Context, TArray<FRayTracingInstance>& OutRayTracingInstances, const FNiagaraSceneProxy* Proxy) final override;
#endif
	//FNiagaraRenderer interface END

private:
	struct FCPUSimParticleDataAllocation
	{
		FGlobalDynamicReadBuffer& DynamicReadBuffer;
		FGlobalDynamicReadBuffer::FAllocation ParticleData;
	};

	void ConditionalInitPrimitiveUniformBuffer(const FNiagaraSceneProxy *SceneProxy) const;
	FCPUSimParticleDataAllocation ConditionalAllocateCPUSimParticleData(FNiagaraDynamicDataSprites *DynamicDataSprites, FGlobalDynamicReadBuffer& DynamicReadBuffer) const;
	TUniformBufferRef<class FNiagaraSpriteUniformParameters> CreatePerViewUniformBuffer(const FSceneView* View, const FSceneViewFamily& ViewFamily, const FNiagaraSceneProxy *SceneProxy) const;
	void SetVertexFactoryParticleData(
		class FNiagaraSpriteVertexFactory& VertexFactory,
		FNiagaraDynamicDataSprites *DynamicDataSprites,
		FCPUSimParticleDataAllocation& CPUSimParticleDataAllocation,
		const FSceneView* View,
		const FNiagaraSceneProxy *SceneProxy) const;
	void CreateMeshBatchForView(
		const FSceneView* View,
		const FSceneViewFamily& ViewFamily,
		const FNiagaraSceneProxy *SceneProxy,
		FNiagaraDynamicDataSprites *DynamicDataSprites,
		uint32 IndirectArgsOffset,
		FMeshBatch& OutMeshBatch,
		class FNiagaraMeshCollectorResourcesSprite& OutCollectorResources) const;

	mutable TUniformBuffer<FPrimitiveUniformShaderParameters> WorldSpacePrimitiveUniformBuffer;
	class FNiagaraSpriteVertexFactory* VertexFactory;

	//Cached data from the properties struct.
	ENiagaraSpriteAlignment Alignment;
	ENiagaraSpriteFacingMode FacingMode;
	FVector CustomFacingVectorMask;
	FVector2D PivotInUVSpace;
	ENiagaraSortMode SortMode;
	FVector2D SubImageSize;
	uint32 bSubImageBlend : 1;
	uint32 bRemoveHMDRollInVR : 1;
	uint32 bSortOnlyWhenTranslucent : 1;
	float MinFacingCameraBlendDistance;
	float MaxFacingCameraBlendDistance;
	FNiagaraCutoutVertexBuffer CutoutVertexBuffer;
	int32 NumCutoutVertexPerSubImage = 0;


	//Offsets into the emitter's dataset for each bound attribute.
	int32 PositionOffset;
	int32 ColorOffset;
	int32 VelocityOffset;
	int32 RotationOffset;
	int32 SizeOffset;
	int32 FacingOffset;
	int32 AlignmentOffset;
	int32 SubImageOffset;
	uint32 MaterialParamValidMask;
	int32 MaterialParamOffset;
	int32 MaterialParamOffset1;
	int32 MaterialParamOffset2;
	int32 MaterialParamOffset3;
	int32 CameraOffsetOffset;
	int32 UVScaleOffset;
	int32 MaterialRandomOffset;
	int32 CustomSortingOffset;
	int32 NormalizedAgeOffset;
};

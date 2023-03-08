// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraRenderer.h"
#include "NiagaraVolumeRendererProperties.h"

#include "LocalVertexFactory.h"

class USceneComponent;

class NIAGARA_API FNiagaraRendererVolumes : public FNiagaraRenderer
{
public:
	explicit FNiagaraRendererVolumes(ERHIFeatureLevel::Type FeatureLevel, const UNiagaraRendererProperties *InProps, const FNiagaraEmitterInstance* Emitter);
	~FNiagaraRendererVolumes();

	//FNiagaraRenderer interface
	virtual void CreateRenderThreadResources() override;
	virtual void ReleaseRenderThreadResources() override;

	virtual bool IsMaterialValid(const UMaterialInterface* Material) const override;

	virtual FNiagaraDynamicDataBase *GenerateDynamicData(const FNiagaraSceneProxy* Proxy, const UNiagaraRendererProperties* InProperties, const FNiagaraEmitterInstance* Emitter) const override;
	virtual int GetDynamicDataSize() const override;

	void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector, const FNiagaraSceneProxy* SceneProxy) const;
#if RHI_RAYTRACING
	void GetDynamicRayTracingInstances(FRayTracingMaterialGatheringContext& Context, TArray<FRayTracingInstance>& OutRayTracingInstances, const FNiagaraSceneProxy* SceneProxy);
#endif //RHI_RAYTRACING
	//FNiagaraRenderer interface END


protected:
	ENiagaraRendererSourceDataMode	SourceMode = ENiagaraRendererSourceDataMode::Emitter;
	int32							RendererVisibilityTag = 0;

	FNiagaraDataSetAccessor<FNiagaraPosition>	PositionDataSetAccessor;
	FNiagaraDataSetAccessor<FQuat4f>			RotationDataSetAccessor;
	FNiagaraDataSetAccessor<FVector3f>			ScaleDataSetAccessor;
	FNiagaraDataSetAccessor<int32>				RendererVisibilityTagAccessor;
	FNiagaraDataSetAccessor<int32>				VolumeResolutionMaxAxisAccessor;
	FNiagaraDataSetAccessor<FVector3f>			VolumeWorldSpaceSizeAccessor;

	bool							bAnyVFBoundOffsets = false;
	int32							VFBoundOffsetsInParamStore[int32(ENiagaraVolumeVFLayout::Num)];

	FLocalVertexFactory				VertexFactory;
};

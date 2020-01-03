// Copyright Epic Games, Inc. All Rights Reserved.

/*==============================================================================
NiagaraRenderer.h: Base class for Niagara render modules
==============================================================================*/
#pragma once

#include "NiagaraRibbonVertexFactory.h"
#include "NiagaraRenderer.h"

class FNiagaraDataSet;

/**
* NiagaraRendererRibbons renders an FNiagaraEmitterInstance as a ribbon connecting all particles
* in order by particle age.
*/
class NIAGARA_API FNiagaraRendererRibbons : public FNiagaraRenderer
{
public:
	FNiagaraRendererRibbons(ERHIFeatureLevel::Type FeatureLevel, const UNiagaraRendererProperties *InProps, const FNiagaraEmitterInstance* Emitter);	// FNiagaraRenderer Interface 
	~FNiagaraRendererRibbons();

	// FNiagaraRenderer Interface 
	virtual void CreateRenderThreadResources(NiagaraEmitterInstanceBatcher* Batcher) override;
	virtual void ReleaseRenderThreadResources() override;

	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector, const FNiagaraSceneProxy *SceneProxy) const override;
	virtual FNiagaraDynamicDataBase *GenerateDynamicData(const FNiagaraSceneProxy* Proxy, const UNiagaraRendererProperties* InProperties, const FNiagaraEmitterInstance* Emitteride) const override;
	virtual int32 GetDynamicDataSize()const override;
	virtual bool IsMaterialValid(UMaterialInterface* Mat)const override;
#if RHI_RAYTRACING
	virtual void GetDynamicRayTracingInstances(FRayTracingMaterialGatheringContext& Context, TArray<FRayTracingInstance>& OutRayTracingInstances, const FNiagaraSceneProxy* Proxy) final override;
#endif
	//FNiagaraInterface END

	FORCEINLINE void AddDynamicParam(TArray<FNiagaraRibbonVertexDynamicParameter>& ParamData, const FVector4& DynamicParam);
protected:
	static void GenerateIndexBuffer(uint16* OutIndices, uint16& outVertexCount, const TArray<int32>& SegmentData, int32 InterpCount, bool bInvertOrder);

private:
	struct FCPUSimParticleDataAllocation
	{
		FGlobalDynamicReadBuffer& DynamicReadBuffer;
		FGlobalDynamicReadBuffer::FAllocation ParticleData;
	};

	void SetupMeshBatchAndCollectorResourceForView(
		const FSceneView* View,
		const FSceneViewFamily& ViewFamily,
		const FNiagaraSceneProxy* SceneProxy,
		FMeshElementCollector& Collector,
		struct FNiagaraDynamicDataRibbon* DynamicData,
		uint32 NumPrimitives,
		const FGlobalDynamicIndexBuffer::FAllocation& IndexAllocation,
		FMeshBatch& OutMeshBatch,
		class FNiagaraMeshCollectorResourcesRibbon& OutCollectorResources) const;

	void CreatePerViewResources(
		const FSceneView* View, const FSceneViewFamily& ViewFamily, const FNiagaraSceneProxy* SceneProxy, FMeshElementCollector& Collector,
		uint16& outVertexCount, uint32& OutNumSegments, FNiagaraRibbonUniformBufferRef& OutUniformBuffer, FGlobalDynamicIndexBuffer::FAllocation& InOutIndexAllocation) const;

	FCPUSimParticleDataAllocation AllocateParticleDataIfCPUSim(struct FNiagaraDynamicDataRibbon* DynamicDataRibbon, FGlobalDynamicReadBuffer& DynamicReadBuffer) const;

	ENiagaraRibbonFacingMode FacingMode;
	float UV0TilingDistance;
	FVector2D UV0Scale;
	FVector2D UV0Offset;
	ENiagaraRibbonAgeOffsetMode UV0AgeOffsetMode;
	float UV1TilingDistance;
	FVector2D UV1Scale;
	FVector2D UV1Offset;
	ENiagaraRibbonAgeOffsetMode UV1AgeOffsetMode;
	ENiagaraRibbonDrawDirection DrawDirection;
	ENiagaraRibbonTessellationMode TessellationMode;
	float CustomCurveTension;
	int32 CustomTessellationFactor;
	bool bCustomUseConstantFactor;
	float CustomTessellationMinAngle;
	bool bCustomUseScreenSpace;

	uint32 MaterialParamValidMask;

	// Average curvature of the segments.
	mutable float TessellationAngle = 0;
	// Average curvature of the segments (computed from the segment angle in radian).
	mutable float TessellationCurvature = 0;
	// Average twist of the segments.
	mutable float TessellationTwistAngle = 0;
	// Average twist curvature of the segments.
	mutable float TessellationTwistCurvature = 0;
	// Average twist curvature of the segments.
	mutable float TessellationTotalSegmentLength = 0;
};

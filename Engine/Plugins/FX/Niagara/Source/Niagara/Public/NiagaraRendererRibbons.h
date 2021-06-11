// Copyright Epic Games, Inc. All Rights Reserved.

/*==============================================================================
NiagaraRenderer.h: Base class for Niagara render modules
==============================================================================*/
#pragma once

#include "NiagaraRibbonVertexFactory.h"
#include "NiagaraRenderer.h"
#include "NiagaraRibbonRendererProperties.h"

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
	virtual bool IsMaterialValid(const UMaterialInterface* Mat)const override;
#if RHI_RAYTRACING
	virtual void GetDynamicRayTracingInstances(FRayTracingMaterialGatheringContext& Context, TArray<FRayTracingInstance>& OutRayTracingInstances, const FNiagaraSceneProxy* Proxy) final override;
#endif
	//FNiagaraInterface END

	FORCEINLINE void AddDynamicParam(TArray<FNiagaraRibbonVertexDynamicParameter>& ParamData, const FVector4& DynamicParam);
protected:
	struct FRibbonRenderingIndexOffsets
	{
		uint32 TotalBitCount;
		bool bWantsEnd;

		uint32 SegmentBitShift;
		uint32 SegmentBitMask;

		uint32 InterpBitShift;
		uint32 InterpBitMask;

		uint32 SliceVertexBitMask;
	};

	static int32 CalculateBitsForRange(int32 Range);
	static FRibbonRenderingIndexOffsets CalculateIndexBufferPacking(int32 NumSegments, int32 NumInterpolations, int32 NumSliceVertices);

	template <typename TValue>
	TValue* AppendToIndexBuffer(
		TValue* OutIndices,
		uint32& OutMaxUsedIndex,
		const TArrayView<int32>& SegmentData,
		const FRibbonRenderingIndexOffsets& Offsets,
		int32 InterpCount,
		bool bInvertOrder) const;


	/** Generate the raw index buffer preserving multi ribbon ordering. */
	template <typename TValue>
	void GenerateIndexBuffer(
		FGlobalDynamicIndexBuffer::FAllocationEx& InOutIndexAllocation, 
		const FRibbonRenderingIndexOffsets& Offsets,
		int32 InterpCount,
		const FVector& ViewDirection, 
		const FVector& ViewOriginForDistanceCulling, 
		struct FNiagaraDynamicDataRibbon* DynamicData) const;

private:
	struct FCPUSimParticleDataAllocation
	{
		FGlobalDynamicReadBuffer& DynamicReadBuffer;
		FParticleRenderData ParticleData;
	};

	void SetupMeshBatchAndCollectorResourceForView(
		const FSceneView* View,
		const FSceneViewFamily& ViewFamily,
		const FNiagaraSceneProxy* SceneProxy,
		FMeshElementCollector& Collector,
		struct FNiagaraDynamicDataRibbon* DynamicData,
		const FGlobalDynamicIndexBuffer::FAllocationEx& IndexAllocation,
		FMeshBatch& OutMeshBatch,
		class FNiagaraMeshCollectorResourcesRibbon& OutCollectorResources) const;

	void CreatePerViewResources(
		const FSceneView* View,
		const FSceneViewFamily& ViewFamily,
		const FNiagaraSceneProxy* SceneProxy,
		FMeshElementCollector& Collector,
		FNiagaraRibbonUniformBufferRef& OutUniformBuffer,
		FGlobalDynamicIndexBuffer::FAllocationEx& InOutIndexAllocation) const;

	FCPUSimParticleDataAllocation AllocateParticleDataIfCPUSim(struct FNiagaraDynamicDataRibbon* DynamicDataRibbon, FGlobalDynamicReadBuffer& DynamicReadBuffer) const;

	ENiagaraRibbonFacingMode FacingMode;
	FNiagaraRibbonUVSettings UV0Settings;
	FNiagaraRibbonUVSettings UV1Settings;
	ENiagaraRibbonDrawDirection DrawDirection;

	ENiagaraRibbonShapeMode Shape;
	bool bEnableAccurateGeometry;
	int32 WidthSegmentationCount;
	int32 MultiPlaneCount;
	int32 TubeSubdivisions;
	TArray<FNiagaraRibbonShapeCustomVertex> CustomVertices;

	ENiagaraRibbonTessellationMode TessellationMode;
	float CustomCurveTension;
	int32 CustomTessellationFactor;
	bool bCustomUseConstantFactor;
	float CustomTessellationMinAngle;
	bool bCustomUseScreenSpace;


	uint32 MaterialParamValidMask;
	const FNiagaraRendererLayout* RendererLayout;

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

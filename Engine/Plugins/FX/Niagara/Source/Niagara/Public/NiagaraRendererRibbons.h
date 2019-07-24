// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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
	virtual void ReleaseRenderThreadResources(NiagaraEmitterInstanceBatcher* Batcher) override;

	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector, const FNiagaraSceneProxy *SceneProxy) const override;
	virtual FNiagaraDynamicDataBase *GenerateDynamicData(const FNiagaraSceneProxy* Proxy, const UNiagaraRendererProperties* InProperties, const FNiagaraEmitterInstance* Emitteride) const override;
	virtual int32 GetDynamicDataSize()const override;
	virtual void TransformChanged() override;
	virtual bool IsMaterialValid(UMaterialInterface* Mat)const override;
	//FNiagaraInterface END

	FORCEINLINE void AddDynamicParam(TArray<FNiagaraRibbonVertexDynamicParameter>& ParamData, const FVector4& DynamicParam);
protected:
	static void GenerateIndexBuffer(uint16* OutIndices, const TArray<int32>& SegmentData, int32 MaxTessellation, bool bInvertOrder, bool bCullTwistedStrips);

private:
	class FNiagaraRibbonVertexFactory *VertexFactory;
	mutable TUniformBuffer<FPrimitiveUniformShaderParameters> WorldSpacePrimitiveUniformBuffer;

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

	int32 PositionDataOffset;
	int32 VelocityDataOffset;
	int32 WidthDataOffset;
	int32 TwistDataOffset;
	int32 FacingDataOffset;
	int32 ColorDataOffset;
	int32 NormalizedAgeDataOffset;
	int32 MaterialRandomDataOffset;
	uint32 MaterialParamValidMask;
	int32 MaterialParamOffset;
	int32 MaterialParamOffset1;
	int32 MaterialParamOffset2;
	int32 MaterialParamOffset3;

	// Average curvature of the segments.
	mutable float TessellationAngle = 0;
	// Average angle of the curvature of the segments (in radian).
	mutable float TessellationCurvature = 0;
	// Average twist of the segments.
	mutable float TessellationTwistAngle = 0;
	// Average twist curvature of the segments.
	mutable float TessellationTwistCurvature = 0;
};

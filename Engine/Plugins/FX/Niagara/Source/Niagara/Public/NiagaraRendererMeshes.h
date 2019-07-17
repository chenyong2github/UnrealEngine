// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*==============================================================================
NiagaraRenderer.h: Base class for Niagara render modules
==============================================================================*/
#pragma once

#include "NiagaraMeshVertexFactory.h"
#include "NiagaraRenderer.h"

class FNiagaraDataSet;

/**
* NiagaraRendererSprites renders an FNiagaraEmitterInstance as sprite particles
*/
class NIAGARA_API FNiagaraRendererMeshes : public FNiagaraRenderer
{
public:
	FNiagaraRendererMeshes(ERHIFeatureLevel::Type FeatureLevel, const UNiagaraRendererProperties *InProps, const FNiagaraEmitterInstance* Emitter);
	~FNiagaraRendererMeshes();
	
	//FNiagaraRenderer Interface
	virtual void CreateRenderThreadResources(NiagaraEmitterInstanceBatcher* Batcher) override;
	virtual void ReleaseRenderThreadResources(NiagaraEmitterInstanceBatcher* Batcher) override;

	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector, const FNiagaraSceneProxy *SceneProxy) const override;
	virtual FNiagaraDynamicDataBase* GenerateDynamicData(const FNiagaraSceneProxy* Proxy, const UNiagaraRendererProperties* InProperties, const FNiagaraEmitterInstance* Emitter) const override;
	virtual int32 GetDynamicDataSize()const override;
	virtual void TransformChanged() override;
	virtual bool IsMaterialValid(UMaterialInterface* Mat)const override;
	//FNiagaraRenderer Interface END

	void SetupVertexFactory(FNiagaraMeshVertexFactory *InVertexFactory, const FStaticMeshLODResources& LODResources) const;

private:
	mutable TUniformBuffer<FPrimitiveUniformShaderParameters> WorldSpacePrimitiveUniformBuffer;
	class FNiagaraMeshVertexFactory* VertexFactory;

	/** Render data of the static mesh we use. */
	FStaticMeshRenderData* MeshRenderData;

	ENiagaraSortMode SortMode;
	ENiagaraMeshFacingMode FacingMode;
	uint32 bOverrideMaterials : 1;
	uint32 bSortOnlyWhenTranslucent : 1;

	int32 PositionOffset;
	int32 VelocityOffset;
	int32 ColorOffset;
	int32 ScaleOffset;
	int32 SizeOffset;
	uint32 MaterialParamValidMask;
	int32 MaterialParamOffset;
	int32 MaterialParamOffset1;
	int32 MaterialParamOffset2;
	int32 MaterialParamOffset3;
	int32 TransformOffset;
	int32 NormalizedAgeOffset;
	int32 MaterialRandomOffset;
	int32 CustomSortingOffset;

	int32 MeshMinimumLOD = 0;
};

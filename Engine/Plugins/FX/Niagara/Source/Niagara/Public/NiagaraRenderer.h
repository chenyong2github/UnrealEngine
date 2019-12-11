// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*==============================================================================
NiagaraRenderer.h: Base class for Niagara render modules
==============================================================================*/
#pragma once

#include "CoreMinimal.h"
#include "NiagaraCommon.h"
#include "Materials/MaterialInterface.h"
#include "UniformBuffer.h"
#include "Materials/Material.h"
#include "PrimitiveViewRelevance.h"
#include "ParticleHelper.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraRibbonRendererProperties.h"
#include "NiagaraMeshRendererProperties.h"
#include "NiagaraLightRendererProperties.h"
#include "RenderingThread.h"
#include "SceneView.h"
#include "NiagaraComponent.h"
#include "NiagaraCutoutVertexBuffer.h"
#include "NiagaraBoundsCalculator.h"

class FNiagaraDataSet;
class FNiagaraSceneProxy;
class FNiagaraGPURendererCount;

/** Struct used to pass dynamic data from game thread to render thread */
struct FNiagaraDynamicDataBase
{
	explicit FNiagaraDynamicDataBase(const FNiagaraEmitterInstance* InEmitter);
	virtual ~FNiagaraDynamicDataBase();

	FNiagaraDynamicDataBase() = delete;
	FNiagaraDynamicDataBase(FNiagaraDynamicDataBase& Other) = delete;
	FNiagaraDynamicDataBase& operator=(const FNiagaraDynamicDataBase& Other) = delete;

	FNiagaraDataBuffer* GetParticleDataToRender()const;
	FORCEINLINE ENiagaraSimTarget GetSimTarget()const { return SimTarget; }
	FORCEINLINE FMaterialRelevance GetMaterialRelevance()const { return MaterialRelevance; }

	FORCEINLINE void SetMaterialRelevance(FMaterialRelevance NewRelevance) { MaterialRelevance = NewRelevance; }
protected:

	FMaterialRelevance MaterialRelevance;
	ENiagaraSimTarget SimTarget;

	union
	{
		FNiagaraDataBuffer* CPUParticleData;
		FNiagaraComputeExecutionContext* GPUExecContext;
	}Data;
};

//////////////////////////////////////////////////////////////////////////

extern int32 GbEnableMinimalGPUBuffers;

/** Mapping between a variable in the source dataset and the location we place it in the GPU buffer passed to the VF. */
struct FNiagaraRendererVariableInfo
{
	FNiagaraRendererVariableInfo()
		: DatasetOffset(INDEX_NONE), GPUBufferOffset(INDEX_NONE), NumComponents(0), bUpload(true)
	{
	}
	
	FNiagaraRendererVariableInfo(int32 InDatasetOffset, int32 InGPUBufferOffset, int32 InNumComponents, bool InbUpload)
		: DatasetOffset(InDatasetOffset), GPUBufferOffset(InGPUBufferOffset), NumComponents(InNumComponents), bUpload(InbUpload)
	{
	}

	FORCEINLINE int32 GetGPUOffset()const { return GbEnableMinimalGPUBuffers ? GPUBufferOffset : DatasetOffset; }

	int32 DatasetOffset;
	int32 GPUBufferOffset;
	int32 NumComponents;
	bool bUpload;
};

/**
* Base class for Niagara System renderers.
*/
class FNiagaraRenderer
{
public:

	FNiagaraRenderer(ERHIFeatureLevel::Type FeatureLevel, const UNiagaraRendererProperties *InProps, const FNiagaraEmitterInstance* Emitter);
	virtual ~FNiagaraRenderer();

	FNiagaraRenderer(const FNiagaraRenderer& Other) = delete;
	FNiagaraRenderer& operator=(const FNiagaraRenderer& Other) = delete;

	virtual void Initialize(const UNiagaraRendererProperties *InProps, const FNiagaraEmitterInstance* Emitter);
	virtual void CreateRenderThreadResources(NiagaraEmitterInstanceBatcher* Batcher);
	virtual void ReleaseRenderThreadResources();

	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View, const FNiagaraSceneProxy *SceneProxy)const;
	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector, const FNiagaraSceneProxy *SceneProxy) const {}
	virtual FNiagaraDynamicDataBase* GenerateDynamicData(const FNiagaraSceneProxy* Proxy, const UNiagaraRendererProperties* InProperties, const FNiagaraEmitterInstance* Emitteride) const { return nullptr; }

	virtual void GatherSimpleLights(FSimpleLightArray& OutParticleLights)const {}
	virtual int32 GetDynamicDataSize()const { return 0; }
	virtual bool IsMaterialValid(UMaterialInterface* Mat)const { return Mat != nullptr; }

	void SortIndices(const struct FNiagaraGPUSortInfo& SortInfo, int32 SortVarIdx, const FNiagaraDataBuffer& Buffer, FGlobalDynamicReadBuffer::FAllocation& OutIndices)const;

	void SetDynamicData_RenderThread(FNiagaraDynamicDataBase* NewDynamicData);
	FORCEINLINE FNiagaraDynamicDataBase *GetDynamicData()const { return DynamicDataRender; }
	FORCEINLINE bool HasDynamicData()const { return DynamicDataRender != nullptr; }
	FORCEINLINE bool HasLights()const { return bHasLights; }

#if RHI_RAYTRACING
	virtual void GetDynamicRayTracingInstances(FRayTracingMaterialGatheringContext& Context, TArray<FRayTracingInstance>& OutRayTracingInstances, const FNiagaraSceneProxy* Proxy) {}
#endif

	NIAGARA_API static FRWBuffer& GetDummyFloatBuffer();
	NIAGARA_API static FRWBuffer& GetDummyFloat4Buffer();
	NIAGARA_API static FRWBuffer& GetDummyIntBuffer();
	NIAGARA_API static FRWBuffer& GetDummyUIntBuffer();
	
	FORCEINLINE ENiagaraSimTarget GetSimTarget() const { return SimTarget; }

protected:

	struct FNiagaraDynamicDataBase *DynamicDataRender;
	
#if RHI_RAYTRACING
	FRWBuffer RayTracingDynamicVertexBuffer;
	FRayTracingGeometry RayTracingGeometry;
#endif

	uint32 bLocalSpace : 1;
	uint32 bHasLights : 1;
	const ENiagaraSimTarget SimTarget;
	uint32 NumIndicesPerInstance;

	ERHIFeatureLevel::Type FeatureLevel;

#if STATS
	TStatId EmitterStatID;
#endif

	bool SetVertexFactoryVariable(const FNiagaraDataSet& DataSet, const FNiagaraVariable& Var, int32 VFVarOffset);
	FGlobalDynamicReadBuffer::FAllocation TransferDataToGPU(FGlobalDynamicReadBuffer& DynamicReadBuffer, FNiagaraDataBuffer* SrcData)const;
	
	mutable TArray<FNiagaraRendererVariableInfo, TInlineAllocator<16>> VFVariables;
	int32 TotalVFComponents;

	/** Cached array of materials used from the properties data. Validated with usage flags etc. */
	TArray<UMaterialInterface*> BaseMaterials_GT;
	FMaterialRelevance BaseMaterialRelevance_GT;

	TRefCountPtr<FNiagaraGPURendererCount> NumRegisteredGPURenderers;
};


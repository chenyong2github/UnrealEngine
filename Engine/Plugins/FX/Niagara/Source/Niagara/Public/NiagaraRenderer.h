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



class SimpleTimer
{
public:
	SimpleTimer()
	{
		StartTime = FPlatformTime::Seconds() * 1000.0;
	}

	double GetElapsedMilliseconds()
	{
		return (FPlatformTime::Seconds()*1000.0) - StartTime;
	}

	~SimpleTimer()
	{
	}

private:
	double StartTime;
};

//////////////////////////////////////////////////////////////////////////

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

	virtual void Initialize(ERHIFeatureLevel::Type FeatureLevel, const UNiagaraRendererProperties *InProps, const FNiagaraEmitterInstance* Emitter);
	virtual void CreateRenderThreadResources(NiagaraEmitterInstanceBatcher* Batcher);
	virtual void ReleaseRenderThreadResources(NiagaraEmitterInstanceBatcher* Batcher);

	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View, const FNiagaraSceneProxy *SceneProxy)const;
	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector, const FNiagaraSceneProxy *SceneProxy) const {}
	virtual FNiagaraDynamicDataBase* GenerateDynamicData(const FNiagaraSceneProxy* Proxy, const UNiagaraRendererProperties* InProperties, const FNiagaraEmitterInstance* Emitteride) const { return nullptr; }

	virtual void GatherSimpleLights(FSimpleLightArray& OutParticleLights)const {}
	virtual int32 GetDynamicDataSize()const { return 0; }
	virtual void TransformChanged() {}
	virtual bool IsMaterialValid(UMaterialInterface* Mat)const { return Mat != nullptr; }

	void SortIndices(ENiagaraSortMode SortMode, int32 SortAttributeOffset, const FNiagaraDataBuffer& Buffer, const FMatrix& LocalToWorld, const FSceneView* View, FGlobalDynamicReadBuffer::FAllocation& OutIndices)const;

	void SetDynamicData_RenderThread(FNiagaraDynamicDataBase* NewDynamicData);
	FORCEINLINE FNiagaraDynamicDataBase *GetDynamicData()const { return DynamicDataRender; }
	FORCEINLINE bool HasDynamicData()const { return DynamicDataRender != nullptr; }
	FORCEINLINE float GetCPUTimeMS() const { return CPUTimeMS; }
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

	mutable float CPUTimeMS;

	uint32 bLocalSpace : 1;
	uint32 bHasLights : 1;
	ENiagaraSimTarget SimTarget;
	uint32 NumIndicesPerInstance;

#if STATS
	TStatId EmitterStatID;
#endif

	/** Cached array of materials used from the properties data. Validated with usage flags etc. */
	TArray<UMaterialInterface*> BaseMaterials_GT;
	FMaterialRelevance BaseMaterialRelevance_GT;

};


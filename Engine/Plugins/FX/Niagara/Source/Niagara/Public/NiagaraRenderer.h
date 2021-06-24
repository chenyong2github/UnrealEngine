// Copyright Epic Games, Inc. All Rights Reserved.

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
#include "NiagaraRendererProperties.h"
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

	FNiagaraDataBuffer* GetParticleDataToRender(bool bIsLowLatencyTranslucent = false) const;
	FORCEINLINE ENiagaraSimTarget GetSimTarget() const { return SimTarget; }
	FORCEINLINE FMaterialRelevance GetMaterialRelevance() const { return MaterialRelevance; }

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

struct FParticleRenderData
{
	FGlobalDynamicReadBuffer::FAllocation FloatData;
	FGlobalDynamicReadBuffer::FAllocation HalfData;
	FGlobalDynamicReadBuffer::FAllocation IntData;
	uint32 FloatStride = 0;
	uint32 HalfStride = 0;
	uint32 IntStride = 0;
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

	virtual void Initialize(const UNiagaraRendererProperties *InProps, const FNiagaraEmitterInstance* Emitter, const UNiagaraComponent* InComponent);
	virtual void CreateRenderThreadResources(NiagaraEmitterInstanceBatcher* Batcher) {}
	virtual void ReleaseRenderThreadResources() {}
	virtual void DestroyRenderState_Concurrent() {}

	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View, const FNiagaraSceneProxy *SceneProxy)const;
	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector, const FNiagaraSceneProxy *SceneProxy) const {}
	virtual FNiagaraDynamicDataBase* GenerateDynamicData(const FNiagaraSceneProxy* Proxy, const UNiagaraRendererProperties* InProperties, const FNiagaraEmitterInstance* Emitteride) const { return nullptr; }

	virtual void GatherSimpleLights(FSimpleLightArray& OutParticleLights)const {}
	virtual int32 GetDynamicDataSize()const { return 0; }
	virtual bool IsMaterialValid(const UMaterialInterface* Mat)const { return Mat != nullptr; }

	static void SortIndices(const struct FNiagaraGPUSortInfo& SortInfo, const FNiagaraRendererVariableInfo& SortVariable, const FNiagaraDataBuffer& Buffer, FGlobalDynamicReadBuffer::FAllocation& OutIndices);
	static int32 SortAndCullIndices(const FNiagaraGPUSortInfo& SortInfo, const FNiagaraDataBuffer& Buffer, FGlobalDynamicReadBuffer::FAllocation& OutIndices);

	void SetDynamicData_RenderThread(FNiagaraDynamicDataBase* NewDynamicData);
	FORCEINLINE FNiagaraDynamicDataBase *GetDynamicData() const { return DynamicDataRender; }
	FORCEINLINE bool HasDynamicData() const { return DynamicDataRender != nullptr; }
	FORCEINLINE bool HasLights() const { return bHasLights; }
	FORCEINLINE bool IsMotionBlurEnabled() const { return bMotionBlurEnabled; }

#if RHI_RAYTRACING
	virtual void GetDynamicRayTracingInstances(FRayTracingMaterialGatheringContext& Context, TArray<FRayTracingInstance>& OutRayTracingInstances, const FNiagaraSceneProxy* Proxy) {}
#endif

	FORCEINLINE static FRHIShaderResourceView* GetSrvOrDefaultFloat(FRWBuffer& RWBuffer) { return RWBuffer.SRV.IsValid() ? (FRHIShaderResourceView*)RWBuffer.SRV : GetDummyFloatBuffer(); }
	FORCEINLINE static FRHIShaderResourceView* GetSrvOrDefaultHalf(FRWBuffer& RWBuffer) { return RWBuffer.SRV.IsValid() ? (FRHIShaderResourceView*)RWBuffer.SRV : GetDummyHalfBuffer(); }
	FORCEINLINE static FRHIShaderResourceView* GetSrvOrDefaultUInt(FRWBuffer& RWBuffer) { return RWBuffer.SRV.IsValid() ? (FRHIShaderResourceView*)RWBuffer.SRV : GetDummyUIntBuffer(); }
	FORCEINLINE static FRHIShaderResourceView* GetSrvOrDefaultInt(FRWBuffer& RWBuffer) { return RWBuffer.SRV.IsValid() ? (FRHIShaderResourceView*)RWBuffer.SRV : GetDummyIntBuffer(); }

	FORCEINLINE static FRHIShaderResourceView* GetSrvOrDefaultFloat(FGlobalDynamicReadBuffer::FAllocation& Allocation) { return Allocation.IsValid() ? (FRHIShaderResourceView*)Allocation.SRV : GetDummyFloatBuffer(); }
	FORCEINLINE static FRHIShaderResourceView* GetSrvOrDefaultHalf(FGlobalDynamicReadBuffer::FAllocation& Allocation) { return Allocation.IsValid() ? (FRHIShaderResourceView*)Allocation.SRV : GetDummyHalfBuffer(); }
	FORCEINLINE static FRHIShaderResourceView* GetSrvOrDefaultUInt(FGlobalDynamicReadBuffer::FAllocation& Allocation) { return Allocation.IsValid() ? (FRHIShaderResourceView*)Allocation.SRV : GetDummyUIntBuffer(); }
	FORCEINLINE static FRHIShaderResourceView* GetSrvOrDefaultInt(FGlobalDynamicReadBuffer::FAllocation& Allocation) { return Allocation.IsValid() ? (FRHIShaderResourceView*)Allocation.SRV : GetDummyIntBuffer(); }

	NIAGARA_API static FRHIShaderResourceView* GetDummyFloatBuffer();
	NIAGARA_API static FRHIShaderResourceView* GetDummyFloat2Buffer();
	NIAGARA_API static FRHIShaderResourceView* GetDummyFloat4Buffer();
	NIAGARA_API static FRHIShaderResourceView* GetDummyWhiteColorBuffer();
	NIAGARA_API static FRHIShaderResourceView* GetDummyIntBuffer();
	NIAGARA_API static FRHIShaderResourceView* GetDummyUIntBuffer();
	NIAGARA_API static FRHIShaderResourceView* GetDummyUInt4Buffer();
	NIAGARA_API static FRHIShaderResourceView* GetDummyTextureReadBuffer2D();
	NIAGARA_API static FRHIShaderResourceView* GetDummyTextureReadBuffer2DArray();
	NIAGARA_API static FRHIShaderResourceView* GetDummyTextureReadBuffer3D();
	NIAGARA_API static FRHIShaderResourceView* GetDummyHalfBuffer();

	FORCEINLINE ENiagaraSimTarget GetSimTarget() const { return SimTarget; }

	virtual void GetUsedMaterials(TArray<UMaterialInterface*>& UsedMaterials, bool bGetDebugMaterials) { UsedMaterials.Append(BaseMaterials_GT); }

	virtual void PostSystemTick_GameThread(const UNiagaraRendererProperties* InProperties, const FNiagaraEmitterInstance* Emitter) { }
	virtual void OnSystemComplete_GameThread(const UNiagaraRendererProperties* InProperties, const FNiagaraEmitterInstance* Emitter) { }

protected:

	virtual void ProcessMaterialParameterBindings(TConstArrayView< FNiagaraMaterialAttributeBinding > InMaterialParameterBindings, const FNiagaraEmitterInstance* InEmitter, TConstArrayView<UMaterialInterface*> InMaterials) const;


	struct FNiagaraDynamicDataBase *DynamicDataRender;

#if RHI_RAYTRACING
	FRWBuffer RayTracingDynamicVertexBuffer;
	FRayTracingGeometry RayTracingGeometry;
#endif

	uint32 bLocalSpace : 1;
	uint32 bHasLights : 1;
	uint32 bMotionBlurEnabled : 1;
	const ENiagaraSimTarget SimTarget;

	ERHIFeatureLevel::Type FeatureLevel;

#if STATS
	TStatId EmitterStatID;
#endif

	static FParticleRenderData TransferDataToGPU(FGlobalDynamicReadBuffer& DynamicReadBuffer, const FNiagaraRendererLayout* RendererLayout, TConstArrayView<uint32> IntComponents, const FNiagaraDataBuffer* SrcData);

	/** Cached array of materials used from the properties data. Validated with usage flags etc. */
	TArray<UMaterialInterface*> BaseMaterials_GT;
	FMaterialRelevance BaseMaterialRelevance_GT;
};

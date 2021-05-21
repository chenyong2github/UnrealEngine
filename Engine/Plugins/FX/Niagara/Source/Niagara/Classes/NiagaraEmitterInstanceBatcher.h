// Copyright Epic Games, Inc. All Rights Reserved.

/*==============================================================================
NiagaraEmitterInstanceBatcher.h: Queueing and batching for Niagara simulation;
use to reduce per-simulation overhead by batching together simulations using
the same VectorVM byte code / compute shader code
==============================================================================*/
#pragma once

#include "CoreMinimal.h"
#include "NiagaraCommon.h"
#include "RendererInterface.h"
#include "NiagaraParameters.h"
#include "NiagaraEmitter.h"
#include "Tickable.h"
#include "Modules/ModuleManager.h"
#include "RHIResources.h"
#include "FXSystem.h"
#include "NiagaraRendererProperties.h"
#include "ParticleResources.h"
#include "Runtime/Engine/Private/Particles/ParticleSortingGPU.h"
#include "NiagaraGPUSortInfo.h"
#include "NiagaraComputeExecutionContext.h"
#include "NiagaraGPUSystemTick.h"
#include "NiagaraSystemGpuComputeProxy.h"
#include "NiagaraGPUInstanceCountManager.h"
#include "NiagaraGPUProfiler.h"

class FGPUSortManager;
class FNiagaraGpuComputeDebug;
class FNiagaraGpuReadbackManager;

struct FNiagaraUAVPoolAccessScope
{
	FNiagaraUAVPoolAccessScope(class NiagaraEmitterInstanceBatcher& InBatcher);
	~FNiagaraUAVPoolAccessScope();
private:
	class NiagaraEmitterInstanceBatcher& Batcher;
};

enum class ENiagaraEmptyUAVType
{
	Buffer,
	Texture2D,
	Texture2DArray,
	Texture3D,
	Num
};

class NiagaraEmitterInstanceBatcher : public FFXSystemInterface
{
	friend FNiagaraUAVPoolAccessScope;

public:
	NiagaraEmitterInstanceBatcher(ERHIFeatureLevel::Type InFeatureLevel, EShaderPlatform InShaderPlatform, FGPUSortManager* InGPUSortManager);

	~NiagaraEmitterInstanceBatcher();

	static NIAGARA_API const FName Name;
	virtual FFXSystemInterface* GetInterface(const FName& InName) override;

	/** Add system instance proxy to the batcher for tracking. */
	void AddGpuComputeProxy(FNiagaraSystemGpuComputeProxy* ComputeProxy);

	/** Remove system instance proxy from the batcher. */
	void RemoveGpuComputeProxy(FNiagaraSystemGpuComputeProxy* ComputeProxy);

#if WITH_EDITOR
	virtual void Suspend() override {}
	virtual void Resume() override {}
#endif // #if WITH_EDITOR

	virtual void DrawDebug(FCanvas* Canvas) override {}
	virtual bool ShouldDebugDraw_RenderThread() const override;
	virtual void DrawDebug_RenderThread(class FRDGBuilder& GraphBuilder, const class FViewInfo& View, const struct FScreenPassRenderTarget& Output) override;
	virtual void DrawSceneDebug_RenderThread(class FRDGBuilder& GraphBuilder, const class FViewInfo& View, FRDGTextureRef SceneColor, FRDGTextureRef SceneDepth) override;
	virtual void AddVectorField(UVectorFieldComponent* VectorFieldComponent) override {}
	virtual void RemoveVectorField(UVectorFieldComponent* VectorFieldComponent) override {}
	virtual void UpdateVectorField(UVectorFieldComponent* VectorFieldComponent) override {}
	virtual void PreInitViews(FRHICommandListImmediate& RHICmdList, bool bAllowGPUParticleUpdate) override;
	virtual void PostInitViews(FRHICommandListImmediate& RHICmdList, FRHIUniformBuffer* ViewUniformBuffer, bool bAllowGPUParticleUpdate) override;
	virtual bool UsesGlobalDistanceField() const override;
	virtual bool UsesDepthBuffer() const override;
	virtual bool RequiresEarlyViewUniformBuffer() const override;
	virtual void PreRender(FRHICommandListImmediate& RHICmdList, const class FGlobalDistanceFieldParameterData* GlobalDistanceFieldParameterData, bool bAllowGPUParticleUpdate) override;
	virtual void OnDestroy() override; // Called on the gamethread to delete the batcher on the renderthread.

	virtual void Tick(float DeltaTime) override;

	virtual void PostRenderOpaque(
		FRHICommandListImmediate& RHICmdList,
		FRHIUniformBuffer* ViewUniformBuffer,
		const class FShaderParametersMetadata* SceneTexturesUniformBufferStruct,
		FRHIUniformBuffer* SceneTexturesUniformBuffer,
		bool bAllowGPUParticleUpdate) override;

	/**
	 * Process and respond to a build up of excessive ticks inside the batcher.
	 * In the case of the application not having focus the game thread may continue
	 * to process and send ticks to the render thread but the rendering thread may
	 * never process them.  The World Manager will ensure this is called once per
	 * game frame so we have an opportunity to flush the ticks avoiding a stall
	 * when we gain focus again.
	 */
	NIAGARA_API void ProcessPendingTicksFlush(FRHICommandListImmediate& RHICmdList, bool bForceFlush);

	/** Processes all pending readbacks */
	void ProcessDebugReadbacks(FRHICommandListImmediate& RHICmdList, bool bWaitCompletion);

	/**
	 * Register work for GPU sorting (using the GPUSortManager).
	 * The constraints of the sort request are defined in SortInfo.SortFlags.
	 * The sort task bindings are set in SortInfo.AllocationInfo.
	 * The initial keys and values are generated in the GenerateSortKeys() callback.
	 *
	 * Return true if the work was registered, or false it GPU sorting is not available or impossible.
	 */
	bool AddSortedGPUSimulation(FNiagaraGPUSortInfo& SortInfo);

	const FGlobalDistanceFieldParameterData& GetGlobalDistanceFieldParameters() const { return GlobalDistanceFieldParams; }

	void SetDataInterfaceParameters(FRHICommandList& RHICmdList, const FNiagaraGPUSystemTick& Tick, const FNiagaraComputeInstanceData& InstanceData, const FNiagaraShaderRef& ComputeShader, const FNiagaraSimStageData& SimStageData) const;
	void UnsetDataInterfaceParameters(FRHICommandList& RHICmdList, const FNiagaraGPUSystemTick& Tick, const FNiagaraComputeInstanceData& InstanceData, const FNiagaraShaderRef& ComputeShader, const FNiagaraSimStageData& SimStageData) const;

	FORCEINLINE FNiagaraGPUInstanceCountManager& GetGPUInstanceCounterManager() { check(IsInRenderingThread()); return GPUInstanceCounterManager; }

	FORCEINLINE EShaderPlatform GetShaderPlatform() const { return ShaderPlatform; }
	FORCEINLINE ERHIFeatureLevel::Type GetFeatureLevel() const { return FeatureLevel; }

	/** Reset the data interfaces and check if the spawn stages are valid */
	void ResetDataInterfaces(FRHICommandList& RHICmdList, const FNiagaraGPUSystemTick& Tick, const FNiagaraComputeInstanceData& InstanceData) const;

	/** Given a shader stage index, find the corresponding data interface */
	FNiagaraDataInterfaceProxyRW* FindIterationInterface(FNiagaraComputeInstanceData* Instance, const uint32 SimulationStageIndex) const;

	/** Loop over all the data interfaces and call the pre-stage methods */
	void PreStageInterface(FRHICommandList& RHICmdList, const FNiagaraGPUSystemTick& Tick, const FNiagaraComputeInstanceData& InstanceData, const FNiagaraSimStageData& SimStageData) const;

	/** Loop over all the data interfaces and call the post-stage methods */
	void PostStageInterface(FRHICommandList& RHICmdList, const FNiagaraGPUSystemTick& Tick, const FNiagaraComputeInstanceData& InstanceData, const FNiagaraSimStageData& SimStageData) const;

	/** Loop over all data interfaces and call the post-simulate methods */
	void PostSimulateInterface(FRHICommandList& RHICmdList, const FNiagaraGPUSystemTick& Tick, const FNiagaraComputeInstanceData& InstanceData) const;

	/** Grab a temporary dummy RW buffer from the pool.  Note: When doing this outside of Niagara you must be within a FNiagaraUAVPoolAccessScope. */
	NIAGARA_API FRHIUnorderedAccessView* GetEmptyUAVFromPool(FRHICommandList& RHICmdList, EPixelFormat Format, ENiagaraEmptyUAVType Type) const;

	/** Get the shared SortManager, used in the rendering loop to call FGPUSortManager::OnPreRender() and FGPUSortManager::OnPostRenderOpaque() */
	virtual FGPUSortManager* GetGPUSortManager() const override;

#if !UE_BUILD_SHIPPING
	/** Debug only function to readback data. */
	void AddDebugReadback(FNiagaraSystemInstanceID InstanceID, TSharedPtr<struct FNiagaraScriptDebuggerInfo, ESPMode::ThreadSafe> DebugInfo, FNiagaraComputeExecutionContext* Context);
#endif

#if NIAGARA_COMPUTEDEBUG_ENABLED
	/** Get the Gpu Compute Debug class, useful for visualizing textures, etc. */
	FNiagaraGpuComputeDebug* GetGpuComputeDebug() const { return GpuComputeDebugPtr.Get(); }
#endif
	FNiagaraGpuReadbackManager* GetGpuReadbackManager() const { return GpuReadbackManagerPtr.Get(); }

private:
	void DumpDebugFrame(FRHICommandListImmediate& RHICmdList);
	void UpdateInstanceCountManager(FRHICommandListImmediate& RHICmdList);
	void PrepareTicksForProxy(FRHICommandListImmediate& RHICmdList, FNiagaraSystemGpuComputeProxy* ComputeProxy, FNiagaraGpuDispatchList& GpuDispatchList);
	void PrepareAllTicks(FRHICommandListImmediate& RHICmdList);
	void ExecuteTicks(FRHICommandList& RHICmdList, FRHIUniformBuffer* ViewUniformBuffer, ENiagaraGpuComputeTickStage::Type TickStage);
	void DispatchStage(FRHICommandList& RHICmdList, FRHIUniformBuffer* ViewUniformBuffer, const FNiagaraGPUSystemTick& Tick, const FNiagaraComputeInstanceData& InstanceData, const FNiagaraSimStageData& SimStageData);

	/**
	 * Generate all the initial keys and values for a GPUSortManager sort batch.
	 * Sort batches are created when GPU sort tasks are registered in AddSortedGPUSimulation().
	 * Each sort task defines constraints about when the initial sort data can generated and
	 * and when the sorted results are needed (see EGPUSortFlags for details).
	 * Currently, for Niagara, all the sort tasks have the EGPUSortFlags::KeyGenAfterPreRender flag
	 * and so the callback registered in GPUSortManager->Register() only has the EGPUSortFlags::KeyGenAfterPreRender usage.
	 * This garanties that GenerateSortKeys() only gets called after PreRender(), which is a constraint required because
	 * Niagara renders the current state of the GPU emitters, before the are ticked
	 * (Niagara GPU emitters are ticked at InitView and in PostRenderOpaque).
	 * Note that this callback must only initialize the content for the elements that relates to the tasks it has registered in this batch.
	 *
	 * @param RHICmdList - The command list used to initiate the keys and values on GPU.
	 * @param BatchId - The GPUSortManager batch id (regrouping several similar sort tasks).
	 * @param NumElementsInBatch - The number of elements grouped in the batch (each element maps to a sort task)
	 * @param Flags - Details about the key precision (see EGPUSortFlags::AnyKeyPrecision) and the keygen location (see EGPUSortFlags::AnyKeyGenLocation).
	 * @param KeysUAV - The UAV that holds all the initial keys used to sort the values (being the particle indices here).
	 * @param ValuesUAV - The UAV that holds the initial values (particle indices) to be sorted accordingly to the keys.
	 */
	void GenerateSortKeys(FRHICommandListImmediate& RHICmdList, int32 BatchId, int32 NumElementsInBatch, EGPUSortFlags Flags, FRHIUnorderedAccessView* KeysUAV, FRHIUnorderedAccessView* ValuesUAV);

	void FinishDispatches();

	void UpdateFreeIDsListSizesBuffer(FRHICommandList& RHICmdList, uint32 NumInstances);
	void UpdateFreeIDBuffers(FRHICommandList& RHICmdList, TConstArrayView<FNiagaraComputeExecutionContext*> Instances);

	void SetConstantBuffers(FRHICommandList &RHICmdList, const FNiagaraShaderRef& Shader, const FNiagaraGPUSystemTick& Tick, const FNiagaraComputeInstanceData* Instance);
	void BuildConstantBuffers(FNiagaraGPUSystemTick& Tick) const;

	/** Feature level of this effects system */
	ERHIFeatureLevel::Type FeatureLevel;
	/** Shader platform that will be rendering this effects system */
	EShaderPlatform ShaderPlatform;

	/** The shared GPUSortManager, used to register GPU sort tasks in order to generate sorted particle indices per emitter. */
	TRefCountPtr<FGPUSortManager> GPUSortManager;
	/** All sort tasks registered in AddSortedGPUSimulation(). Holds all the data required in GenerateSortKeys(). */
	TArray<FNiagaraGPUSortInfo> SimulationsToSort;

	// GPU emitter instance count buffer. Contains the actual particle / instance count generate in the GPU tick.
	FNiagaraGPUInstanceCountManager GPUInstanceCounterManager;

#if STATS
	FNiagaraGPUProfiler GPUProfiler;
#endif

	// persistent layouts used to create the constant buffers for the compute sim shader
	TRefCountPtr<FNiagaraRHIUniformBufferLayout> GlobalCBufferLayout;
	TRefCountPtr<FNiagaraRHIUniformBufferLayout> SystemCBufferLayout;
	TRefCountPtr<FNiagaraRHIUniformBufferLayout> OwnerCBufferLayout;
	TRefCountPtr<FNiagaraRHIUniformBufferLayout> EmitterCBufferLayout;

	uint32 FramesBeforeTickFlush = 0;

	FGlobalDistanceFieldParameterData GlobalDistanceFieldParams;

	/** A buffer of list sizes used by UpdateFreeIDBuffers to allow overlapping several dispatches. */
	FRWBuffer FreeIDListSizesBuffer;
	uint32 NumAllocatedFreeIDListSizes = 0;

	struct FDummyUAV
	{
		FVertexBufferRHIRef Buffer;
		FTextureRHIRef Texture;
		FUnorderedAccessViewRHIRef UAV;

		~FDummyUAV();
		void Init(FRHICommandList& RHICmdList, EPixelFormat Format, ENiagaraEmptyUAVType Type, const TCHAR* DebugName);
	};

	struct FDummyUAVPool
	{
		~FDummyUAVPool();

		int32 NextFreeIndex = 0;
		TArray<FDummyUAV> UAVs;
	};

	uint32 DummyUAVAccessCounter = 0;
	mutable TMap<EPixelFormat, FDummyUAVPool> DummyUAVPools[(int)ENiagaraEmptyUAVType::Num];

	void ResetEmptyUAVPools();

	uint32 NumProxiesThatRequireDistanceFieldData = 0;
	uint32 NumProxiesThatRequireDepthBuffer = 0;
	uint32 NumProxiesThatRequireEarlyViewData = 0;

	int32 TotalDispatchesThisFrame = 0;

	bool bRequiresReadback = false;
	TArray<FNiagaraSystemGpuComputeProxy*> ProxiesPerStage[ENiagaraGpuComputeTickStage::Max];

	FNiagaraGpuDispatchList DispatchListPerStage[ENiagaraGpuComputeTickStage::Max];

#if NIAGARA_COMPUTEDEBUG_ENABLED
	TUniquePtr<FNiagaraGpuComputeDebug> GpuComputeDebugPtr;
#endif
#if !UE_BUILD_SHIPPING
	struct FDebugReadbackInfo
	{
		FNiagaraSystemInstanceID InstanceID;
		TSharedPtr<struct FNiagaraScriptDebuggerInfo, ESPMode::ThreadSafe> DebugInfo;
		FNiagaraComputeExecutionContext* Context;
	};
	TArray<FDebugReadbackInfo> GpuDebugReadbackInfos;
#endif
	TUniquePtr<FNiagaraGpuReadbackManager> GpuReadbackManagerPtr;

#if WITH_MGPU
	static const FName TemporalEffectName;
	TArray<FRHIVertexBuffer*> TemporalEffectBuffers;
	ENiagaraGpuComputeTickStage::Type StageToWaitForTemporalEffect = ENiagaraGpuComputeTickStage::First;
	ENiagaraGpuComputeTickStage::Type StageToBroadcastTemporalEffect = ENiagaraGpuComputeTickStage::First;

	void AddTemporalEffectBuffers(FNiagaraDataBuffer* FinalData);
	void BroadcastTemporalEffect(FRHICommandList& RHICmdList);
#endif // WITH_MGPU

};

// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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
#include "NiagaraScriptExecutionContext.h"
#include "NiagaraGPUInstanceCountManager.h"

class FNiagaraIndicesVertexBuffer : public FParticleIndicesVertexBuffer
{
public:

	FNiagaraIndicesVertexBuffer(int32 InIndexCount);

	FUnorderedAccessViewRHIRef VertexBufferUAV;

	// The allocation count.
	const int32 IndexCount;

	// Currently used count.
	int32 UsedIndexCount = 0;
};

enum class ETickStage
{
	PreInitViews,
	PostInitViews,
	PostOpaqueRender
};

class NiagaraEmitterInstanceBatcher : public FFXSystemInterface
{
public:
	using FNiagaraBufferArray = TArray<FRHIUnorderedAccessView*, TMemStackAllocator<>>;
	using FOverlappableTicks = TArray<FNiagaraGPUSystemTick*, TMemStackAllocator<>>;

	NiagaraEmitterInstanceBatcher(ERHIFeatureLevel::Type InFeatureLevel, EShaderPlatform InShaderPlatform)
		: FeatureLevel(InFeatureLevel)
		, ShaderPlatform(InShaderPlatform)
		, ParticleSortBuffers(true)
		// @todo REMOVE THIS HACK
		, LastFrameThatDrainedData(GFrameNumberRenderThread)
		, NumAllocatedFreeIDListSizes(0)
	{
	}

	~NiagaraEmitterInstanceBatcher();

	static const FName Name;
	virtual FFXSystemInterface* GetInterface(const FName& InName) override;

	// The batcher assumes ownership of the data here.
	void GiveSystemTick_RenderThread(FNiagaraGPUSystemTick& Tick);

	void GiveEmitterContextToDestroy_RenderThread(FNiagaraComputeExecutionContext* Context);
	void GiveDataSetToDestroy_RenderThread(FNiagaraDataSet* DataSet);

	void EnqueueDeferredDeletesForDI_RenderThread(TSharedPtr<FNiagaraDataInterfaceProxy, ESPMode::ThreadSafe> Proxy)
	{
		DIProxyDeferredDeletes_RT.Add(MoveTemp(Proxy));
	}

#if WITH_EDITOR
	virtual void Suspend() override {}
	virtual void Resume() override {}
#endif // #if WITH_EDITOR

	virtual void DrawDebug(FCanvas* Canvas) override {}
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

	virtual void Tick(float DeltaTime) override
	{
		BuildBatches();
	}

	// TODO: process queue, build batches from context with the same script
	//  also need to figure out how to handle multiple sets of parameters across a batch
	//	for now this executes every single sim in the queue individually, which is terrible 
	//	in terms of overhead
	void BuildBatches()
	{
	}

	uint32 GetEventSpawnTotal(const FNiagaraComputeExecutionContext *InContext) const;

	virtual void PostRenderOpaque(
		FRHICommandListImmediate& RHICmdList,
		FRHIUniformBuffer* ViewUniformBuffer,
		const class FShaderParametersMetadata* SceneTexturesUniformBufferStruct,
		FRHIUniformBuffer* SceneTexturesUniformBuffer,
		bool bAllowGPUParticleUpdate) override;

	int32 AddSortedGPUSimulation(const FNiagaraGPUSortInfo& SortInfo);
	void SortGPUParticles(FRHICommandListImmediate& RHICmdList);
	void ResolveParticleSortBuffers(FRHICommandListImmediate& RHICmdList, int32 ResultBufferIndex);

	const FParticleIndicesVertexBuffer& GetGPUSortedBuffer() const { return SortedVertexBuffers.Last(); }
	const FGlobalDistanceFieldParameterData& GetGlobalDistanceFieldParameters() const { return GlobalDistanceFieldParams; }

	void ProcessDebugInfo(FRHICommandList &RHICmdLis, const FNiagaraComputeExecutionContext* Context) const;

	void SetDataInterfaceParameters(const TArray<FNiagaraDataInterfaceProxy*>& DataInterfaceProxies, FNiagaraShader* Shader, FRHICommandList &RHICmdList, const FNiagaraComputeInstanceData* Instance, const FNiagaraGPUSystemTick& Tick, uint32 ShaderStageIndex) const;
	void UnsetDataInterfaceParameters(const TArray<FNiagaraDataInterfaceProxy*>& DataInterfaceProxies, FNiagaraShader* Shader, FRHICommandList& RHICmdList, const FNiagaraComputeInstanceData* Instance, const FNiagaraGPUSystemTick& Tick) const;

	void Run(	const FNiagaraGPUSystemTick& Tick,
				const FNiagaraComputeInstanceData* Instance, 
				uint32 UpdateStartInstance, 
				const uint32 TotalNumInstances, 
				FNiagaraShader* Shader,
				FRHICommandList &RHICmdList, 
				FRHIUniformBuffer* ViewUniformBuffer, 
				const FNiagaraGpuSpawnInfo& SpawnInfo,
				bool bCopyBeforeStart = false,
				uint32 DefaultShaderStageIndex = 0,
				uint32 ShaderStageIndex = 0,
				FNiagaraDataInterfaceProxy *IterationInterface = nullptr,
				bool HasRunParticleStage = false
			);

	void ResizeCurrentBuffer(FRHICommandList &RHICmdList, FNiagaraComputeExecutionContext *Context, uint32 NewNumInstances, uint32 PrevNumInstances) const;

	FORCEINLINE FNiagaraGPUInstanceCountManager& GetGPUInstanceCounterManager() { check(IsInRenderingThread()); return GPUInstanceCounterManager; }

	FORCEINLINE EShaderPlatform GetShaderPlatform() const { return ShaderPlatform; }
	FORCEINLINE ERHIFeatureLevel::Type GetFeatureLevel() const { return FeatureLevel; }

	/** Reset the data interfaces and check if the spawn stages are valid */
	bool ResetDataInterfaces(const FNiagaraGPUSystemTick& Tick, FNiagaraComputeInstanceData *Instance, FRHICommandList &RHICmdList, FNiagaraShader* ComputeShader ) const;

	/** Given a shader stage index, find the corresponding data interface */
	FNiagaraDataInterfaceProxy* FindIterationInterface(FNiagaraComputeInstanceData *Instance, const uint32 ShaderStageIndex) const;

	/** Loop over all the data interfaces and call the prestage methods */
	void PreStageInterface(const FNiagaraGPUSystemTick& Tick, FNiagaraComputeInstanceData *Instance, FRHICommandList &RHICmdList, FNiagaraShader* ComputeShader, const uint32 ShaderStageIndex) const;

	/** Loop over all the data interfaces and call the poststage methods */
	void PostStageInterface(const FNiagaraGPUSystemTick& Tick, FNiagaraComputeInstanceData *Instance, FRHICommandList &RHICmdList, FNiagaraShader* ComputeShader, const uint32 ShaderStageIndex) const;

	/** Run the dispatch over multiple stages */
	void DispatchMultipleStages(const FNiagaraGPUSystemTick& Tick, FNiagaraComputeInstanceData *Instance, FRHICommandList &RHICmdList, FRHIUniformBuffer* ViewUniformBuffer, FNiagaraShader* ComputeShader);

private:
	void ExecuteAll(FRHICommandList& RHICmdList, FRHIUniformBuffer* ViewUniformBuffer, bool bSetReadback, ETickStage TickStage);
	void ResizeBuffersAndGatherResources(FOverlappableTicks& OverlappableTick, FRHICommandList& RHICmdList, FNiagaraBufferArray& ReadBuffers, FNiagaraBufferArray& WriteBuffers);
	void DispatchAllOnCompute(FOverlappableTicks& OverlappableTick, FRHICommandList& RHICmdList, FRHIUniformBuffer* ViewUniformBuffer, FNiagaraBufferArray& ReadBuffers, FNiagaraBufferArray& WriteBuffers, bool bSetReadback);

	bool ShouldTickForStage(const FNiagaraGPUSystemTick& Tick, ETickStage TickStage) const;

//	void SimStepReadback(const FNiagaraComputeInstanceData& Instance, FRHICommandList& RHICmdList) const;

	inline uint32 UnpackEmitterDispatchCount(uint8* PackedData)
	{
		return *(uint32*)PackedData;
	}

	inline FNiagaraComputeInstanceData* UnpackEmitterComputeDispatchArray(uint8* PackedData)
	{
		return (FNiagaraComputeInstanceData*)(PackedData + sizeof(uint32));
	}

	void FinishDispatches();
	void ReleaseTicks();

	/** Feature level of this effects system */
	ERHIFeatureLevel::Type FeatureLevel;
	/** Shader platform that will be rendering this effects system */
	EShaderPlatform ShaderPlatform;

	// Number of particle to sort this frame.
	int32 SortedParticleCount = 0;
	int32 NumFramesRequiringShrinking = 0;
	TArray<FNiagaraGPUSortInfo> SimulationsToSort;
	FParticleSortBuffers ParticleSortBuffers;

	// GPU emitter instance count buffer. Contains the actual particle / instance count generate in the GPU tick.
	FNiagaraGPUInstanceCountManager GPUInstanceCounterManager;

	// @todo REMOVE THIS HACK
	uint32 LastFrameThatDrainedData;

	// The result of the GPU sort. Each next element replace the previous.
	// The last entry is used to transfer the result of the ParticleSortBuffers.
	TIndirectArray<FNiagaraIndicesVertexBuffer> SortedVertexBuffers;
	
	TArray<FNiagaraGPUSystemTick> Ticks_RT;
	FGlobalDistanceFieldParameterData GlobalDistanceFieldParams;

	// For deferred deletion of resources
	TSet<FNiagaraComputeExecutionContext*> ContextsToDestroy_RT;
	TSet<FNiagaraDataSet*> DataSetsToDestroy_RT;

	TSet<TSharedPtr<FNiagaraDataInterfaceProxy, ESPMode::ThreadSafe>> DIProxyDeferredDeletes_RT;

	/** A buffer used by the compute shader which determines the list of free particle IDs for each emitter. */
	FRWBuffer FreeIDListSizesBuffer;
	uint32 NumAllocatedFreeIDListSizes;
	uint32 CurrentFreeIDListIndex;
};

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
#include "NiagaraScriptExecutionContext.h"
#include "NiagaraGPUInstanceCountManager.h"

class FGPUSortManager;

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

	NiagaraEmitterInstanceBatcher(ERHIFeatureLevel::Type InFeatureLevel, EShaderPlatform InShaderPlatform, FGPUSortManager* InGPUSortManager);

	~NiagaraEmitterInstanceBatcher();

	static const FName Name;
	virtual FFXSystemInterface* GetInterface(const FName& InName) override;

	/** Notification that the InstanceID has been removed. */
	void InstanceDeallocated_RenderThread(const FNiagaraSystemInstanceID InstanceID);

	// The batcher assumes ownership of the data here.

	void GiveSystemTick_RenderThread(FNiagaraGPUSystemTick& Tick);

	/** Called to release GPU instance counts that the batcher is tracking. */
	void ReleaseInstanceCounts_RenderThread(FNiagaraComputeExecutionContext* ExecContext, FNiagaraDataSet* DataSet);

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

	void ProcessDebugInfo(FRHICommandList &RHICmdLis, const FNiagaraComputeExecutionContext* Context) const;

	void SetDataInterfaceParameters(const TArray<FNiagaraDataInterfaceProxy*>& DataInterfaceProxies, const FNiagaraShaderRef& Shader, FRHICommandList &RHICmdList, const FNiagaraComputeInstanceData* Instance, const FNiagaraGPUSystemTick& Tick, uint32 SimulationStageIndex) const;
	void UnsetDataInterfaceParameters(const TArray<FNiagaraDataInterfaceProxy*>& DataInterfaceProxies, const FNiagaraShaderRef& Shader, FRHICommandList& RHICmdList, const FNiagaraComputeInstanceData* Instance, const FNiagaraGPUSystemTick& Tick) const;

	void Run(	const FNiagaraGPUSystemTick& Tick,
				const FNiagaraComputeInstanceData* Instance, 
				uint32 UpdateStartInstance, 
				const uint32 TotalNumInstances, 
				const FNiagaraShaderRef& Shader,
				FRHICommandList &RHICmdList, 
				FRHIUniformBuffer* ViewUniformBuffer, 
				const FNiagaraGpuSpawnInfo& SpawnInfo,
				bool bCopyBeforeStart = false,
				uint32 DefaultSimulationStageIndex = 0,
				uint32 SimulationStageIndex = 0,
				FNiagaraDataInterfaceProxy *IterationInterface = nullptr,
				bool HasRunParticleStage = false
			);

	void ResizeCurrentBuffer(FRHICommandList &RHICmdList, FNiagaraComputeExecutionContext *Context, uint32 NewNumInstances, uint32 PrevNumInstances) const;

	FORCEINLINE FNiagaraGPUInstanceCountManager& GetGPUInstanceCounterManager() { check(IsInRenderingThread()); return GPUInstanceCounterManager; }

	FORCEINLINE EShaderPlatform GetShaderPlatform() const { return ShaderPlatform; }
	FORCEINLINE ERHIFeatureLevel::Type GetFeatureLevel() const { return FeatureLevel; }

	/** Reset the data interfaces and check if the spawn stages are valid */
	bool ResetDataInterfaces(const FNiagaraGPUSystemTick& Tick, FNiagaraComputeInstanceData *Instance, FRHICommandList &RHICmdList, const FNiagaraShaderRef& ComputeShader ) const;

	/** Given a shader stage index, find the corresponding data interface */
	FNiagaraDataInterfaceProxy* FindIterationInterface(FNiagaraComputeInstanceData *Instance, const uint32 SimulationStageIndex) const;

	/** Loop over all the data interfaces and call the prestage methods */
	void PreStageInterface(const FNiagaraGPUSystemTick& Tick, FNiagaraComputeInstanceData *Instance, FRHICommandList &RHICmdList, const FNiagaraShaderRef& ComputeShader, const uint32 SimulationStageIndex) const;

	/** Loop over all the data interfaces and call the poststage methods */
	void PostStageInterface(const FNiagaraGPUSystemTick& Tick, FNiagaraComputeInstanceData *Instance, FRHICommandList &RHICmdList, const FNiagaraShaderRef& ComputeShader, const uint32 SimulationStageIndex) const;

	NIAGARA_API FRHIUnorderedAccessView* GetEmptyRWBufferFromPool(FRHICommandList& RHICmdList, EPixelFormat Format) const { return GetEmptyUAVFromPool(RHICmdList, Format, false); }
	NIAGARA_API FRHIUnorderedAccessView* GetEmptyRWTextureFromPool(FRHICommandList& RHICmdList, EPixelFormat Format) const { return GetEmptyUAVFromPool(RHICmdList, Format, true); }

	/** Get the shared SortManager, used in the rendering loop to call FGPUSortManager::OnPreRender() and FGPUSortManager::OnPostRenderOpaque() */
	virtual FGPUSortManager* GetGPUSortManager() const override;

private:
	using FEmitterInstanceList = TArray<FNiagaraComputeInstanceData*>;

	void ExecuteAll(FRHICommandList& RHICmdList, FRHIUniformBuffer* ViewUniformBuffer, ETickStage TickStage);
	void ResizeBuffersAndGatherResources(FOverlappableTicks& OverlappableTick, FRHICommandList& RHICmdList, FNiagaraBufferArray& OutputGraphicsBuffers, FEmitterInstanceList& InstancesWithPersistentIDs);
	void TransitionBuffers(FRHICommandList& RHICmdList, const FNiagaraShaderRef& ComputeShader, FNiagaraComputeInstanceData* Instance, uint32 SimulationStageIndex, const FNiagaraDataBuffer*& LastSource, bool& bFreeIDTableTransitioned);
	void DispatchAllOnCompute(FOverlappableTicks& OverlappableTick, FRHICommandList& RHICmdList, FRHIUniformBuffer* ViewUniformBuffer);
	void DispatchMultipleStages(const FNiagaraGPUSystemTick& Tick, FNiagaraComputeInstanceData* Instance, FRHICommandList& RHICmdList, FRHIUniformBuffer* ViewUniformBuffer, const FNiagaraShaderRef& ComputeShader);

	bool ShouldTickForStage(const FNiagaraGPUSystemTick& Tick, ETickStage TickStage) const;

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

//	void SimStepReadback(const FNiagaraComputeInstanceData& Instance, FRHICommandList& RHICmdList) const;

	inline uint32 UnpackEmitterDispatchCount(uint8* PackedData)
	{
		return *(uint32*)PackedData;
	}

	inline FNiagaraComputeInstanceData* UnpackEmitterComputeDispatchArray(uint8* PackedData)
	{
		return (FNiagaraComputeInstanceData*)(PackedData + sizeof(uint32));
	}

	bool UseOverlapCompute();
	void FinishDispatches();
	void ReleaseTicks();
	void ResizeFreeIDsListSizesBuffer(uint32 NumInstances);
	void ClearFreeIDsListSizesBuffer(FRHICommandList& RHICmdList);
	void UpdateFreeIDBuffers(FRHICommandList& RHICmdList, FEmitterInstanceList& Instances);

	void SetConstantBuffers(FRHICommandList &RHICmdList, const FNiagaraShaderRef& Shader, const FNiagaraGPUSystemTick& Tick, const FNiagaraComputeInstanceData* Instance);
	void BuildConstantBuffers(FNiagaraGPUSystemTick& Tick);

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

	// persistent layouts used to create the constant buffers for the compute sim shader
	FRHIUniformBufferLayout GlobalCBufferLayout;
	FRHIUniformBufferLayout SystemCBufferLayout;
	FRHIUniformBufferLayout OwnerCBufferLayout;
	FRHIUniformBufferLayout EmitterCBufferLayout;

	// @todo REMOVE THIS HACK
	uint32 LastFrameThatDrainedData;
	
	TArray<FNiagaraGPUSystemTick> Ticks_RT;
	FGlobalDistanceFieldParameterData GlobalDistanceFieldParams;

	/** A buffer of list sizes used by UpdateFreeIDBuffers to allow overlapping several dispatches. */
	FRWBuffer FreeIDListSizesBuffer;
	uint32 NumAllocatedFreeIDListSizes = 0;
	bool bFreeIDListSizesBufferCleared = false;

	/** List of emitter instances which need their free ID buffers updated post render. */
	FEmitterInstanceList DeferredIDBufferUpdates;

	struct DummyUAV
	{
		FVertexBufferRHIRef Buffer;
		FTexture2DRHIRef Texture;
		FUnorderedAccessViewRHIRef UAV;

		~DummyUAV();
		void Init(FRHICommandList& RHICmdList, EPixelFormat Format, bool IsTexture, const TCHAR* DebugName);
	};

	struct DummyUAVPool
	{
		int32 NextFreeIndex = 0;
		TArray<DummyUAV> UAVs;
	};

	mutable TMap<EPixelFormat, DummyUAVPool> DummyBufferPool;
	mutable TMap<EPixelFormat, DummyUAVPool> DummyTexturePool;

	FRHIUnorderedAccessView* GetEmptyUAVFromPool(FRHICommandList& RHICmdList, EPixelFormat Format, bool IsTexture) const;
	void ResetEmptyUAVPool(TMap<EPixelFormat, DummyUAVPool>& UAVMap, TArray<FRHIUnorderedAccessView*>& Transitions);
	void ResetEmptyUAVPools(FRHICommandList& RHICmdList);
};

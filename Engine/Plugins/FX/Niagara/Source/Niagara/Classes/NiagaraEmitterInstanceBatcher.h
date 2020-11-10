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
#include "NiagaraGPUProfiler.h"

class FGPUSortManager;
class FNiagaraGpuComputeDebug;
class FNiagaraGpuReadbackManager;

enum class ETickStage
{
	PreInitViews,
	PostInitViews,
	PostOpaqueRender,
	Max
};

class NiagaraEmitterInstanceBatcher : public FFXSystemInterface
{
public:
	using FNiagaraTransitionList = TArray<FRHITransitionInfo, TMemStackAllocator<>>;
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
	virtual bool ShouldDebugDraw_RenderThread() const override;
	virtual void DrawDebug_RenderThread(class FRDGBuilder& GraphBuilder, const class FViewInfo& View, const struct FScreenPassRenderTarget& Output) override;
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
	}

	virtual void PostRenderOpaque(
		FRHICommandListImmediate& RHICmdList,
		FRHIUniformBuffer* ViewUniformBuffer,
		const class FShaderParametersMetadata* SceneTexturesUniformBufferStruct,
		FRHIUniformBuffer* SceneTexturesUniformBuffer,
		bool bAllowGPUParticleUpdate) override;

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

	void SetDataInterfaceParameters(const TArray<FNiagaraDataInterfaceProxy*>& DataInterfaceProxies, const FNiagaraShaderRef& Shader, FRHICommandList &RHICmdList, const FNiagaraComputeInstanceData* Instance, const FNiagaraGPUSystemTick& Tick, uint32 SimulationStageIndex) const;
	void UnsetDataInterfaceParameters(const TArray<FNiagaraDataInterfaceProxy*>& DataInterfaceProxies, const FNiagaraShaderRef& Shader, FRHICommandList& RHICmdList, const FNiagaraComputeInstanceData* Instance, const FNiagaraGPUSystemTick& Tick, uint32 SimulationStageIndex) const;

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
				FNiagaraDataInterfaceProxyRW* IterationInterface = nullptr,
				bool HasRunParticleStage = false
			);

	FORCEINLINE FNiagaraGPUInstanceCountManager& GetGPUInstanceCounterManager() { check(IsInRenderingThread()); return GPUInstanceCounterManager; }

	FORCEINLINE EShaderPlatform GetShaderPlatform() const { return ShaderPlatform; }
	FORCEINLINE ERHIFeatureLevel::Type GetFeatureLevel() const { return FeatureLevel; }

	/** Reset the data interfaces and check if the spawn stages are valid */
	bool ResetDataInterfaces(const FNiagaraGPUSystemTick& Tick, FNiagaraComputeInstanceData *Instance, FRHICommandList& RHICmdList, const FNiagaraShaderScript* ShaderScript) const;

	/** Given a shader stage index, find the corresponding data interface */
	FNiagaraDataInterfaceProxyRW* FindIterationInterface(FNiagaraComputeInstanceData* Instance, const uint32 SimulationStageIndex) const;

	/** Loop over all the data interfaces and call the prestage methods */
	void PreStageInterface(const FNiagaraGPUSystemTick& Tick, FNiagaraComputeInstanceData *Instance, FRHICommandList& RHICmdList, const uint32 SimulationStageIndex) const;

	/** Loop over all the data interfaces and call the poststage methods */
	void PostStageInterface(const FNiagaraGPUSystemTick& Tick, FNiagaraComputeInstanceData *Instance, FRHICommandList& RHICmdList, const uint32 SimulationStageIndex) const;

	/** Loop over all data interfaces and call the postsimulate methods */
	void PostSimulateInterface(const FNiagaraGPUSystemTick& Tick, FNiagaraComputeInstanceData* Instance, FRHICommandList& RHICmdList, const FNiagaraShaderScript* ShaderScript) const;

	NIAGARA_API FRHIUnorderedAccessView* GetEmptyRWBufferFromPool(FRHICommandList& RHICmdList, EPixelFormat Format) const { return GetEmptyUAVFromPool(RHICmdList, Format, false); }
	NIAGARA_API FRHIUnorderedAccessView* GetEmptyRWTextureFromPool(FRHICommandList& RHICmdList, EPixelFormat Format) const { return GetEmptyUAVFromPool(RHICmdList, Format, true); }

	/** Get the shared SortManager, used in the rendering loop to call FGPUSortManager::OnPreRender() and FGPUSortManager::OnPostRenderOpaque() */
	virtual FGPUSortManager* GetGPUSortManager() const override;

#if WITH_EDITORONLY_DATA
	void AddDebugReadback(FNiagaraSystemInstanceID InstanceID, TSharedPtr<struct FNiagaraScriptDebuggerInfo, ESPMode::ThreadSafe> DebugInfo, FNiagaraComputeExecutionContext* Context);
#endif

#if WITH_EDITOR
	/** Get the Gpu Compute Debug class, useful for visualizing textures, etc. */
	FNiagaraGpuComputeDebug* GetGpuComputeDebug() const { return GpuComputeDebugPtr.Get(); }
#endif
	FNiagaraGpuReadbackManager* GetGpuReadbackManager() const { return GpuReadbackManagerPtr.Get(); }

private:
	using FEmitterInstanceList = TArray<FNiagaraComputeInstanceData*>;

	struct FDispatchInstance
	{
		FNiagaraGPUSystemTick* Tick = nullptr;
		FNiagaraComputeInstanceData* InstanceData = nullptr;
		int32 StageIndex = 0;
		bool bFinalStage = false;
	};
	using FDispatchInstanceList = TArray<FDispatchInstance, TMemStackAllocator<>>;

	struct FDispatchGroup
	{
		FDispatchInstanceList DispatchInstances;
		FNiagaraTransitionList TransitionsBefore;
		FNiagaraTransitionList TransitionsAfter;
	};
	using FDispatchGroupList = TArray<FDispatchGroup, TMemStackAllocator<>>;

	void UpdateInstanceCountManager(FRHICommandListImmediate& RHICmdList);
	void BuildTickStagePasses(FRHICommandListImmediate& RHICmdList, ETickStage GenerateTickStage);

	void ExecuteAll(FRHICommandList& RHICmdList, FRHIUniformBuffer* ViewUniformBuffer, ETickStage TickStage);
	void AddDestinationBufferTransitions(FDispatchGroup* Group, FNiagaraDataBuffer* DestinationData);
	void BuildDispatchGroups(FOverlappableTicks& OverlappableTick, FRHICommandList& RHICmdList, FDispatchGroupList& DispatchGroups, FEmitterInstanceList& InstancesWithPersistentIDs);
	void DispatchStage(FDispatchInstance& DispatchInstance, uint32 StageIndex, FRHICommandList& RHICmdList, FRHIUniformBuffer* ViewUniformBuffer);
	void DispatchAllOnCompute(FDispatchInstanceList& DispatchInstances, FRHICommandList& RHICmdList, FRHIUniformBuffer* ViewUniformBuffer);

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
	void ResizeFreeIDsListSizesBuffer(FRHICommandList& RHICmdList, uint32 NumInstances);
	void ClearFreeIDsListSizesBuffer(FRHICommandList& RHICmdList);
	void UpdateFreeIDBuffers(FRHICommandList& RHICmdList, FEmitterInstanceList& Instances);

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

	// @todo REMOVE THIS HACK
	uint32 NumFramesWithUnprocessedTicks;
	uint32 LastFrameWithNewTicks;
	
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

	NIAGARA_API FRHIUnorderedAccessView* GetEmptyUAVFromPool(FRHICommandList& RHICmdList, EPixelFormat Format, bool IsTexture) const;
	void ResetEmptyUAVPool(TMap<EPixelFormat, DummyUAVPool>& UAVMap);
	void ResetEmptyUAVPools(FRHICommandList& RHICmdList);

	uint32 NumTicksThatRequireDistanceFieldData = 0;
	uint32 NumTicksThatRequireDepthBuffer = 0;
	uint32 NumTicksThatRequireEarlyViewData = 0;

	TArray<FNiagaraComputeSharedContext*> ContextsPerStage[(int)ETickStage::Max];
	TArray<FNiagaraGPUSystemTick*> TicksPerStage[(int)ETickStage::Max];

	TArray<uint32> CountsToRelease[(int)ETickStage::Max];

#if WITH_EDITOR
	TUniquePtr<FNiagaraGpuComputeDebug> GpuComputeDebugPtr;
#endif
#if WITH_EDITORONLY_DATA
	struct FDebugReadbackInfo
	{
		FNiagaraSystemInstanceID InstanceID;
		TSharedPtr<struct FNiagaraScriptDebuggerInfo, ESPMode::ThreadSafe> DebugInfo;
		FNiagaraComputeExecutionContext* Context;
	};
	TArray<FDebugReadbackInfo> GpuDebugReadbackInfos;
#endif
	TUniquePtr<FNiagaraGpuReadbackManager> GpuReadbackManagerPtr;
};

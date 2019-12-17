// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraEmitterInstance.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraSystem.h"
#include "NiagaraDataInterfaceBindingInstance.h"
#include "Templates/UniquePtr.h"
#include "NiagaraCommon.h"
#include "NiagaraDataInterface.h"
#include "NiagaraSystemFastPath.h"

class FNiagaraWorldManager;
class UNiagaraComponent;
class FNiagaraSystemInstance;
class FNiagaraSystemSimulation;
class NiagaraEmitterInstanceBatcher;
class FNiagaraGPUSystemTick;

class NIAGARA_API FNiagaraSystemInstance 
{
	friend class FNiagaraSystemSimulation;
	friend class FNiagaraGPUSystemTick;

public:
	DECLARE_MULTICAST_DELEGATE(FOnInitialized);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnComplete, FNiagaraSystemInstance*);
	
#if WITH_EDITOR
	DECLARE_MULTICAST_DELEGATE(FOnReset);
	DECLARE_MULTICAST_DELEGATE(FOnDestroyed);
#endif

public:

	/** Defines modes for resetting the System instance. */
	enum class EResetMode
	{
		/** Resets the System instance and simulations. */
		ResetAll,
		/** Resets the System instance but not the simualtions */
		ResetSystem,
		/** Full reinitialization of the system and emitters.  */
		ReInit,
		/** No reset */
		None
	};

	FORCEINLINE bool GetAreDataInterfacesInitialized() const { return bDataInterfacesInitialized; }

	/** Creates a new niagara System instance with the supplied component. */
	explicit FNiagaraSystemInstance(UNiagaraComponent* InComponent);

	/** Cleanup*/
	virtual ~FNiagaraSystemInstance();

	void Cleanup();

	/** Initializes this System instance to simulate the supplied System. */
	void Init(bool bInForceSolo=false);

	void Activate(EResetMode InResetMode = EResetMode::ResetAll);
	void Deactivate(bool bImmediate = false);
	void Complete();

	void SetPaused(bool bInPaused);
	FORCEINLINE bool IsPaused()const { return bPaused; }

	void SetSolo(bool bInSolo);

	void UpdatePrereqs();

	//void RebindParameterCollection(UNiagaraParameterCollectionInstance* OldInstance, UNiagaraParameterCollectionInstance* NewInstance);
	void BindParameters();
	void UnbindParameters(bool bFromComplete = false);

	FORCEINLINE FNiagaraParameterStore& GetInstanceParameters() { return InstanceParameters; }
	
	FNiagaraWorldManager* GetWorldManager()const;
	bool RequiresDistanceFieldData() const;

	/** Requests the the simulation be reset on the next tick. */
	void Reset(EResetMode Mode);

	void ComponentTick(float DeltaSeconds, const FGraphEventRef& MyCompletionGraphEvent);

	/** Initial phase of system instance tick. Must be executed on the game thread. */
	void Tick_GameThread(float DeltaSeconds);
	/** Secondary phase of the system instance tick that can be executed on any thread. */
	void Tick_Concurrent();
	/** Final phase of system instance tick. Must be executed on the game thread. */
	void FinalizeTick_GameThread();

	/** Blocks until any async work for this system instance has completed. Must be called on the game thread. */
	void WaitForAsyncTick(bool bEnsureComplete=false);

	/** Handles completion of the system and returns true if the system is complete. */
	bool HandleCompletion();

	void SetEmitterEnable(FName EmitterName, bool bNewEnableState);

	/** Perform per-tick updates on data interfaces that need it. This can cause systems to complete so cannot be parallelized. */
	void TickDataInterfaces(float DeltaSeconds, bool bPostSimulate);

	ENiagaraExecutionState GetRequestedExecutionState() { return RequestedExecutionState; }
	void SetRequestedExecutionState(ENiagaraExecutionState InState);

	ENiagaraExecutionState GetActualExecutionState() { return ActualExecutionState; }
	void SetActualExecutionState(ENiagaraExecutionState InState);

	float GetSystemTimeSinceRendered() const { return SystemTimeSinceRenderedParam.GetValue(); }

	float GetOwnerLODDistance() const { return GatheredInstanceParameters.LODDistance; }
	float GetOwnerMaxLODDistance() const { return GatheredInstanceParameters.MaxLODDistance; }

	int32 GetNumParticles(int32 EmitterIndex) const { return ParameterNumParticleBindings[EmitterIndex].GetValue(); }
	float GetSpawnCountScale(int32 EmitterIndex) const { return ParameterSpawnCountScaleBindings[EmitterIndex].GetValue(); }

	FVector GetOwnerVelocity() const { return OwnerVelocityParam.GetValue(); }

	FORCEINLINE bool IsComplete()const { return ActualExecutionState == ENiagaraExecutionState::Complete || ActualExecutionState == ENiagaraExecutionState::Disabled; }
	FORCEINLINE bool IsDisabled()const { return ActualExecutionState == ENiagaraExecutionState::Disabled; }

	/** Gets the simulation for the supplied emitter handle. */
	TSharedPtr<FNiagaraEmitterInstance, ESPMode::ThreadSafe> GetSimulationForHandle(const FNiagaraEmitterHandle& EmitterHandle);

	UNiagaraSystem* GetSystem()const;
	FORCEINLINE UNiagaraComponent *GetComponent() { return Component; }
	FORCEINLINE TArray<TSharedRef<FNiagaraEmitterInstance, ESPMode::ThreadSafe> > &GetEmitters()	{ return Emitters; }
	FORCEINLINE const FBox& GetLocalBounds() { return LocalBounds;  }

	FNiagaraEmitterInstance* GetEmitterByID(FGuid InID);

	FORCEINLINE bool IsSolo()const { return bSolo; }

	//TEMPORARY. We wont have a single set of parameters when we're executing system scripts.
	//System params will be pulled in from a data set.
	FORCEINLINE FNiagaraParameterStore& GetParameters() { return InstanceParameters; }

	/** Gets a data set either from another emitter or one owned by the System itself. */
	FNiagaraDataSet* GetDataSet(FNiagaraDataSetID SetID, FName EmitterName = NAME_None);

	/** Gets a multicast delegate which is called whenever this instance is initialized with an System asset. */
	FOnInitialized& OnInitialized();

	/** Gets a multicast delegate which is called whenever this instance is complete. */
	FOnComplete& OnComplete();

#if WITH_EDITOR
	/** Gets a multicast delegate which is called whenever this instance is reset due to external changes in the source System asset. */
	FOnReset& OnReset();

	FOnDestroyed& OnDestroyed();
#endif

#if WITH_EDITORONLY_DATA
	bool GetIsolateEnabled() const;
#endif

	FNiagaraSystemInstanceID GetId() { return ID; }

	/** Returns the instance data for a particular interface for this System. */
	FORCEINLINE void* FindDataInterfaceInstanceData(UNiagaraDataInterface* Interface) 
	{
		if (int32* InstDataOffset = DataInterfaceInstanceDataOffsets.Find(Interface))
		{
			return &DataInterfaceInstanceData[*InstDataOffset];
		}
		return nullptr;
	}

	bool UsesEmitter(const UNiagaraEmitter* Emitter)const;
	bool UsesScript(const UNiagaraScript* Script)const;
	//bool UsesDataInterface(UNiagaraDataInterface* Interface);
	bool UsesCollection(const UNiagaraParameterCollection* Collection)const;

	FORCEINLINE bool IsPendingSpawn()const { return bPendingSpawn; }
	FORCEINLINE void SetPendingSpawn(bool bInValue) { bPendingSpawn = bInValue; }

	FORCEINLINE float GetAge()const { return Age; }
	FORCEINLINE int32 GetTickCount() const { return TickCount; }
	
	FORCEINLINE TSharedPtr<FNiagaraSystemSimulation, ESPMode::ThreadSafe> GetSystemSimulation()const
	{
		return SystemSimulation; 
	}

	bool IsReadyToRun() const;

	FORCEINLINE bool HasTickingEmitters()const { return bHasTickingEmitters; }

	UNiagaraParameterCollectionInstance* GetParameterCollectionInstance(UNiagaraParameterCollection* Collection);

	/** 
	Manually advances this system's simulation by the specified number of ticks and tick delta. 
	To be advanced in this way a system must be in solo mode or moved into solo mode which will add additional overhead.
	*/
	void AdvanceSimulation(int32 TickCountToSimulate, float TickDeltaSeconds);

#if WITH_EDITORONLY_DATA
	/** Request that this simulation capture a frame. Cannot capture if disabled or already completed.*/
	bool RequestCapture(const FGuid& RequestId);

	/** Poll for previous frame capture requests. Once queried and bool is returned, the results are cleared from this system instance.*/
	bool QueryCaptureResults(const FGuid& RequestId, TArray<TSharedPtr<struct FNiagaraScriptDebuggerInfo, ESPMode::ThreadSafe>>& OutCaptureResults);

	/** Only call from within the script execution states. Value is null if not capturing a frame.*/
	TArray<TSharedPtr<struct FNiagaraScriptDebuggerInfo, ESPMode::ThreadSafe>>* GetActiveCaptureResults();

	/** Only call from within the script execution states. Does nothing if not capturing a frame.*/
	void FinishCapture();

	/** Only call from within the script execution states. Value is false if not capturing a frame.*/
	bool ShouldCaptureThisFrame() const;

	/** Only call from within the script execution states. Value is nullptr if not capturing a frame.*/
	TSharedPtr<struct FNiagaraScriptDebuggerInfo, ESPMode::ThreadSafe> GetActiveCaptureWrite(const FName& InHandleName, ENiagaraScriptUsage InUsage, const FGuid& InUsageId);

#endif

	/** Dumps all of this systems info to the log. */
	void Dump()const;

	/** Dumps information about the instances tick to the log */
	void DumpTickInfo(FOutputDevice& Ar);

	bool GetPerInstanceDataAndOffsets(void*& OutData, uint32& OutDataSize, TMap<TWeakObjectPtr<UNiagaraDataInterface>, int32>*& OutOffsets);

	NiagaraEmitterInstanceBatcher* GetBatcher() const { return Batcher; }

	static bool AllocateSystemInstance(class UNiagaraComponent* InComponent, TUniquePtr< FNiagaraSystemInstance >& OutSystemInstanceAllocation);
	static bool DeallocateSystemInstance(TUniquePtr< FNiagaraSystemInstance >& SystemInstanceAllocation);
	/*void SetHasGPUEmitters(bool bInHasGPUEmitters) { bHasGPUEmitters = bInHasGPUEmitters; }*/
	bool HasGPUEmitters() { return bHasGPUEmitters;  }

	int32 GetDetailLevel()const;

	FORCEINLINE void BeginAsyncWork()
	{
		bAsyncWorkInProgress = true;
		bNeedsFinalize = true;
	}

	void TickInstanceParameters_GameThread(float DeltaSeconds);

	void TickInstanceParameters_Concurrent();

	void TickFastPathBindings();

	void ResetFastPathBindings();

	FNiagaraSystemFastPath::FParamMap0& GetFastPathMap() { return FastPathMap; }

	FORCEINLINE void SetLODDistance(float InLODDistance, float InMaxLODDistance);

	void DestroyDataInterfaceInstanceData();

	/** Builds the emitter simulations. */
	void InitEmitters();

	/** Resets the System, emitter simulations, and renderers to initial conditions. */
	void ReInitInternal();
	/** Resets for restart, assumes no change in emitter setup */
	void ResetInternal(bool bResetSimulations);

	/** Call PrepareForSImulation on each data source from the simulations and determine which need per-tick updates.*/
	void InitDataInterfaces();	
	
	/** Calculates the distance to use for distance based LODing / culling. */
	float GetLODDistance();

	/** Calculates which tick group the instance should be in. */
	ETickingGroup CalculateTickGroup();

	/** Index of this instance in the system simulation. */
	int32 SystemInstanceIndex;

	TSharedPtr<class FNiagaraSystemSimulation, ESPMode::ThreadSafe> SystemSimulation;

	UNiagaraComponent* Component;

	UActorComponent* PrereqComponent;

	ENiagaraTickBehavior TickBehavior;

	/** The age of the System instance. */
	float Age;

	/** The tick count of the System instance. */
	int32 TickCount;

	/** LODDistance driven by our component. */
	float LODDistance;
	float MaxLODDistance;

	TMap<FNiagaraDataSetID, FNiagaraDataSet> ExternalEvents;

	TArray< TSharedRef<FNiagaraEmitterInstance, ESPMode::ThreadSafe> > Emitters;

	FOnInitialized OnInitializedDelegate;
	FOnComplete OnCompleteDelegate;

#if WITH_EDITOR
	FOnReset OnResetDelegate;
	FOnDestroyed OnDestroyedDelegate;
#endif

#if WITH_EDITORONLY_DATA
	TSharedPtr<TArray<TSharedPtr<struct FNiagaraScriptDebuggerInfo, ESPMode::ThreadSafe>>, ESPMode::ThreadSafe> CurrentCapture;
	TSharedPtr<FGuid, ESPMode::ThreadSafe> CurrentCaptureGuid;
	bool bWasSoloPriorToCaptureRequest;
	TMap<FGuid, TSharedPtr<TArray<TSharedPtr<struct FNiagaraScriptDebuggerInfo, ESPMode::ThreadSafe>>, ESPMode::ThreadSafe> > CapturedFrames;
#endif

	FNiagaraSystemInstanceID ID;
	FName IDName;
	
	/** Per instance data for any data interfaces requiring it. */
	TArray<uint8, TAlignedHeapAllocator<16>> DataInterfaceInstanceData;

	/** Map of data interfaces to their instance data. */
	TMap<TWeakObjectPtr<UNiagaraDataInterface>, int32> DataInterfaceInstanceDataOffsets;

	/** Per system instance parameters. These can be fed by the component and are placed into a dataset for execution for the system scripts. */
	FNiagaraParameterStore InstanceParameters;
	
	FNiagaraParameterDirectBinding<FVector> OwnerPositionParam;
	FNiagaraParameterDirectBinding<FVector> OwnerScaleParam;
	FNiagaraParameterDirectBinding<FVector> OwnerVelocityParam;
	FNiagaraParameterDirectBinding<FVector> OwnerXAxisParam;
	FNiagaraParameterDirectBinding<FVector> OwnerYAxisParam;
	FNiagaraParameterDirectBinding<FVector> OwnerZAxisParam;

	FNiagaraParameterDirectBinding<FQuat> OwnerRotationParam;

	FNiagaraParameterDirectBinding<FMatrix> OwnerTransformParam;
	FNiagaraParameterDirectBinding<FMatrix> OwnerInverseParam;
	FNiagaraParameterDirectBinding<FMatrix> OwnerTransposeParam;
	FNiagaraParameterDirectBinding<FMatrix> OwnerInverseTransposeParam;
	FNiagaraParameterDirectBinding<FMatrix> OwnerTransformNoScaleParam;
	FNiagaraParameterDirectBinding<FMatrix> OwnerInverseNoScaleParam;

	FNiagaraParameterDirectBinding<float> OwnerDeltaSecondsParam;
	FNiagaraParameterDirectBinding<float> OwnerInverseDeltaSecondsParam;
	FNiagaraParameterDirectBinding<float> OwnerEngineTimeParam;
	FNiagaraParameterDirectBinding<float> OwnerEngineRealtimeParam;
	FNiagaraParameterDirectBinding<float> SystemAgeParam;
	FNiagaraParameterDirectBinding<int32> SystemTickCountParam;

	FNiagaraParameterDirectBinding<float> OwnerLODDistanceParam;
	FNiagaraParameterDirectBinding<float> OwnerLODDistanceFractionParam;
	FNiagaraParameterDirectBinding<int32> SystemNumEmittersParam;
	FNiagaraParameterDirectBinding<int32> SystemNumEmittersAliveParam;

	FNiagaraParameterDirectBinding<float> SystemTimeSinceRenderedParam;

	FNiagaraParameterDirectBinding<int32> OwnerExecutionStateParam;

	TArray<FNiagaraParameterDirectBinding<float>> ParameterSpawnCountScaleBindings;
	TArray<FNiagaraParameterDirectBinding<int32>> ParameterNumParticleBindings;
	TArray<FNiagaraParameterDirectBinding<int32>> ParameterTotalSpawnedParticlesBindings;

	/** Indicates whether this instance must update itself rather than being batched up as most instances are. */
	uint32 bSolo : 1;
	uint32 bForceSolo : 1;

	uint32 bPendingSpawn : 1;
	uint32 bNotifyOnCompletion : 1;

	/** If this instance has any currently ticking emitters. If false, allows us to skip some work. */
	uint32 bHasTickingEmitters : 1;

	/** If this system is paused. When paused it will not tick and never complete etc. */
	uint32 bPaused : 1;
	/** If this system has emitters that will run GPU Simulations */
	uint32 bHasGPUEmitters : 1;
	/** The system contains data interfaces that can have tick group prerequisites. */
	uint32 bDataInterfacesHaveTickPrereqs : 1;

	/** True if our bounds have changed and we require pushing that to the rendering thread. */
	uint32 bIsTransformDirty : 1;

	/** True if we require a call to FinalizeTick_GameThread(). Typically this is called from a GT task but can be called in WaitForAsync. */
	uint32 bNeedsFinalize : 1;

	uint32 bDataInterfacesInitialized : 1;

	uint32 bAlreadyBound : 1;

	uint32 bLODDistanceIsValid : 1;

	/** True if we have async work in flight. */
	volatile bool bAsyncWorkInProgress;

	/** Cached delta time, written during Tick_GameThread and used during other phases. */
	float CachedDeltaSeconds;

	/** Time since we last forced a bounds update. */
	float TimeSinceLastForceUpdateTransform;

	/** Current calculated local bounds. */
	FBox LocalBounds;

	/* Execution state requested by external code/BPs calling Activate/Deactivate. */
	ENiagaraExecutionState RequestedExecutionState;

	/** Copy of simulations internal state so that it can be passed to emitters etc. */
	ENiagaraExecutionState ActualExecutionState;

	NiagaraEmitterInstanceBatcher* Batcher = nullptr;

public:
	// Transient data that is accumulated during tick.
	uint32 TotalParamSize = 0;
	uint32 ActiveGPUEmitterCount = 0;
	int32 GPUDataInterfaceInstanceDataSize = 0;

	struct FInstanceParameters
	{
		float DeltaSeconds;

		FTransform ComponentTrans;
		FVector OldPos;

		float LODDistance;
		float MaxLODDistance;
		float TimeSeconds;
		float RealTimeSeconds;

		float Age;
		int32 TickCount;

		TArray<int32> EmitterNumParticles;
		TArray<int32> EmitterTotalSpawnedParticles;
		TArray<float> EmitterSpawnCountScale;
		int32 NumAlive;

		float SafeTimeSinceRendererd;

		ENiagaraExecutionState RequestedExecutionState;

		void Init(int32 NumEmitters)
		{
			EmitterNumParticles.AddUninitialized(NumEmitters);
			EmitterTotalSpawnedParticles.AddUninitialized(NumEmitters);
			EmitterSpawnCountScale.AddUninitialized(NumEmitters);
		}
	};

	FInstanceParameters GatheredInstanceParameters;

	FNiagaraSystemFastPath::FParamMap0 FastPathMap;

	TArray<TNiagaraFastPathRangedInputBinding<int32>> FastPathIntUpdateRangedInputBindings;
	TArray<TNiagaraFastPathRangedInputBinding<float>> FastPathFloatUpdateRangedInputBindings;
	TArray<TNiagaraFastPathUserParameterInputBinding<int32>> FastPathIntUserParameterInputBindings;
	TArray<TNiagaraFastPathUserParameterInputBinding<float>> FastPathFloatUserParameterInputBindings;
};

FORCEINLINE void FNiagaraSystemInstance::SetLODDistance(float InLODDistance, float InMaxLODDistance)
{
	bLODDistanceIsValid = true;
	LODDistance = InLODDistance; 
	MaxLODDistance = InMaxLODDistance;
}
// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*==============================================================================
NiagaraEmitterInstance.h: Niagara emitter simulation class
==============================================================================*/
#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"
#include "NiagaraCommon.h"
#include "NiagaraDataSet.h"
#include "NiagaraEvents.h"
#include "NiagaraCollision.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraEmitter.h"
#include "NiagaraScriptExecutionContext.h"
#include "NiagaraBoundsCalculator.h"

class FNiagaraSystemInstance;
struct FNiagaraEmitterHandle;
class UNiagaraParameterCollection;
class UNiagaraParameterCollectionInstance;
class NiagaraEmitterInstanceBatcher;


/**
* A Niagara particle simulation.
*/
class FNiagaraEmitterInstance
{
public:
	explicit FNiagaraEmitterInstance(FNiagaraSystemInstance* InParentSystemInstance);
	bool bDumpAfterEvent;
	virtual ~FNiagaraEmitterInstance();

	void Init(int32 InEmitterIdx, FName SystemInstanceName);

	void ResetSimulation(bool bKillExisting=true);

	/** Called after all emitters in an System have been initialized, allows emitters to access information from one another. */
	void PostInitSimulation();

	void DirtyDataInterfaces();
	
	/** Replaces the binding for a single parameter colleciton instance. If for example the component begins to override the global instance. */
	//void RebindParameterCollection(UNiagaraParameterCollectionInstance* OldInstance, UNiagaraParameterCollectionInstance* NewInstance);
	void BindParameters();
	void UnbindParameters();

	bool IsAllowedToExecute() const;

	void PreTick();
	void Tick(float DeltaSeconds);
	void PostTick();
	bool HandleCompletion(bool bForce=false);

	bool RequiredPersistentID()const;

	FORCEINLINE bool ShouldTick()const { return ExecutionState == ENiagaraExecutionState::Active || GetNumParticles() > 0; }

	uint32 CalculateEventSpawnCount(const FNiagaraEventScriptProperties &EventHandlerProps, TArray<int32, TInlineAllocator<16>>& EventSpawnCounts, FNiagaraDataSet *EventSet);

	/** Generate system bounds, reading back data from the GPU will introduce a stall and should only be used for debug purposes. */
	FBox CalculateDynamicBounds(const bool bReadGPUSimulation = false);
	NIAGARA_API void CalculateFixedBounds(const FTransform& ToWorldSpace);

	FNiagaraDataSet& GetData()const { return *ParticleDataSet; }
	
	FORCEINLINE bool IsDisabled()const { return ExecutionState == ENiagaraExecutionState::Disabled; }
	FORCEINLINE bool IsComplete()const { return ExecutionState == ENiagaraExecutionState::Complete || ExecutionState == ENiagaraExecutionState::Disabled; }
		
	/** Create a new NiagaraRenderer. The old renderer is not immediately deleted, but instead put in the ToBeRemoved list.*/
	//void NIAGARA_API UpdateEmitterRenderer(ERHIFeatureLevel::Type FeatureLevel, TArray<NiagaraRenderer*>& ToBeAddedList, TArray<NiagaraRenderer*>& ToBeRemovedList);

	FORCEINLINE int32 GetNumParticles()const
	{
		// Note: For ENiagaraSimTarget::GPUComputeSim this data is latent
		if (ParticleDataSet->GetCurrentData())
		{
			return ParticleDataSet->GetCurrentData()->GetNumInstances();
		}
		return 0;
	}
	FORCEINLINE int32 GetTotalSpawnedParticles()const	{ return TotalSpawnedParticles; }

	NIAGARA_API const FNiagaraEmitterHandle& GetEmitterHandle() const;

	FNiagaraSystemInstance* GetParentSystemInstance()const	{ return ParentSystemInstance; }

	float NIAGARA_API GetTotalCPUTime();
	int	NIAGARA_API GetTotalBytesUsed();
	
	ENiagaraExecutionState NIAGARA_API GetExecutionState()	{ return ExecutionState; }
	void NIAGARA_API SetExecutionState(ENiagaraExecutionState InState);

	FNiagaraDataSet* GetDataSet(FNiagaraDataSetID SetID);

	FBox GetBounds();

	FNiagaraScriptExecutionContext& GetSpawnExecutionContext() { return SpawnExecContext; }
	FNiagaraScriptExecutionContext& GetUpdateExecutionContext() { return UpdateExecContext; }
	TArray<FNiagaraScriptExecutionContext>& GetEventExecutionContexts() { return EventExecContexts; }

	FORCEINLINE FName GetCachedIDName()const { return CachedIDName; }
	FORCEINLINE UNiagaraEmitter* GetCachedEmitter()const { return CachedEmitter; }

	TArray<FNiagaraSpawnInfo>& GetSpawnInfo() { return SpawnInfos; }

	NIAGARA_API bool IsReadyToRun() const;

	void Dump()const;

	bool WaitForDebugInfo();

	FNiagaraComputeExecutionContext* GetGPUContext()const
	{
		return GPUExecContext;
	}

	void SetSystemFixedBoundsOverride(FBox SystemFixedBounds);

private:

	void CheckForErrors();

	/** The index of our emitter in our parent system instance. */
	int32 EmitterIdx;

	/* The age of the emitter*/
	float Age;
	/* how many loops this emitter has gone through */
	uint32 Loops;

	int32 TickCount;

	int32 TotalSpawnedParticles;
	
	/** Typical resets must be deferred until the tick as the RT could still be using the current buffer. */
	uint32 bResetPending : 1;

	/* Seconds taken to process everything (including rendering) */
	float CPUTimeMS;
	/* Emitter tick state */
	ENiagaraExecutionState ExecutionState;
	/* Emitter bounds */
	FBox CachedBounds;

	/** Array of all spawn info driven by our owning emitter script. */
	TArray<FNiagaraSpawnInfo> SpawnInfos;

	FNiagaraScriptExecutionContext SpawnExecContext;
	FNiagaraScriptExecutionContext UpdateExecContext;
	FNiagaraComputeExecutionContext* GPUExecContext;
	TArray<FNiagaraScriptExecutionContext> EventExecContexts;

	FNiagaraParameterDirectBinding<float> SpawnIntervalBinding;
	FNiagaraParameterDirectBinding<float> InterpSpawnStartBinding;
	FNiagaraParameterDirectBinding<int32> SpawnGroupBinding;

	FNiagaraParameterDirectBinding<float> EmitterAgeBindingGPU;

	FNiagaraParameterDirectBinding<float> SpawnEmitterAgeBinding;
	FNiagaraParameterDirectBinding<float> UpdateEmitterAgeBinding;
	TArray<FNiagaraParameterDirectBinding<float>> EventEmitterAgeBindings;

	FNiagaraParameterDirectBinding<int32> SpawnExecCountBinding;
	FNiagaraParameterDirectBinding<int32> UpdateExecCountBinding;
	TArray<FNiagaraParameterDirectBinding<int32>> EventExecCountBindings;

	/*
	FNiagaraParameterDirectBinding<int32> SpawnTotalSpawnedParticlesBinding;
	FNiagaraParameterDirectBinding<int32> UpdateTotalSpawnedParticlesBinding;
	FNiagaraParameterDirectBinding<int32> GPUTotalSpawnedParticlesBinding;
	*/

	FNiagaraParameterDirectBinding<int32> SpawnRandomSeedBinding;
	FNiagaraParameterDirectBinding<int32> UpdateRandomSeedBinding;
	FNiagaraParameterDirectBinding<int32> GPURandomSeedBinding;

	/*
	FNiagaraParameterDirectBinding<int32> SpawnDeterminismBinding;
	FNiagaraParameterDirectBinding<int32> UpdateDeterminismBinding;
	FNiagaraParameterDirectBinding<int32> GPUDeterminismBinding;
	*/
	
	/** particle simulation data. Must be a shared ref as various things on the RT can have direct ref to it. */
	FNiagaraDataSet* ParticleDataSet;

	FNiagaraSystemInstance *ParentSystemInstance;

	/** Raw pointer to the emitter that we're instanced from. Raw ptr should be safe here as we check for the validity of the system and it's emitters higher up before any ticking. */
	UNiagaraEmitter* CachedEmitter;
	FName CachedIDName;

	TArray<FNiagaraDataSet*> UpdateScriptEventDataSets;
	TArray<FNiagaraDataSet*> SpawnScriptEventDataSets;
	TMap<FNiagaraDataSetID, FNiagaraDataSet*> DataSetMap;

	TArray<bool> UpdateEventGeneratorIsSharedByIndex;
	TArray<bool> SpawnEventGeneratorIsSharedByIndex;

	FName OwnerSystemInstanceName;

	/** Cached fixed bounds of the parent system which override this Emitter Instances bounds if set. Whenever we initialize the owning SystemInstance we will reconstruct this
	 ** EmitterInstance and the cached bounds will be unset. */
	TOptional<FBox> CachedSystemFixedBounds;

#if WITH_EDITORONLY_DATA
	bool CheckAttributesForRenderer(int32 Index);
#endif

#if !UE_BUILD_SHIPPING
	bool bEncounteredNaNs;
#endif

	/** A parameter store which contains the data interfaces parameters which were defined by the scripts. */
	FNiagaraParameterStore ScriptDefinedDataInterfaceParameters;

	NiagaraEmitterInstanceBatcher* Batcher = nullptr;

	/** Data required for handling events. */
	TArray<FNiagaraEventHandlingInfo> EventHandlingInfo;
	int32 EventSpawnTotal;

	/** Optional list of bounds calculators. */
	TArray<TUniquePtr<FNiagaraBoundsCalculator>, TInlineAllocator<1>> BoundsCalculators;
};

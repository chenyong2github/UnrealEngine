// Copyright Epic Games, Inc. All Rights Reserved.

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
struct FNiagaraEmitterCompiledData;

/**
* A Niagara particle simulation.
*/
class FNiagaraEmitterInstance
{
private:
	struct FEventInstanceData
	{
		TArray<FNiagaraScriptExecutionContext> EventExecContexts;
		TArray<FNiagaraParameterDirectBinding<int32>> EventExecCountBindings;

		TArray<FNiagaraDataSet*> UpdateScriptEventDataSets;
		TArray<FNiagaraDataSet*> SpawnScriptEventDataSets;

		TArray<bool> UpdateEventGeneratorIsSharedByIndex;
		TArray<bool> SpawnEventGeneratorIsSharedByIndex;

		/** Data required for handling events. */
		TArray<FNiagaraEventHandlingInfo> EventHandlingInfo;
		int32 EventSpawnTotal = 0;
	};

public:
	explicit FNiagaraEmitterInstance(FNiagaraSystemInstance* InParentSystemInstance);
	virtual ~FNiagaraEmitterInstance();

	void Init(int32 InEmitterIdx, FNiagaraSystemInstanceID SystemInstanceID);

	void ResetSimulation(bool bKillExisting = true);

	void DirtyDataInterfaces();

	/** Replaces the binding for a single parameter collection instance. If for example the component begins to override the global instance. */
	//void RebindParameterCollection(UNiagaraParameterCollectionInstance* OldInstance, UNiagaraParameterCollectionInstance* NewInstance);
	void BindParameters(bool bExternalOnly);
	void UnbindParameters(bool bExternalOnly);

	bool IsAllowedToExecute() const;

	void PreTick();
	void Tick(float DeltaSeconds);
	void PostTick();
	bool HandleCompletion(bool bForce = false);

	bool RequiresPersistentIDs() const;

	FORCEINLINE bool ShouldTick()const { return ExecutionState == ENiagaraExecutionState::Active || GetNumParticles() > 0; }

	uint32 CalculateEventSpawnCount(const FNiagaraEventScriptProperties &EventHandlerProps, TArray<int32, TInlineAllocator<16>>& EventSpawnCounts, FNiagaraDataSet *EventSet);

#if WITH_EDITOR
	/** Potentially reads back data from the GPU which will introduce a stall and should only be used for debug purposes. */
	NIAGARA_API void CalculateFixedBounds(const FTransform& ToWorldSpace);
#endif

	FNiagaraDataSet& GetData()const { return *ParticleDataSet; }

	FORCEINLINE bool IsDisabled()const { return ExecutionState == ENiagaraExecutionState::Disabled; }
	FORCEINLINE bool IsInactive()const { return ExecutionState == ENiagaraExecutionState::Inactive; }
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
	FORCEINLINE int32 GetTotalSpawnedParticles()const { return TotalSpawnedParticles; }
	FORCEINLINE const FNiagaraEmitterScalabilitySettings& GetScalabilitySettings()const { return CachedEmitter->GetScalabilitySettings(); }

	NIAGARA_API const FNiagaraEmitterHandle& GetEmitterHandle() const;

	FNiagaraSystemInstance* GetParentSystemInstance()const { return ParentSystemInstance; }

	float NIAGARA_API GetTotalCPUTimeMS();
	int	NIAGARA_API GetTotalBytesUsed();

	ENiagaraExecutionState NIAGARA_API GetExecutionState() { return ExecutionState; }
	void NIAGARA_API SetExecutionState(ENiagaraExecutionState InState);

	FBox GetBounds();

	FNiagaraScriptExecutionContext& GetSpawnExecutionContext() { return SpawnExecContext; }
	FNiagaraScriptExecutionContext& GetUpdateExecutionContext() { return UpdateExecContext; }
	TArrayView<FNiagaraScriptExecutionContext> GetEventExecutionContexts();

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

	bool FindBinding(const FNiagaraUserParameterBinding& InBinding, TArray<UMaterialInterface*>& OutMaterials) const;


	bool HasTicked() const { return TickCount > 0;  }

private:
	void CheckForErrors();

	void BuildConstantBufferTable(
		const FNiagaraScriptExecutionContext& ExecContext,
		FScriptExecutionConstantBufferTable& ConstantBufferTable) const;

	/** Generate emitter bounds */
	FBox InternalCalculateDynamicBounds(int32 ParticleCount) const;

	/** Array of all spawn info driven by our owning emitter script. */
	TArray<FNiagaraSpawnInfo> SpawnInfos;

	FNiagaraScriptExecutionContext SpawnExecContext;
	FNiagaraScriptExecutionContext UpdateExecContext;
	FNiagaraComputeExecutionContext* GPUExecContext = nullptr;

	FNiagaraParameterDirectBinding<float> SpawnIntervalBinding;
	FNiagaraParameterDirectBinding<float> InterpSpawnStartBinding;
	FNiagaraParameterDirectBinding<int32> SpawnGroupBinding;

	FNiagaraParameterDirectBinding<int32> SpawnExecCountBinding;
	FNiagaraParameterDirectBinding<int32> UpdateExecCountBinding;

	/** Optional list of bounds calculators. */
	TArray<TUniquePtr<FNiagaraBoundsCalculator>, TInlineAllocator<1>> BoundsCalculators;

	TSharedPtr<const FNiagaraEmitterCompiledData> CachedEmitterCompiledData;

	TUniquePtr<FEventInstanceData> EventInstanceData;

	/** A parameter store which contains the data interfaces parameters which were defined by the scripts. */
	FNiagaraParameterStore ScriptDefinedDataInterfaceParameters;

	/* Emitter bounds */
	FBox CachedBounds;

	/** Cached fixed bounds of the parent system which override this Emitter Instances bounds if set. Whenever we initialize the owning SystemInstance we will reconstruct this
	 ** EmitterInstance and the cached bounds will be unset. */
	TOptional<FBox> CachedSystemFixedBounds;

	FNiagaraSystemInstanceID OwnerSystemInstanceID;

	NiagaraEmitterInstanceBatcher* Batcher = nullptr;

	/** particle simulation data. Must be a shared ref as various things on the RT can have direct ref to it. */
	FNiagaraDataSet* ParticleDataSet = nullptr;

	FNiagaraSystemInstance *ParentSystemInstance = nullptr;

	/** Raw pointer to the emitter that we're instanced from. Raw ptr should be safe here as we check for the validity of the system and it's emitters higher up before any ticking. */
	UNiagaraEmitter* CachedEmitter = nullptr;
	FName CachedIDName;

	/** The index of our emitter in our parent system instance. */
	int32 EmitterIdx = INDEX_NONE;

	/* The age of the emitter*/
	float EmitterAge = 0.0f;

	int32 TickCount = 0;

	int32 TotalSpawnedParticles = 0;
	
	/* Cycles taken to process the tick. */
	uint32 CPUTimeCycles = 0;

	uint32 MaxRuntimeAllocation = 0;

	int32 MaxAllocationCount = 0;
	int32 MinOverallocation = -1;
	int32 ReallocationCount = 0;

	uint32 MaxInstanceCount = 0;

	/* Emitter tick state */
	ENiagaraExecutionState ExecutionState = ENiagaraExecutionState::Inactive;

	/** Typical resets must be deferred until the tick as the RT could still be using the current buffer. */
	uint32 bResetPending : 1;
};

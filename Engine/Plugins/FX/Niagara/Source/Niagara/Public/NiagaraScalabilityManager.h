// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraEffectType.h"

#include "NiagaraScalabilityManager.generated.h"

class FNiagaraWorldManager;
class UNiagaraSystem;
class UNiagaraComponent;
class FReferenceCollector;

//Disabled for now until we can spend more time on a good method of applying the data that is gathered.
#define ENABLE_NIAGARA_RUNTIME_CYCLE_COUNTERS (0)

#if ENABLE_NIAGARA_RUNTIME_CYCLE_COUNTERS
struct FNiagaraRuntimeCycleCounter
{
	FORCEINLINE FNiagaraRuntimeCycleCounter(int32* InCycleDest)
		: StartCycles(0)
		, CycleDest(InCycleDest)
	{
	}

	FORCEINLINE FNiagaraRuntimeCycleCounter(UNiagaraSystem* System, bool bGameThread, bool bConcurrent)
		: StartCycles(0)
		, CycleDest(nullptr)
	{
		CycleDest = System->GetCycleCounter(bGameThread, bConcurrent);
	}

	FORCEINLINE void Begin()
	{
		if (CycleDest)
		{
			StartCycles = FPlatformTime::Cycles();
		}
	}

	FORCEINLINE void End()
	{
		if (CycleDest)
		{
			FPlatformAtomics::InterlockedAdd(CycleDest, FPlatformTime::Cycles() - StartCycles);
		}
	}

private:
	uint32 StartCycles;
	int32* CycleDest;
};
#else
struct FNiagaraRuntimeCycleCounter
{
	FORCEINLINE FNiagaraRuntimeCycleCounter(int32* InCycleDest) {}
	FORCEINLINE FNiagaraRuntimeCycleCounter(UNiagaraSystem* System, bool bGameThread, bool bConcurrent) {}
	FORCEINLINE void Begin() {}
	FORCEINLINE void End() {}
};
#endif

struct FNiagaraScopedRuntimeCycleCounter : public FNiagaraRuntimeCycleCounter
{
	FORCEINLINE FNiagaraScopedRuntimeCycleCounter(int32* InCycleDest)
		: FNiagaraRuntimeCycleCounter(InCycleDest)
	{
		Begin();
	}

	FORCEINLINE FNiagaraScopedRuntimeCycleCounter(UNiagaraSystem* System, bool bGameThread, bool bConcurrent)
		: FNiagaraRuntimeCycleCounter(System, bGameThread, bConcurrent)
	{
		Begin();
	}

	FORCEINLINE ~FNiagaraScopedRuntimeCycleCounter()
	{
		End();
	}
};

struct FNiagaraScalabilityState
{
	FNiagaraScalabilityState()
		: Significance(1.0f)
		, bCulled(0)
		, bPreviousCulled(0)
#if DEBUG_SCALABILITY_STATE
		, bCulledByDistance(0)
		, bCulledByInstanceCount(0)
		, bCulledByVisibility(0)
#endif
	{
	}

	FNiagaraScalabilityState(float InSignificance, bool InCulled, bool InPreviousCulled)
		: Significance(InSignificance)
		, bCulled(InCulled)
		, bPreviousCulled(InPreviousCulled)
#if DEBUG_SCALABILITY_STATE
		, bCulledByDistance(0)
		, bCulledByInstanceCount(0)
		, bCulledByVisibility(0)
#endif
	{
	}

	bool IsDirty() const { return bCulled != bPreviousCulled; }

	float Significance;
	uint8 bCulled : 1;
	uint8 bPreviousCulled : 1;

#if DEBUG_SCALABILITY_STATE
	uint8 bCulledByDistance : 1;
	uint8 bCulledByInstanceCount : 1;
	uint8 bCulledByVisibility : 1;
#endif
};

struct FComponentIterationContext
{
	TArray<int32> SignificanceIndices;
	TBitArray<> ComponentRequiresUpdate;

	int32 MaxUpdateCount = 0;

	bool bNewOnly = false;
	bool bProcessAllComponents = false;
	bool bHasDirtyState = false;
	bool bRequiresGlobalSignificancePass = false;
};

USTRUCT()
struct FNiagaraScalabilityManager
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(transient)
	UNiagaraEffectType* EffectType;

	UPROPERTY(transient)
	TArray<UNiagaraComponent*>  ManagedComponents;

	TArray<FNiagaraScalabilityState> State;

	float LastUpdateTime;

	FNiagaraScalabilityManager();
	~FNiagaraScalabilityManager();
	void Update(FNiagaraWorldManager* Owner, float DeltaSeconds, bool bNewOnly);
	void Register(UNiagaraComponent* Component);
	void Unregister(UNiagaraComponent* Component);

	void AddReferencedObjects(FReferenceCollector& Collector);
	void PreGarbageCollectBeginDestroy();

#if DEBUG_SCALABILITY_STATE
	void Dump();
#endif

private: 
	void UnregisterAt(int32 IndexToRemove);
	bool HasPendingUpdates() const { return DefaultContext.ComponentRequiresUpdate.Num() > 0; }

	void UpdateInternal(FNiagaraWorldManager* WorldMan, FComponentIterationContext& Context);
	bool EvaluateCullState(FNiagaraWorldManager* WorldMan, FComponentIterationContext& Context, int32 ComponentIndex, int32& UpdateCounter);
	void ProcessSignificance(FNiagaraWorldManager* WorldMan, UNiagaraSignificanceHandler* SignificanceHandler, FComponentIterationContext& Context);
	bool ApplyScalabilityState(int32 ComponentIndex, ENiagaraCullReaction CullReaction);

	FComponentIterationContext DefaultContext;
};

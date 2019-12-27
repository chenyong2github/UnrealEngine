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
		, bDirty(0)
		, bCulled(0)
#if DEBUG_SCALABILITY_STATE
		, bCulledBySignificance(0)
		, bCulledByInstanceCount(0)
		, bCulledByVisibility(0)
		, bCulledByMaxOwnerLOD(0)
#endif
	{
	}

	float Significance;
	uint8 bDirty : 1;
	uint8 bCulled : 1;

#if DEBUG_SCALABILITY_STATE
	uint8 bCulledBySignificance : 1;
	uint8 bCulledByInstanceCount : 1;
	uint8 bCulledByVisibility : 1;
	uint8 bCulledByMaxOwnerLOD : 1;
#endif
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

	TArray<int32> SignificanceSortedIndices;

	float LastUpdateTime;

	FNiagaraScalabilityManager();
	~FNiagaraScalabilityManager();
	void Update(FNiagaraWorldManager* Owner);
	void Register(UNiagaraComponent* Component);
	void Unregister(UNiagaraComponent* Component);

	void AddReferencedObjects(FReferenceCollector& Collector);

#if DEBUG_SCALABILITY_STATE
	void Dump();
#endif

private: 
	void UnregisterAt(int32 IndexToRemove);
};

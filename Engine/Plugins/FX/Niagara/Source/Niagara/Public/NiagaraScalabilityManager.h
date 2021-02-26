// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraEffectType.h"
#include "NiagaraCommon.h"

#include "NiagaraScalabilityManager.generated.h"

class FNiagaraWorldManager;
class UNiagaraSystem;
class UNiagaraComponent;
class FReferenceCollector;

struct FComponentIterationContext
{
	TArray<int32> SignificanceIndices;
	TBitArray<> ComponentRequiresUpdate;

	int32 MaxUpdateCount = 0;
	float WorstGlobalBudgetUse = 0.0f;

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

// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "NiagaraScalabilityManager.h"
#include "NiagaraWorldManager.h"
#include "NiagaraComponent.h"

static float GScalabilityUpdateTime_Low = 1.0f;
static float GScalabilityUpdateTime_Medium = 0.5f;
static float GScalabilityUpdateTime_High = 0.25f;
static FAutoConsoleVariableRef CVarScalabilityUpdateTime_Low(TEXT("fx.NiagaraScalabilityUpdateTime_Low"), GScalabilityUpdateTime_Low, TEXT("Time in seconds between updates to scalability states for Niagara systems set to update at Low frequency. \n"), ECVF_Default);
static FAutoConsoleVariableRef CVarScalabilityUpdateTime_Medium(TEXT("fx.NiagaraScalabilityUpdateTime_Medium"), GScalabilityUpdateTime_Medium, TEXT("Time in seconds between updates to scalability states for Niagara systems set to update at Medium frequency. \n"), ECVF_Default);
static FAutoConsoleVariableRef CVarScalabilityUpdateTime_High(TEXT("fx.NiagaraScalabilityUpdateTime_High"), GScalabilityUpdateTime_High, TEXT("Time in seconds between updates to scalability states for Niagara systems set to update at High frequency. \n"), ECVF_Default);

static int32 GScalabilityManParallelThreshold = 50;
static FAutoConsoleVariableRef CVarScalabilityManParallelThreshold(TEXT("fx.ScalabilityManParallelThreshold"), GScalabilityManParallelThreshold, TEXT("Number of instances required for a niagara significance manger to go parallel for it's update. \n"), ECVF_Default);

FNiagaraScalabilityManager::FNiagaraScalabilityManager()
	: EffectType(nullptr)
	, LastUpdateTime(0.0f)
{

}

void FNiagaraScalabilityManager::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(EffectType);
	Collector.AddReferencedObjects(ManagedComponents);
}

void FNiagaraScalabilityManager::Register(UNiagaraComponent* Component)
{
	check(Component->ScalabilityManagerHandle == INDEX_NONE);
	check(ManagedComponents.Num() == State.Num());

	Component->ScalabilityManagerHandle = ManagedComponents.Add(Component);
	State.AddDefaulted();

	//UE_LOG(LogNiagara, Warning, TEXT("Registered Component %0xP at index %d"), Component, Component->ScalabilityManagerHandle);
}

void FNiagaraScalabilityManager::Unregister(UNiagaraComponent* Component)
{
	check(Component->ScalabilityManagerHandle != INDEX_NONE);
	check(ManagedComponents.Num() == State.Num());

	int32 IndexToRemove = Component->ScalabilityManagerHandle;
	Component->ScalabilityManagerHandle = INDEX_NONE;
	UnregisterAt(IndexToRemove);
}

void FNiagaraScalabilityManager::UnregisterAt(int32 IndexToRemove)
{
	//UE_LOG(LogNiagara, Warning, TEXT("Unregistering Component %0xP at index %d (Replaced with %0xP)"), ManagedComponents[IndexToRemove], IndexToRemove, ManagedComponents.Num() > 1 ? ManagedComponents.Last() : nullptr);

	ManagedComponents.RemoveAtSwap(IndexToRemove);
	State.RemoveAtSwap(IndexToRemove);

	if (ManagedComponents.IsValidIndex(IndexToRemove))
	{
		if ((ManagedComponents[IndexToRemove] != nullptr))//Possible this has been GCd. It will be removed later if so.
		{
			ManagedComponents[IndexToRemove]->ScalabilityManagerHandle = IndexToRemove;
		}
	}
}

void FNiagaraScalabilityManager::Update(FNiagaraWorldManager* WorldMan)
{
	//Paranoia code in case the EffectType is GCd from under us.
	if (EffectType == nullptr)
	{
		ManagedComponents.Empty();
		State.Empty();
		LastUpdateTime = 0.0f;
	}

	float WorldTime = WorldMan->GetWorld()->GetTimeSeconds();
	bool bShouldUpdateScalabilityStates = false;
	switch (EffectType->UpdateFrequency)
	{
	case ENiagaraScalabilityUpdateFrequency::Continuous: bShouldUpdateScalabilityStates = true; break;
	case ENiagaraScalabilityUpdateFrequency::High: bShouldUpdateScalabilityStates = WorldTime >= LastUpdateTime + GScalabilityUpdateTime_High; break;
	case ENiagaraScalabilityUpdateFrequency::Medium: bShouldUpdateScalabilityStates = WorldTime >= LastUpdateTime + GScalabilityUpdateTime_Medium; break;
	case ENiagaraScalabilityUpdateFrequency::Low: bShouldUpdateScalabilityStates = WorldTime >= LastUpdateTime + GScalabilityUpdateTime_Low; break;
	};

	if (!bShouldUpdateScalabilityStates)
	{
		return;
	}

	LastUpdateTime = WorldTime;

	//Belt and braces paranoia code to ensure we're safe if a component or System is GCd but the component isn't unregistered for whatever reason.
	int32 CompIdx = 0;
	while (CompIdx < ManagedComponents.Num())
	{
		UNiagaraComponent* Component = ManagedComponents[CompIdx];
		if (Component)
		{
			if (Component->GetAsset())
			{
				++CompIdx;
			}
			else
			{
				UE_LOG(LogNiagara, Warning, TEXT("Niagara System has been destroyed with components still registered to the scalability manager. Unregistering this component.\nComponent: %0xP - %s\nEffectType: %0xP - %s"),
					Component, *Component->GetName(), EffectType, *EffectType->GetName());
				Unregister(Component);
			}
		}
		else
		{
			UE_LOG(LogNiagara, Warning, TEXT("Niagara Component has been destroyed while still registered to the scalability manager. Unregistering this component.\nComponent: %0xP \nEffectType: %0xP - %s"),
				Component, EffectType, *EffectType->GetName());
			UnregisterAt(CompIdx);
		}
	}

	bool bNeedSortedSignificanceCull = false;
	SignificanceSortedIndices.Reset(ManagedComponents.Num());

	//TODO parallelize if we exceed GScalabilityManParallelThreshold instances.
	CompIdx = 0;
	bool bAnyDirty = false;
	for (int32 i = 0; i < ManagedComponents.Num(); ++i)
	{
		UNiagaraComponent* Component = ManagedComponents[i];
		FNiagaraScalabilityState& CompState = State[i];
#if DEBUG_SCALABILITY_STATE
		CompState.bCulledByInstanceCount = false;
		CompState.bCulledByMaxOwnerLOD = false;
		CompState.bCulledBySignificance = false;
		CompState.bCulledByVisibility = false;
#endif

		UNiagaraSystem* System = Component->GetAsset();
		const FNiagaraScalabilitySettings& ScalabilitySettings = System->GetScalabilitySettings(Component->GetPreviewDetailLevel());

		SignificanceSortedIndices.Add(i);
		bNeedSortedSignificanceCull = ScalabilitySettings.bCullMaxInstanceCount && ScalabilitySettings.MaxInstances > 0;

		WorldMan->CalculateScalabilityState(System, ScalabilitySettings, EffectType, Component, false, CompState);

		bAnyDirty |= CompState.bDirty;
	}

	if (bNeedSortedSignificanceCull)
	{
		SignificanceSortedIndices.Sort([&](int32 A, int32 B) { return State[A].Significance > State[B].Significance; });

		for (int32 i = 0; i < SignificanceSortedIndices.Num(); ++i)
		{
			int32 SortedIdx = SignificanceSortedIndices[i];
			UNiagaraComponent* Component = ManagedComponents[SortedIdx];
			FNiagaraScalabilityState& CompState = State[SortedIdx];
			UNiagaraSystem* System = Component->GetAsset();

			bool bOldCulled = CompState.bCulled;

			const FNiagaraScalabilitySettings& ScalabilitySettings = System->GetScalabilitySettings(Component->GetPreviewDetailLevel());
			WorldMan->SortedSignificanceCull(EffectType, ScalabilitySettings, CompState.Significance, i, CompState);

			CompState.bDirty |= CompState.bCulled != bOldCulled;
			bAnyDirty |= CompState.bDirty;
		}
	}

	if (bAnyDirty)
	{
		CompIdx = 0;
		//As we'll be activating and deactivating here, this must be done on the game thread.
		while (CompIdx < ManagedComponents.Num())
		{
			FNiagaraScalabilityState& CompState = State[CompIdx];
			bool bRepeatIndex = false;
			if (CompState.bDirty)
			{
				UNiagaraComponent* Component = ManagedComponents[CompIdx];
				if (CompState.bCulled)
				{
					switch (EffectType->CullReaction)
					{
					case ENiagaraCullReaction::Deactivate:					Component->DeactivateInternal(false); bRepeatIndex = true; break;//We don't increment CompIdx here as this call will remove an entry from ManagedObjects;
					case ENiagaraCullReaction::DeactivateImmediate:			Component->DeactivateImmediateInternal(false); bRepeatIndex = true;  break; //We don't increment CompIdx here as this call will remove an entry from ManagedObjects;
					case ENiagaraCullReaction::DeactivateResume:			Component->DeactivateInternal(true); break;
					case ENiagaraCullReaction::DeactivateImmediateResume:	Component->DeactivateImmediateInternal(true); break;
					};
				}
				else
				{
					if (EffectType->CullReaction == ENiagaraCullReaction::Deactivate || EffectType->CullReaction == ENiagaraCullReaction::DeactivateImmediate)
					{
						UE_LOG(LogNiagara, Error, TEXT("Niagara Component is incorrectly still registered with the scalability manager. %d - %s "), (int32)EffectType->CullReaction, *Component->GetAsset()->GetFullName());
					}
					Component->ActivateInternal(false, true);
				}

				//TODO: Beyond culling by hard limits here we could progressively scale down fx by biasing detail levels they use. Could also introduce some budgeting here like N at lvl 0, M at lvl 1 etc.
				//TODO: Possibly also limiting the rate at which their instances can tick. Ofc system sims still need to run but instances can skip ticks.
			}

			//If we are making a call that will unregister this component from the manager and remove it from ManagedComponents then we need to visit the new component that is now at this index.
			if (bRepeatIndex == false)
			{
				++CompIdx;
			}
		}
	}
}

#if DEBUG_SCALABILITY_STATE

void FNiagaraScalabilityManager::Dump()
{
	FString DetailString;

	struct FSummary
	{
		FSummary()
			: NumCulled(0)
			, NumCulledBySignificance(0)
			, NumCulledByInstanceCount(0)
			, NumCulledByVisibility(0)
			, NumCulledByMaxOwnerLOD(0)
		{}

		int32 NumCulled;
		int32 NumCulledBySignificance;
		int32 NumCulledByInstanceCount;
		int32 NumCulledByVisibility;
		int32 NumCulledByMaxOwnerLOD;
	}Summary;

	for (int32 i = 0; i < ManagedComponents.Num(); ++i)
	{
		UNiagaraComponent* Comp = ManagedComponents[i];
		FNiagaraScalabilityState& CompState = State[i];

		FString CulledStr = TEXT("Active:");
		if (CompState.bCulled)
		{
			CulledStr = TEXT("Culled:");
			++Summary.NumCulled;
		}
		if (CompState.bCulledBySignificance)
		{
			CulledStr += TEXT("-Significance-");
			++Summary.NumCulledBySignificance;
		}
		if (CompState.bCulledByInstanceCount)
		{
			CulledStr += TEXT("-Inst Count-");
			++Summary.NumCulledByInstanceCount;
		}
		if (CompState.bCulledByVisibility)
		{
			CulledStr += TEXT("-Visibility-");
			++Summary.NumCulledByVisibility;
		}
		if (CompState.bCulledByMaxOwnerLOD)
		{
			CulledStr += TEXT("-Owner LOD-");
			++Summary.NumCulledByMaxOwnerLOD;
		}

		DetailString += FString::Printf(TEXT("| %s | Sig: %2.4f | %0xP | %s | %s |\n"), *CulledStr, CompState.Significance, Comp, *Comp->GetAsset()->GetPathName(), *Comp->GetPathName());
	}

	UE_LOG(LogNiagara, Display, TEXT("-------------------------------------------------------------------------------"));
	UE_LOG(LogNiagara, Display, TEXT("Effect Type: %s"), *EffectType->GetPathName());
	UE_LOG(LogNiagara, Display, TEXT("-------------------------------------------------------------------------------"));
	UE_LOG(LogNiagara, Display, TEXT("| Summary for managed systems of this effect type. Does NOT inclued all possible Niagara FX in scene. |"));
	UE_LOG(LogNiagara, Display, TEXT("| Num Managed Components: %d |"), ManagedComponents.Num());
	UE_LOG(LogNiagara, Display, TEXT("| Num Active: %d |"), ManagedComponents.Num() - Summary.NumCulled);
	UE_LOG(LogNiagara, Display, TEXT("| Num Culled: %d |"), Summary.NumCulled);
	UE_LOG(LogNiagara, Display, TEXT("| Num Culled By Significance: %d |"), Summary.NumCulledBySignificance);
	UE_LOG(LogNiagara, Display, TEXT("| Num Culled By Instance Count: %d |"), Summary.NumCulledByInstanceCount);
	UE_LOG(LogNiagara, Display, TEXT("| Num Culled By Max Owner LOD: %d |"), Summary.NumCulledByMaxOwnerLOD);
	UE_LOG(LogNiagara, Display, TEXT("| Num Culled By Visibility: %d |"), Summary.NumCulledByVisibility);
	UE_LOG(LogNiagara, Display, TEXT("| Avg Frame GT: %d |"), EffectType->GetAverageFrameTime_GT());
	UE_LOG(LogNiagara, Display, TEXT("| Avg Frame GT + CNC: %d |"), EffectType->GetAverageFrameTime_GT_CNC());
	UE_LOG(LogNiagara, Display, TEXT("| Avg Frame RT: %d |"), EffectType->GetAverageFrameTime_RT());
	UE_LOG(LogNiagara, Display, TEXT("-------------------------------------------------------------------------------"));
	UE_LOG(LogNiagara, Display, TEXT("| Details |"));
	UE_LOG(LogNiagara, Display, TEXT("-------------------------------------------------------------------------------\n%s"), *DetailString);
}

#endif
// Copyright Epic Games, Inc. All Rights Reserved.


#include "GameplayInteractionContext.h"
#include "GameplayInteractionSmartObjectBehaviorDefinition.h"
#include "GameplayInteractionsTypes.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Subsystems/WorldSubsystem.h"
#include "StateTreeReference.h"
#include "VisualLogger/VisualLogger.h"

bool FGameplayInteractionContext::Activate(const UGameplayInteractionSmartObjectBehaviorDefinition& Definition)
{
	const FStateTreeReference& StateTreeReference = Definition.StateTreeReference;
	const UStateTree* StateTree = StateTreeReference.StateTree;

	if (!IsValid())
	{
		UE_LOG(LogGameplayInteractions, Error, TEXT("Failed to activate interaction. Context is not properly setup."));
		return false;
	}
	
	if (StateTree == nullptr)
	{
		UE_VLOG_UELOG(InteractorActor, LogGameplayInteractions, Error,
			TEXT("Failed to activate interaction for %s. Definition %s doesn't point to a valid StateTree asset."),
			*GetNameSafe(InteractorActor),
			*Definition.GetFullName());
		return false;
	}
	
	if (!StateTreeContext.Init(*InteractorActor, *StateTree, EStateTreeStorage::Internal))
	{
		UE_VLOG_UELOG(InteractorActor, LogGameplayInteractions, Error,
			TEXT("Failed to activate interaction for %s. Unable to initialize StateTree execution context for StateTree asset: %s."),
			*GetNameSafe(InteractorActor),
			*StateTree->GetFullName());
		return false;
	}

	if (!SetContextRequirements())
	{
		UE_VLOG_UELOG(InteractorActor, LogGameplayInteractions, Error,
			TEXT("Failed to activate interaction for %s. Unable to provide all external data views for StateTree asset: %s."),
			*GetNameSafe(InteractorActor),
			*StateTree->GetFullName());
		return false;
	}

	StateTreeContext.SetParameters(StateTreeReference.Parameters);
	StateTreeContext.Start();
	return true;
}

bool FGameplayInteractionContext::Tick(const float DeltaTime)
{
	EStateTreeRunStatus RunStatus = EStateTreeRunStatus::Unset;
	if (SetContextRequirements())
	{
		RunStatus = StateTreeContext.Tick(DeltaTime);
	}

	return RunStatus == EStateTreeRunStatus::Running;
}

void FGameplayInteractionContext::Deactivate()
{
	if (SetContextRequirements())
	{
		StateTreeContext.Stop();
	}
}

bool FGameplayInteractionContext::SetContextRequirements()
{
	if (!StateTreeContext.IsValid())
	{
		return false;
	}
	
	for (const FStateTreeExternalDataDesc& ItemDesc : StateTreeContext.GetNamedExternalDataDescs())
	{
		if (ItemDesc.Name == UE::GameplayInteraction::Names::InteractableActor)
		{
			StateTreeContext.SetExternalData(ItemDesc.Handle, FStateTreeDataView(InteractableActor));
		}
		else if (ItemDesc.Name == UE::GameplayInteraction::Names::SmartObjectClaimedHandle)
        {
            StateTreeContext.SetExternalData(ItemDesc.Handle, FStateTreeDataView(FSmartObjectClaimHandle::StaticStruct(), (uint8*)&ClaimedHandle));
        }
		else if (ItemDesc.Name == UE::GameplayInteraction::Names::AbortContext)
		{
			StateTreeContext.SetExternalData(ItemDesc.Handle, FStateTreeDataView(FGameplayInteractionAbortContext::StaticStruct(), (uint8*)&AbortContext));
		}
	}

	checkf(InteractorActor != nullptr, TEXT("Should never reach this point with an invalid InteractorActor since it is required to get a valid StateTreeContext."));
	const UWorld* World = InteractorActor->GetWorld();
	for (const FStateTreeExternalDataDesc& ItemDesc : StateTreeContext.GetExternalDataDescs())
	{
		if (ItemDesc.Struct != nullptr)
		{
			if (World != nullptr && ItemDesc.Struct->IsChildOf(UWorldSubsystem::StaticClass()))
			{
				UWorldSubsystem* Subsystem = World->GetSubsystemBase(Cast<UClass>(const_cast<UStruct*>(ToRawPtr(ItemDesc.Struct))));
				StateTreeContext.SetExternalData(ItemDesc.Handle, FStateTreeDataView(Subsystem));
			}
			else if (ItemDesc.Struct->IsChildOf(AActor::StaticClass()))
			{
				StateTreeContext.SetExternalData(ItemDesc.Handle, FStateTreeDataView(InteractorActor));
			}
		}
	}

	return StateTreeContext.AreExternalDataViewsValid();
}
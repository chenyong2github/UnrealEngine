// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameplayBehaviorManager.h"
#include "GameplayBehaviorConfig.h"
#include "DefaultManagerInstanceTracker.h"

namespace
{
	typedef TDefaultManagerInstanceTracker<UGameplayBehaviorManager> FGameplayBehaviorsManagerInstanceTracker;
	FGameplayBehaviorsManagerInstanceTracker InstanceTracker;
}

UGameplayBehaviorManager::FInstanceGetterSignature UGameplayBehaviorManager::InstanceGetterDelegate = UGameplayBehaviorManager::FInstanceGetterSignature::CreateRaw(&InstanceTracker, &FGameplayBehaviorsManagerInstanceTracker::GetManagerInstance);

UGameplayBehaviorManager::UGameplayBehaviorManager(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bCreateIfMissing = true;
}

void UGameplayBehaviorManager::PostInitProperties()
{
	Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		InstanceTracker.bCreateIfMissing = bCreateIfMissing;
	}
}

bool UGameplayBehaviorManager::StopBehavior(AActor& Avatar, TSubclassOf<UGameplayBehavior> BehaviorToStop)
{
	FAgentGameplayBehaviors* AgentData = AgentGameplayBehaviors.Find(&Avatar);

	if (AgentData)
	{
		for (int32 Index = AgentData->Behaviors.Num() - 1; Index >= 0; --Index)
		{
			UGameplayBehavior* Beh = AgentData->Behaviors[Index];
			// @todo make sure we're aware of this in UGameplayBehaviorManager::OnBehaviorFinished
			if (Beh && (!BehaviorToStop || Beh->IsA(BehaviorToStop)))
			{
				Beh->EndBehavior(Avatar, /*bInterrupted=*/true);
			}
		}
	}

	return false;
}

void UGameplayBehaviorManager::OnBehaviorFinished(UGameplayBehavior& Behavior, AActor& Avatar, const bool bInterrupted)
{
	if (IsShuttingDown())
	{
		// ignore if we're shutting down
		return;
	}

	FAgentGameplayBehaviors* AgentData = AgentGameplayBehaviors.Find(&Avatar);

	if (AgentData)
	{
		const int32 BehaviorIndex = AgentData->Behaviors.Find(&Behavior);
		if (BehaviorIndex != INDEX_NONE)
		{
			Behavior.GetOnBehaviorFinishedDelegate().RemoveAll(this);
			AgentData->Behaviors.RemoveAtSwap(BehaviorIndex, 1, /*bAllowShrinking=*/false);
		}
	}
}

bool UGameplayBehaviorManager::TriggerBehavior(const UGameplayBehaviorConfig& Config, AActor& Avatar, AActor* SmartObjectOwner/* = nullptr*/)
{
	UWorld* World = Avatar.GetWorld();
	UGameplayBehavior* BehaviorRun = World ? Config.GetBehavior(*World) : nullptr;
	return BehaviorRun != nullptr
		&& UGameplayBehaviorManager::TriggerBehavior(*BehaviorRun, Avatar, &Config, SmartObjectOwner);
}

bool UGameplayBehaviorManager::TriggerBehavior(UGameplayBehavior& Behavior, AActor& Avatar, const UGameplayBehaviorConfig* Config, AActor* SmartObjectOwner/* = nullptr*/)
{
	UWorld* World = Avatar.GetWorld();
	if (World)
	{
		UGameplayBehaviorManager* ManagerInstance = InstanceGetterDelegate.Execute(*World);
		return ManagerInstance != nullptr
			&& ManagerInstance->TriggerBehaviorImpl(Behavior, Avatar, Config, SmartObjectOwner);
	}
	return false;
}

bool UGameplayBehaviorManager::TriggerBehaviorImpl(UGameplayBehavior& Behavior, AActor& Avatar, const UGameplayBehaviorConfig* Config, AActor* SmartObjectOwner/* = nullptr*/)
{
	if (Behavior.Trigger(Avatar, Config, SmartObjectOwner))
	{
		Behavior.GetOnBehaviorFinishedDelegate().AddUObject(this, &UGameplayBehaviorManager::OnBehaviorFinished);
		
		FAgentGameplayBehaviors& AgentData = AgentGameplayBehaviors.FindOrAdd(&Avatar);
		AgentData.Behaviors.Add(&Behavior);

		return true;
	}
	return false;
}

UGameplayBehaviorManager* UGameplayBehaviorManager::GetCurrent(UWorld* World)
{
	return World ? InstanceGetterDelegate.Execute(*World) : nullptr;
}

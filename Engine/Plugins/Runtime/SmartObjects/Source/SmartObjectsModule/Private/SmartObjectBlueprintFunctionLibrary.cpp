// Copyright Epic Games, Inc. All Rights Reserved.

#include "SmartObjectBlueprintFunctionLibrary.h"
#include "SmartObjectSubsystem.h"
#include "BlackboardKeyType_SOClaimHandle.h"
#include "Blueprint/AIBlueprintHelperLibrary.h"
#include "AI/AITask_UseSmartObject.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "BehaviorTree/BlackboardComponent.h"

//----------------------------------------------------------------------//
// USmartObjectBlueprintFunctionLibrary 
//----------------------------------------------------------------------//
FSmartObjectClaimHandle USmartObjectBlueprintFunctionLibrary::GetValueAsSOClaimHandle(UBlackboardComponent* BlackboardComponent, const FName& KeyName)
{
	if (BlackboardComponent == nullptr)
	{
		return {};
	}
	return BlackboardComponent->GetValue<UBlackboardKeyType_SOClaimHandle>(KeyName);
}

void USmartObjectBlueprintFunctionLibrary::SetValueAsSOClaimHandle(UBlackboardComponent* BlackboardComponent, const FName& KeyName, const FSmartObjectClaimHandle Value)
{
	if (BlackboardComponent == nullptr)
	{
		return;
	}
	const FBlackboard::FKey KeyID = BlackboardComponent->GetKeyID(KeyName);
	BlackboardComponent->SetValue<UBlackboardKeyType_SOClaimHandle>(KeyID, Value);
}

bool USmartObjectBlueprintFunctionLibrary::K2_UseSmartObject(AActor* Avatar, AActor* SmartObject)
{
	if (Avatar == nullptr || SmartObject == nullptr)
	{
		return false;
	}

	AAIController* AIController = UAIBlueprintHelperLibrary::GetAIController(Avatar);
	if (AIController != nullptr)
	{
		UAITask_UseSmartObject* Task = UAITask_UseSmartObject::UseSmartObject(AIController, SmartObject, nullptr);
		if (Task != nullptr)
		{
			Task->ReadyForActivation();
		}
		return Task != nullptr;
	}

	return false;
}

bool USmartObjectBlueprintFunctionLibrary::K2_SetSmartObjectEnabled(AActor* SmartObject, const bool bEnabled)
{
	if (SmartObject == nullptr)
	{
		return false;
	}

	UWorld* World = SmartObject->GetWorld();
	if (World == nullptr)
	{
		return false;
	}

	USmartObjectSubsystem* Subsystem = USmartObjectSubsystem::GetCurrent(World);
	if (Subsystem == nullptr)
	{
		return false;
	}

	return bEnabled ? Subsystem->RegisterSmartObjectActor(*SmartObject)
		: Subsystem->UnregisterSmartObjectActor(*SmartObject);
}

//----------------------------------------------------------------------//
// DEPRECATED 
//----------------------------------------------------------------------//
bool USmartObjectBlueprintFunctionLibrary::K2_AddLooseGameplayTags(AActor* Actor, const FGameplayTagContainer& GameplayTags)
{
	return UAbilitySystemBlueprintLibrary::AddLooseGameplayTags(Actor, GameplayTags);
}

bool USmartObjectBlueprintFunctionLibrary::K2_RemoveLooseGameplayTags(AActor* Actor, const FGameplayTagContainer& GameplayTags)
{
	return UAbilitySystemBlueprintLibrary::RemoveLooseGameplayTags(Actor, GameplayTags);
}

// Copyright Epic Games, Inc. All Rights Reserved.

#include "AI/GameplayBehavior_BehaviorTree.h"
#include "AI/GameplayBehaviorConfig_BehaviorTree.h"
#include "VisualLogger/VisualLogger.h"
#include "Blueprint/AIBlueprintHelperLibrary.h"
#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"

//----------------------------------------------------------------------//
// UGameplayBehavior_BehaviorTree
//----------------------------------------------------------------------//
UGameplayBehavior_BehaviorTree::UGameplayBehavior_BehaviorTree(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InstantiationPolicy = EGameplayBehaviorInstantiationPolicy::ConditionallyInstantiate;
}

bool UGameplayBehavior_BehaviorTree::Trigger(AActor& InAvatar, const UGameplayBehaviorConfig* Config /* = nullptr*/, AActor* SmartObjectOwner /* = nullptr*/)
{
	const UGameplayBehaviorConfig_BehaviorTree* BTConfig = Cast<const UGameplayBehaviorConfig_BehaviorTree>(Config);
	if (BTConfig == nullptr || BTConfig->GetBehaviorTree() == nullptr)
	{
		UE_VLOG(&InAvatar, LogGameplayBehavior, Warning, TEXT("Failed to trigger behavior %s for %s due to %s being null")
			, *GetName(), *InAvatar.GetName(), BTConfig ? TEXT("Config->BehaviorTree") : TEXT("Config"));
		return false;
	}

	// note that the value stored in this property is unreliable if we're in the CDO
	// If you need this to be reliable set InstantiationPolicy to Instantiate
	AIController = UAIBlueprintHelperLibrary::GetAIController(&InAvatar);
	if (AIController == nullptr)
	{
		UE_VLOG(&InAvatar, LogGameplayBehavior, Warning, TEXT("Failed to trigger behavior %s due to %s not being AI-controlled")
			, *GetName(), *InAvatar.GetName());
		return false;
	}

	if (BTConfig->ShouldStorePreviousBT())
	{
		UBehaviorTreeComponent* BTComp = Cast<UBehaviorTreeComponent>(AIController->GetBrainComponent());
		PreviousBT = BTComp ? BTComp->GetRootTree() : nullptr;
	}

	const bool bResult = AIController->RunBehaviorTree(BTConfig->GetBehaviorTree());

	UE_CVLOG(bResult == false, &InAvatar, LogGameplayBehavior, Warning, TEXT("Failed to run behavior tree %s on %s; %s")
		, *BTConfig->GetBehaviorTree()->GetName(), *InAvatar.GetName(), *AIController->GetName());

	return bResult;
}

void UGameplayBehavior_BehaviorTree::EndBehavior(AActor& InAvatar, const bool bInterrupted)
{
	Super::EndBehavior(InAvatar, bInterrupted);

	if (PreviousBT && AIController)
	{
		AIController->RunBehaviorTree(PreviousBT);
	}
}

bool UGameplayBehavior_BehaviorTree::NeedsInstance(const UGameplayBehaviorConfig* Config) const
{
	const UGameplayBehaviorConfig_BehaviorTree* BTConfig = Cast<const UGameplayBehaviorConfig_BehaviorTree>(Config);
	return BTConfig && BTConfig->ShouldStorePreviousBT();
}

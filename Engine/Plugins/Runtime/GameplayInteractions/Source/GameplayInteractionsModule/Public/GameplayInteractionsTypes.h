// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StateTreeTaskBase.h"
#include "GameplayInteractionsTypes.generated.h"

GAMEPLAYINTERACTIONSMODULE_API DECLARE_LOG_CATEGORY_EXTERN(LogGameplayInteractions, Warning, All);

namespace UE::GameplayInteraction::Names
{
	const FName InteractableActor = TEXT("InteractableActor");
	const FName SmartObjectClaimedHandle = TEXT("SmartObjectClaimedHandle");
};

/**
 * Base class (namespace) for all StateTree Tasks related to AI/Gameplay.
 * This allows schemas to safely include all tasks child of this struct. 
 */
USTRUCT(meta = (Hidden))
struct GAMEPLAYINTERACTIONSMODULE_API FGameplayInteractionStateTreeTask : public FStateTreeTaskBase
{
	GENERATED_BODY()

};

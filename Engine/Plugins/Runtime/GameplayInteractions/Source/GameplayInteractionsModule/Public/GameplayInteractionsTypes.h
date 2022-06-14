// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StateTreeTaskBase.h"
#include "GameplayInteractionsTypes.generated.h"

GAMEPLAYINTERACTIONSMODULE_API DECLARE_LOG_CATEGORY_EXTERN(LogGameplayInteractions, Warning, All);

namespace UE::GameplayInteraction::Names
{
	const FName InteractableActor = TEXT("InteractableActor");
	const FName SmartObjectClaimedHandle = TEXT("SmartObjectClaimedHandle");
	const FName AbortContext = TEXT("AbortContext");
};

/** Reason why the interaction is ended prematurely. */
UENUM(BlueprintType)
enum class EGameplayInteractionAbortReason : uint8
{
	Unset,
	ExternalAbort,
	InternalAbort,	// Internal failure from slot invalidation (e.g. slot unregistered, destroyed)
};

/**
 * Struct holding data related to the abort action  
 */
USTRUCT(BlueprintType)
struct GAMEPLAYINTERACTIONSMODULE_API FGameplayInteractionAbortContext
{
	GENERATED_BODY()

	FGameplayInteractionAbortContext() = default;
	explicit FGameplayInteractionAbortContext(const EGameplayInteractionAbortReason& InReason) : Reason(InReason) {}
	
	UPROPERTY()
	EGameplayInteractionAbortReason Reason = EGameplayInteractionAbortReason::Unset;
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

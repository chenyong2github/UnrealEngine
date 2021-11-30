// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StateTreeEvaluatorBase.h" 
#include "StateTreeTaskBase.h"
#include "MassStateTreeTypes.generated.h"

/**
 * Signals used by the StateTree framework in Mass
 */
namespace UE::Mass::Signals
{
	const FName StateTreeActivate = FName(TEXT("StateTreeActivate"));
	const FName LookAtFinished = FName(TEXT("LookAtFinished"));
	const FName NewStateTreeTaskRequired = FName(TEXT("NewStateTreeTaskRequired"));
	const FName StandTaskFinished = FName(TEXT("StandTaskFinished"));
	const FName DelayedTransitionWakeup = FName(TEXT("DelayedTransitionWakeup"));
	// @todo MassStateTree: move this to its game plugin when possible
	const FName ContextualAnimTaskFinished = FName(TEXT("ContextualAnimTaskFinished"));
}

/**
 * Base struct for all Mass StateTree Evaluators.
 */
USTRUCT(meta = (DisplayName = "Mass Evaluator Base"))
struct MASSAIBEHAVIOR_API FMassStateTreeEvaluatorBase : public FStateTreeEvaluatorBase
{
	GENERATED_BODY()
};

/**
 * Base struct for all Mass StateTree Tasks.
 */
USTRUCT(meta = (DisplayName = "Mass Task Base"))
struct MASSAIBEHAVIOR_API FMassStateTreeTaskBase : public FStateTreeTaskBase
{
	GENERATED_BODY()
};

/**
* A handle pointing to a registered StateTree asset in UMassStateTreeSubsystem.
*/
struct FMassStateTreeHandle
{
	static constexpr uint16 Invalid = MAX_uint16;

	FMassStateTreeHandle() = default;

	/** Initializes new handle based on an index */
	static FMassStateTreeHandle Make(const uint16 InIndex) { return FMassStateTreeHandle(InIndex); }
	
	/** Returns index the handle points to */
	uint16 GetIndex() const { return Index; }

protected:
	FMassStateTreeHandle(const uint16 InIndex) : Index(InIndex) {}

	uint16 Index = Invalid;
};


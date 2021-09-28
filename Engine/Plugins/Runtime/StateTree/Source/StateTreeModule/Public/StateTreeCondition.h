// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AITypes.h"
#include "StateTreeVariable.h"
#include "StateTreeCondition.generated.h"

/**
 * Condition contains left and right variable references and comparison operator.
 * The variable chosen left will dictate which type is available at right.
 * This is done in the editor code, see StateTreeConditionDetails.
*/
USTRUCT()
struct STATETREEMODULE_API FStateTreeCondition
{
	GENERATED_BODY()
public:
	FStateTreeCondition();

#if WITH_EDITOR
	FText GetDescription() const;
#endif

	UPROPERTY(EditDefaultsOnly, Category = Condition)
	FStateTreeVariable Left;

	UPROPERTY(EditDefaultsOnly, Category = Condition)
	FStateTreeVariable Right;

	UPROPERTY(EditDefaultsOnly, Category = Condition)
	EGenericAICheck Operator;
};

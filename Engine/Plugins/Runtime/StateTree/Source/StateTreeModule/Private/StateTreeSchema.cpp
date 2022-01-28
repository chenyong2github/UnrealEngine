// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeSchema.h"
#include "CoreMinimal.h"
#include "Blueprint/StateTreeEvaluatorBlueprintBase.h"
#include "Blueprint/StateTreeTaskBlueprintBase.h"
#include "Blueprint/StateTreeConditionBlueprintBase.h"

bool UStateTreeSchema::IsChildOfBlueprintBase(const UClass* InClass) const
{
	return InClass->IsChildOf<UStateTreeItemBlueprintBase>();
}

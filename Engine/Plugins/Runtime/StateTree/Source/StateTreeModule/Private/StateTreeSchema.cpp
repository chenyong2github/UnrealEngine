// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeSchema.h"
#include "Blueprint/StateTreeNodeBlueprintBase.h"

bool UStateTreeSchema::IsChildOfBlueprintBase(const UClass* InClass) const
{
	return InClass->IsChildOf<UStateTreeNodeBlueprintBase>();
}

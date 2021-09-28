// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Interface.h"
#include "StateTreeVariableDesc.h"
#include "StateTreeVariableProvider.generated.h"

struct FStateTreeVariableLayout;

/**
 * Variable provider is used by the UI (see StateTreeVariableBindingDetails) to get variables that are visible for a StateTreeVariableBinding property.
 * The UI uses outer objects to find the nearest provider.
*/
UINTERFACE(Blueprintable, MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UStateTreeVariableProvider : public UInterface
{
	GENERATED_UINTERFACE_BODY()
};

class STATETREEEDITORMODULE_API IStateTreeVariableProvider
{
	GENERATED_IINTERFACE_BODY()

	virtual void GetVisibleVariables(FStateTreeVariableLayout& Variables) const PURE_VIRTUAL(IStateTreeVariableProvider::GetVisibleVariableBlocks, return; );
};


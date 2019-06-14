// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/**
 * Base class of any editor-only objects
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "UObject/ScriptMacros.h"
#include "EditorUtilityObject.generated.h"


UCLASS(Abstract, Blueprintable, meta = (ShowWorldContextPin))
class BLUTILITY_API UEditorUtilityObject : public UObject
{
	GENERATED_UCLASS_BODY()

	// Standard function to execute
	UFUNCTION(BlueprintCallable, BlueprintImplementableEvent, Category = "Editor")
	void Run();
};

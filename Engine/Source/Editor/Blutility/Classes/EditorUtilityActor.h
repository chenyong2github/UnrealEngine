// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * Base class of any editor-only actors
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UObject/ScriptMacros.h"
#include "EditorUtilityActor.generated.h"


UCLASS(Abstract, Blueprintable, meta = (ShowWorldContextPin))
class BLUTILITY_API AEditorUtilityActor : public AActor
{
	GENERATED_UCLASS_BODY()

	// Standard function to execute
	UFUNCTION(BlueprintCallable, BlueprintImplementableEvent, Category = "Editor")
	void Run();
};

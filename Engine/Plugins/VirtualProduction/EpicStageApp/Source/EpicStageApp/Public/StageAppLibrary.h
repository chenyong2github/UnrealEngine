// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "StageAppLibrary.generated.h"

/**
 * Generally useful Blueprint/remote functions for Epic Stage App integration.
 */
UCLASS()
class EPICSTAGEAPP_API UStageAppFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/** The current semantic version for the stage app API as a formatted string. */
	UFUNCTION(BlueprintPure, Category = "Development", meta = (BlueprintThreadSafe))
	static FString GetAPIVersion();
};

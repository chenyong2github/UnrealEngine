// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "USDTestsBlueprintLibrary.generated.h"

/**
 * Library of functions that can be used via Python scripting to help testing the other USD functionality
 */
UCLASS(meta=(ScriptName="USDTestingLibrary"))
class USDTESTS_API USDTestsBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/**
	 * Helps test the effects of blueprint recompilation on the spawned actors and assets when a stage is opened.
	 * Returns whether it compiled successfully or not.
	 */
	UFUNCTION( BlueprintCallable, Category = "Blueprint" )
	static bool RecompileBlueprintStageActor( AUsdStageActor* BlueprintDerivedStageActor );

	/**
	 * Intentionally dirties the UBlueprint for the given stage actor's generated class.
	 * This is useful for testing how the stage actor behaves when going into PIE with a dirty blueprint, as that usually triggers
	 * a recompile at the very sensitive PIE transition
	 */
	UFUNCTION( BlueprintCallable, Category = "Blueprint" )
	static void DirtyStageActorBlueprint( AUsdStageActor* BlueprintDerivedStageActor );
};

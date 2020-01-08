// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Blueprints/IOutputRemapBlueprintAPI.h"

#include "OutputRemapBlueprintLib.generated.h"


/**
 * Blueprint API function library
 */
UCLASS()
class UOutputRemapIBlueprintLib
	: public UBlueprintFunctionLibrary
{
	GENERATED_UCLASS_BODY()

public:
	/** Return Display Cluster API interface. */
	UFUNCTION(BlueprintPure, meta = (DisplayName = "OutputRemap Module API"), Category = "nDisplay")
	static void GetAPI(TScriptInterface<IOutputRemapBlueprintAPI>& OutAPI);
};

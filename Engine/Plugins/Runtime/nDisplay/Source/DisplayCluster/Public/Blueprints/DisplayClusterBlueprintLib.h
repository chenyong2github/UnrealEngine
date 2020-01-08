// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Blueprints/IDisplayClusterBlueprintAPI.h"
#include "DisplayClusterBlueprintLib.generated.h"


/**
 * Blueprint API function library
 */
UCLASS()
class UDisplayClusterBlueprintLib
	: public UBlueprintFunctionLibrary
{
	GENERATED_UCLASS_BODY()

public:
	/** Return Display Cluster API interface. */
	UFUNCTION(BlueprintPure, meta = (DisplayName = "DisplayCluster Module API"), Category = "nDisplay")
	static void GetAPI(TScriptInterface<IDisplayClusterBlueprintAPI>& OutAPI);
};

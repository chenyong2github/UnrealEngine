// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Blueprints/IMPCDIBlueprintAPI.h"

#include "MPCDIBlueprintLib.generated.h"


/**
 * Blueprint API function library
 */
UCLASS()
class UMPCDIBlueprintLib
	: public UBlueprintFunctionLibrary
{
	GENERATED_UCLASS_BODY()

public:
	/** Return Display Cluster API interface. */
	UFUNCTION(BlueprintPure, meta = (DisplayName = "MPCDI Module API"), Category = "nDisplay")
	static void GetAPI(TScriptInterface<IMPCDIBlueprintAPI>& OutAPI);
};

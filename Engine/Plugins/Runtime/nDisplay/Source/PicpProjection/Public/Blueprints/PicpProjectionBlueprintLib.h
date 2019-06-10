// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Blueprints/IPicpProjectionBlueprintAPI.h"

#include "PicpProjectionBlueprintLib.generated.h"


/**
 * Blueprint API function library
 */
UCLASS()
class UPicpProjectionIBlueprintLib
	: public UBlueprintFunctionLibrary
{
	GENERATED_UCLASS_BODY()

public:
	/** Return Display Cluster API interface. */
	UFUNCTION(BlueprintPure, meta = (DisplayName = "PICP Module API"), Category = "nDisplay")
	static void GetAPI(TScriptInterface<IPicpProjectionBlueprintAPI>& OutAPI);
};

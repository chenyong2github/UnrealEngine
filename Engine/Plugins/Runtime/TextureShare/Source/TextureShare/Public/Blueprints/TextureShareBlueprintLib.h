// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Blueprints/ITextureShareBlueprintAPI.h"
#include "TextureShareBlueprintLib.generated.h"

/**
 * Blueprint API function library
 */
UCLASS()
class UTextureShareIBlueprintLib
	: public UBlueprintFunctionLibrary
{
	GENERATED_UCLASS_BODY()

public:
	/** Return Display Cluster API interface. */
	UFUNCTION(BlueprintPure, meta = (DisplayName = "TextureShare API"), Category = "TextureShare")
	static void GetAPI(TScriptInterface<ITextureShareBlueprintAPI>& OutAPI);
};

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"

#include "LocalizableMessageLibrary.generated.h"

struct FLocalizableMessage;
class FText;

/** BlueprintFunctionLibrary for LocalizationMessage */
UCLASS(Abstract, MinimalAPI, BlueprintInternalUseOnly)
class ULocalizableMessageLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Conversion function from LocalizationMessage to FText.
	 * @note Is only valid on the client. The processor generate a 
	 */
	UFUNCTION(BlueprintPure, BlueprintCosmetic, Category = "Localization Message", meta = (WorldContext = "WorldContextObject", BlueprintInternalUseOnly = "true"))
	static LOCALIZABLEMESSAGEBLUEPRINT_API FText Conv_LocalizableMessageToText(UObject* WorldContextObject, UPARAM(ref) const FLocalizableMessage& Message);
};


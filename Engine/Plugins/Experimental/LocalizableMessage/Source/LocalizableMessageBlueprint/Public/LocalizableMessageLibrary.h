// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "LocalizableMessage.h"

#include "LocalizableMessageLibrary.generated.h"

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
	UFUNCTION(BlueprintPure, BlueprintCosmetic, CustomThunk, Category = "Localization Message", meta = (CustomStructureParam = "Message", WorldContext = "WorldContextObject", BlueprintInternalUseOnly = "true"))
	static LOCALIZABLEMESSAGEBLUEPRINT_API FText Conv_LocalizableMessageToText(const UObject* WorldContextObject, UPARAM(ref) const int32& Message);

private:
	DECLARE_FUNCTION(execConv_LocalizableMessageToText);
};


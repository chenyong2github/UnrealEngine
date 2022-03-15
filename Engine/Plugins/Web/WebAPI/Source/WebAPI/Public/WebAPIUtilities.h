// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "WebAPIMessageResponse.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "WebAPIUtilities.generated.h"

/**
 * 
 */
UCLASS()
class WEBAPI_API UWebAPIUtilities : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, BlueprintPure, Category="WebAPI", meta=(ScriptMethod))
	static const FText& GetResponseMessage(const FWebAPIMessageResponse& MessageResponse);
};

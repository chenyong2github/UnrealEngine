// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "NetPushModelHelpers.generated.h"

UCLASS()
class ENGINE_API UNetPushModelHelpers : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintCallable, Category="Net")
	static void MarkPropertyDirty(UObject* Object, FName PropertyName);

private:

	UFUNCTION(BlueprintCallable, Category = "Net", Meta=(BlueprintInternalUseOnly = "true", HidePin = "Object|RepIndex|PropertyName"))
	static void MarkPropertyDirtyFromRepIndex(UObject* Object, int32 RepIndex, FName PropertyName);
};
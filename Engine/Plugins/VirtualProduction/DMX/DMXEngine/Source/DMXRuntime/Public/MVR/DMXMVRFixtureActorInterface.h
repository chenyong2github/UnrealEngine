// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Interface.h"

#include "DMXMVRFixtureActorInterface.generated.h"

class UDMXEntityFixturePatch;


/** 
 * When implemented in an actor, MVR will find the Actor and consider it when auto-selecting Fixtures.
 */
UINTERFACE(BlueprintType)
class DMXRUNTIME_API UDMXMVRFixtureActorInterface
	: public UInterface
{
	GENERATED_BODY()

};

class DMXRUNTIME_API IDMXMVRFixtureActorInterface
{
	GENERATED_BODY()

public:
	/** Should return the DMX Attributes the Actor supports */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DMX", Meta = (DisplayName = "On MVR Get Supported DMX Attributes"))
	void OnMVRGetSupportedDMXAttributes(TArray<FName>& OutAttributeNames, TArray<FName>& OutMatrixAttributeNames) const;

	/** Should set the Fixture Patch the MVR Fixture uses */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, CallInEditor, Category = "DMX", Meta = (DisplayName = "On MVR Set Fixture Patch"))
	void OnMVRSetFixturePatch(UDMXEntityFixturePatch* FixturePatch);
};

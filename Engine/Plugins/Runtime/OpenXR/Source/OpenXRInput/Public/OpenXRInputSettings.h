// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "OpenXRInputSettings.generated.h"

/**
* Implements the settings for the OpenXR Input plugin.
*/
UCLASS(config = Input, defaultconfig)
class OPENXRINPUT_API UOpenXRInputSettings : public UObject
{
	GENERATED_UCLASS_BODY()

public:
	/** Use Enhanced Input for XR instead of regular input. */
	UPROPERTY(config, EditAnywhere, Category = "Enhanced Input")
	bool bUseEnhancedInput = false;

	/** A the mappable input config used to generate action sets for OpenXR. */
	UPROPERTY(config, EditAnywhere, Category = "Enhanced Input", meta = (DisplayName = "Mappable Input Config for XR", AllowedClasses = "PlayerMappableInputConfig"))
	FSoftObjectPath MappableInputConfig = nullptr;
};

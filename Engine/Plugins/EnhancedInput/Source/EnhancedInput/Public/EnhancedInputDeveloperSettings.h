// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettingsBackedByCVars.h"
#include "Engine/PlatformSettings.h"
#include "EnhancedInputDeveloperSettings.generated.h"

/** Developer settings for Enhanced Input */
UCLASS(config = Input, defaultconfig, meta = (DisplayName = "Enhanced Input"))
class ENHANCEDINPUT_API UEnhancedInputDeveloperSettings : public UDeveloperSettingsBackedByCVars
{
	GENERATED_BODY()
public:
	
	UEnhancedInputDeveloperSettings(const FObjectInitializer& Initializer);

	/**
	 * Platform specific settings for Enhanced Input.
	 * @see UEnhancedInputPlatformSettings
	 */
	UPROPERTY(EditAnywhere, Category = "Input")
	FPerPlatformSettings PlatformSettings;
};
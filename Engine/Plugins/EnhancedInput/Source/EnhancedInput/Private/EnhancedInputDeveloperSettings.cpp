// Copyright Epic Games, Inc. All Rights Reserved.

#include "EnhancedInputDeveloperSettings.h"
#include "EnhancedInputPlatformSettings.h"

UEnhancedInputDeveloperSettings::UEnhancedInputDeveloperSettings(const FObjectInitializer& Initializer)
	: Super(Initializer)
{
	PlatformSettings.Initialize(UEnhancedInputPlatformSettings::StaticClass());
}
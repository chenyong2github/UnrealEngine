// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "Engine/DeveloperSettings.h"
#include "UObject/ObjectMacros.h"
#include "UObject/SoftObjectPath.h"

#include "AudioModulationSettings.generated.h"


UCLASS(config=AudioModulation, defaultconfig, meta = (DisplayName = "Audio Modulation"))
class AUDIOMODULATION_API UAudioModulationSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	// Array of loaded Modulation Parameters
	UPROPERTY(config, EditAnywhere, Category = "Parameters", meta = (AllowedClasses = "SoundModulationParameter"))
	TArray<FSoftObjectPath> Parameters;
};
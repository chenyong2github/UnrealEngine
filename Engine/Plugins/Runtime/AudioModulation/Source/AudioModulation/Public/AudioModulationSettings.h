// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "Engine/DeveloperSettings.h"
#include "UObject/ObjectMacros.h"
#include "UObject/SoftObjectPath.h"

#include "AudioModulationSettings.generated.h"


UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Sound Modulation"))
class AUDIOMODULATION_API UAudioModulationSettings : public UDeveloperSettings
{
	GENERATED_UCLASS_BODY()

#if WITH_EDITORONLY_DATA
	// Names of all controls provided for modulation beyond standard types (Volume, Pitch, HPF, LPF)
	// Properties hidden as Generic Control Modulation is still in development
	// UPROPERTY(config, EditAnywhere, Category = "Controls")
	UPROPERTY()
	TArray<FName> ControlNames;
#endif // WITH_EDITORONLY_DATA
};
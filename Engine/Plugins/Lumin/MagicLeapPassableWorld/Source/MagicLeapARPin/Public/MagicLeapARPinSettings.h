// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "MagicLeapARPinTypes.h"
#include "MagicLeapARPinSettings.generated.h"

/**

* Implements the settings for the Magic Leap AR Pin.

*/

UCLASS(config=Engine, defaultconfig)
class MAGICLEAPARPIN_API UMagicLeapARPinSettings : public UObject
{
	GENERATED_BODY()

public:
	UMagicLeapARPinSettings();

	/** Time (in seconds) to check for updates in ARPins (Lumin-only). Set 0 to check every frame. */
	UPROPERTY(GlobalConfig, EditAnywhere, Category = "ARPin")
	float UpdateCheckFrequency;

	/** What should be the delta of the ARPin state properties to trigger an OnUpdated event for that pin. A pin will be considered "updated" if at least one of it's state property deltas are above the specified thresholds. */
	UPROPERTY(GlobalConfig, EditAnywhere, Category = "ARPin")
	FMagicLeapARPinState OnUpdatedEventTriggerDelta;
};


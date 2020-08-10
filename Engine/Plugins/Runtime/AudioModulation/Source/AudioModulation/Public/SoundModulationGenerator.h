// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "DSP/LFO.h"
#include "IAudioModulation.h"
#include "SoundModulationValue.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"

#include "SoundModulationGenerator.generated.h"


/**
 * Base class for modulators that algoithmically generate values that can effect
 * various endpoints (ex. Control Buses & Parameter Destinations)
 */
UCLASS(hideCategories = Object, abstract, MinimalAPI)
class USoundModulationGenerator : public USoundModulatorBase
{
	GENERATED_UCLASS_BODY()

#if WITH_EDITOR
		virtual void PostEditChangeProperty(FPropertyChangedEvent& InPropertyChangedEvent) override;
#endif // WITH_EDITOR
};

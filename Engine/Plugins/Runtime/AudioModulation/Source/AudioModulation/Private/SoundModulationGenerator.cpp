// Copyright Epic Games, Inc. All Rights Reserved.
#include "SoundModulationGenerator.h"

#include "AudioDevice.h"
#include "AudioModulation.h"
#include "AudioModulationLogging.h"
#include "AudioModulationSystem.h"
#include "Engine/World.h"


USoundModulationGenerator::USoundModulationGenerator(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

#if WITH_EDITOR
void USoundModulationGenerator::PostEditChangeProperty(FPropertyChangedEvent& InPropertyChangedEvent)
{
	AudioModulation::IterateModSystems([this](AudioModulation::FAudioModulationSystem& OutModSystem)
	{
		OutModSystem.UpdateModulator(*this);
	});

	Super::PostEditChangeProperty(InPropertyChangedEvent);
}
#endif // WITH_EDITOR

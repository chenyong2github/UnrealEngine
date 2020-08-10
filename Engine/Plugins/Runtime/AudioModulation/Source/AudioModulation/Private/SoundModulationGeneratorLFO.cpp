// Copyright Epic Games, Inc. All Rights Reserved.
#include "SoundModulationGeneratorLFO.h"

#include "AudioDevice.h"
#include "AudioModulation.h"
#include "AudioModulationLogging.h"
#include "AudioModulationSystem.h"
#include "Engine/World.h"
#include "SoundModulationGeneratorLFOProxy.h"


USoundModulationGeneratorLFO::USoundModulationGeneratorLFO(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, Shape(ESoundModulationGeneratorLFOShape::Sine)
	, Amplitude(0.5f)
	, Frequency(1.0f)
	, Offset(0.5f)
	, bLooping(1)
	, bBypass(0)
{
}

void USoundModulationGeneratorLFO::BeginDestroy()
{
	Super::BeginDestroy();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (FAudioDeviceHandle AudioDevice = World->GetAudioDevice())
	{
		check(AudioDevice->IsModulationPluginEnabled());
		if (IAudioModulation* ModulationInterface = AudioDevice->ModulationInterface.Get())
		{
			auto ModSystem = static_cast<AudioModulation::FAudioModulation*>(ModulationInterface)->GetModulationSystem();
			check(ModSystem);
			ModSystem->DeactivateLFO(*this);
		}
	}
}

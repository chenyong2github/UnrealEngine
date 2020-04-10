// Copyright Epic Games, Inc. All Rights Reserved.
#include "SoundModulatorLFO.h"

#include "AudioDevice.h"
#include "AudioModulation.h"
#include "AudioModulationLogging.h"
#include "AudioModulationSystem.h"
#include "Engine/World.h"
#include "SoundModulatorLFOProxy.h"


USoundBusModulatorBase::USoundBusModulatorBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

#if WITH_EDITOR
void USoundBusModulatorBase::PostEditChangeProperty(FPropertyChangedEvent& InPropertyChangedEvent)
{
	if (FProperty* Property = InPropertyChangedEvent.Property)
	{
		AudioModulation::OnEditModulator(InPropertyChangedEvent, *this);
	}

	Super::PostEditChangeProperty(InPropertyChangedEvent);
}
#endif // WITH_EDITOR

USoundBusModulatorLFO::USoundBusModulatorLFO(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, Shape(ESoundModulatorLFOShape::Sine)
	, Amplitude(0.5f)
	, Frequency(1.0f)
	, Offset(0.5f)
	, bLooping(1)
	, bBypass(0)
{
}

void USoundBusModulatorLFO::BeginDestroy()
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

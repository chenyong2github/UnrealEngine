// Copyright Epic Games, Inc. All Rights Reserved.
#include "SoundModulationGenerator.h"

#include "AudioDevice.h"
#include "AudioModulation.h"
#include "AudioModulationLogging.h"
#include "AudioModulationSystem.h"
#include "Engine/World.h"
#include "Templates/Function.h"


namespace AudioModulation
{
	void IGenerator::AudioRenderThreadCommand(TUniqueFunction<void()>&& InCommand)
	{
		CommandQueue.Enqueue(MoveTemp(InCommand));
	}

	void IGenerator::PumpCommands()
	{
		TUniqueFunction<void()> Cmd;
		while (!CommandQueue.IsEmpty())
		{
			CommandQueue.Dequeue(Cmd);
			Cmd();
		}
	}
} // namespace AudioModulation

#if WITH_EDITOR
void USoundModulationGenerator::PostEditChangeProperty(FPropertyChangedEvent& InPropertyChangedEvent)
{
	AudioModulation::IterateModulationImpl([this](AudioModulation::FAudioModulation& OutModulation)
	{
		OutModulation.UpdateModulator(*this);
	});

	Super::PostEditChangeProperty(InPropertyChangedEvent);
}
#endif // WITH_EDITOR

void USoundModulationGenerator::BeginDestroy()
{
	using namespace AudioModulation;

	Super::BeginDestroy();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FAudioDeviceHandle AudioDevice = World->GetAudioDevice();
	if (AudioDevice.IsValid())
	{
		check(AudioDevice->IsModulationPluginEnabled());
		if (IAudioModulation* ModulationInterface = AudioDevice->ModulationInterface.Get())
		{
			FAudioModulation* Modulation = static_cast<FAudioModulation*>(ModulationInterface);
			check(Modulation);
			Modulation->DeactivateGenerator(*this);
		}
	}
}

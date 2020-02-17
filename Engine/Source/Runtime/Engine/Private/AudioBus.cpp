// Copyright Epic Games, Inc. All Rights Reserved.


#include "Sound/AudioBus.h"
#include "AudioDeviceManager.h"
#include "EngineGlobals.h"
#include "Engine/Engine.h"
#include "UObject/UObjectIterator.h"
#include "ActiveSound.h"
#include "AudioDevice.h"
#include "AudioMixerDevice.h"

UAudioBus::UAudioBus(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UAudioBus::BeginDestroy()
{
	Super::BeginDestroy();

	if (!GEngine)
	{
		return;
	}

	// Make sure we stop all audio bus instances on all devices if this object is getting destroyed
	uint32 AudioBusId = GetUniqueID();

	FAudioDeviceManager* AudioDeviceManager = GEngine->GetAudioDeviceManager();
	if (AudioDeviceManager)
	{
		TArray<FAudioDevice*> AudioDevices = AudioDeviceManager->GetAudioDevices();
		for (FAudioDevice* AudioDevice : AudioDevices)
		{
			if (AudioDevice->IsAudioMixerEnabled())
			{
				Audio::FMixerDevice* MixerDevice = static_cast<Audio::FMixerDevice*>(AudioDevice);
				MixerDevice->StopAudioBus(AudioBusId);
			}
		}
	}
}

#if WITH_EDITOR
void UAudioBus::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
}
#endif


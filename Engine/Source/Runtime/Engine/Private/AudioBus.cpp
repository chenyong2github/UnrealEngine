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
	if (!PropertyChangedEvent.Property)
	{
		return;
	}

	FName PropertyName = PropertyChangedEvent.Property->GetFName();
	if (PropertyName != GET_MEMBER_NAME_CHECKED(UAudioBus, AudioBusChannels))
	{
		return;
	}

	const UAudioSettings* AudioSettings = GetDefault<UAudioSettings>();
	if (!AudioSettings)
	{
		return;
	}

	for (const FDefaultAudioBusSettings& DefaultBusSettings : AudioSettings->DefaultAudioBuses)
	{
		if (DefaultBusSettings.AudioBus.ResolveObject() != this)
		{
			continue;
		}

		FAudioDeviceManager* DeviceManager = FAudioDeviceManager::Get();
		if (!DeviceManager)
		{
			continue;
		}

		// Restart bus with new channel count
		UObject* AudioBus = DefaultBusSettings.AudioBus.ResolveObject();
		if (this != AudioBus)
		{
			continue;
		}

		DeviceManager->IterateOverAllDevices([BusId = AudioBus->GetUniqueID(), NumChannels = AudioBusChannels](Audio::FDeviceId, FAudioDevice* InDevice)
		{
			if (Audio::FMixerDevice* MixerDevice = static_cast<Audio::FMixerDevice*>(InDevice))
			{
				MixerDevice->StopAudioBus(BusId);
				switch(NumChannels)
				{
					case EAudioBusChannels::Stereo:
						MixerDevice->StartAudioBus(BusId, 2, false /* bInIsAutomatic */);
					break;

					case EAudioBusChannels::Mono:
					default:
						MixerDevice->StartAudioBus(BusId, 1, false /* bInIsAutomatic */);
					break;
				}
			}
		});
	}
}
#endif // WITH_EDITOR

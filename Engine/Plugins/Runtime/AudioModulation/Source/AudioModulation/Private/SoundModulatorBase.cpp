// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "SoundModulatorBase.h"

#include "AudioDevice.h"
#include "AudioDeviceManager.h"
#include "IAudioExtensionPlugin.h"

#include "AudioModulation.h"
#include "AudioModulationInternal.h"


USoundModulatorBase::USoundModulatorBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

#if WITH_EDITOR
void USoundModulatorBase::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (!PropertyChangedEvent.Property)
	{
		return;
	}

	FAudioDeviceManager* DeviceManager = GEngine->GetAudioDeviceManager();
	if (!DeviceManager)
	{
		return;
	}

	const TArray<FAudioDevice*>& Devices = DeviceManager->GetAudioDevices();
	for (FAudioDevice* Device : Devices)
	{
		if (Device && Device->IsModulationPluginEnabled() && Device->ModulationInterface.IsValid())
		{
			auto Impl = static_cast<AudioModulation::FAudioModulation*>(Device->ModulationInterface.Get())->GetImpl();
			Impl->UpdateModulator(*this);
		}
	}
}
#endif // WITH_EDITOR

USoundBusModulatorBase::USoundBusModulatorBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

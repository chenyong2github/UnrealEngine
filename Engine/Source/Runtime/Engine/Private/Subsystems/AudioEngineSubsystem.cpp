// Copyright Epic Games, Inc. All Rights Reserved.

#include "Subsystems/AudioEngineSubsystem.h"

UAudioEngineSubsystem::UAudioEngineSubsystem()
	: UDynamicSubsystem()
{
}

FAudioDeviceHandle UAudioEngineSubsystem::GetAudioDeviceHandle(Audio::FDeviceId InDeviceID)
{
	if (FAudioDeviceManager* AudioDeviceManager = FAudioDeviceManager::Get())
	{
		return AudioDeviceManager->GetAudioDevice(InDeviceID);
	}

	return FAudioDeviceHandle();
}

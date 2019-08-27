// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"

#include "AudioModulation.h"
#include "AudioModulationInternal.h"


namespace AudioModulation
{
	UWorld* GetAudioWorld(const UObject* WorldContextObject)
	{
		if (!GEngine || !GEngine->UseSound())
		{
			return nullptr;
		}

		UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
		if (!World || !World->bAllowAudioPlayback || World->IsNetMode(NM_DedicatedServer))
		{
			return nullptr;
		}

		return World;
	}

	FAudioModulationImpl* GetModulationImpl(FAudioDevice* AudioDevice)
	{
		if (AudioDevice)
		{
			if (AudioDevice->IsModulationPluginEnabled())
			{
				if (IAudioModulation* ModulationInterface = AudioDevice->ModulationInterface.Get())
				{
					return static_cast<AudioModulation::FAudioModulation*>(ModulationInterface)->GetImpl();
				}
			}
		}

		return nullptr;
	}

	FAudioModulationImpl* GetModulationImpl(UWorld* World)
	{
		FAudioDevice* AudioDevice = nullptr;
		if (World)
		{
			AudioDevice = World->GetAudioDevice();
		}
		else
		{
			if (GEngine)
			{
				if (FAudioDeviceManager* DeviceManager = GEngine->GetAudioDeviceManager())
				{
					AudioDevice = DeviceManager->GetActiveAudioDevice();
				}
			}
		}
		return GetModulationImpl(AudioDevice);
	}

	template <class T>
	T* CreateModulatorBus(const UObject* WorldContextObject, FName Name, float DefaultValue, bool Activate)
	{
		UWorld* World = GetAudioWorld(WorldContextObject);
		if (!World)
		{
			return nullptr;
		}

		T* NewBus = NewObject<T>(nullptr, Name, RF_Transient);
		NewBus->DefaultValue = DefaultValue;

		if (Activate)
		{
			if (AudioModulation::FAudioModulationImpl* ModulationImpl = GetModulationImpl(World))
			{
				ModulationImpl->ActivateBus(*NewBus);
			}
		}

		return NewBus;
	}
} // namespace AudioModulation
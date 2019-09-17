// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AudioModulationStatics.h"

#include "AudioDevice.h"
#include "AudioModulation.h"
#include "AudioModulationInternal.h"
#include "AudioModulationLogging.h"

#include "Engine/Engine.h"
#include "SoundControlBus.h"

#define LOCTEXT_NAMESPACE "AudioModulationStatics"


namespace
{
	template <class T>
	T* CreateModulatorBus(const UObject* WorldContextObject, FName Name, float DefaultValue, bool Activate)
	{
		UWorld* World = UAudioModulationStatics::GetAudioWorld(WorldContextObject);
		if (!World)
		{
			return nullptr;
		}

		T* NewBus = NewObject<T>(nullptr, Name, RF_Transient);
		NewBus->DefaultValue = DefaultValue;

		if (Activate)
		{
			if (AudioModulation::FAudioModulationImpl* ModulationImpl = UAudioModulationStatics::GetModulationImpl(World))
			{
				ModulationImpl->ActivateBus(*NewBus);
			}
		}

		return NewBus;
	}
} // namespace <>

//////////////////////////////////////////////////////////////////////////
// UAudioModulationStatics

UAudioModulationStatics::UAudioModulationStatics(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UAudioModulationStatics::ActivateBus(const UObject* WorldContextObject, USoundControlBusBase* Bus)
{
	if (!Bus)
	{
		return;
	}

	UWorld* World = GetAudioWorld(WorldContextObject);
	if (AudioModulation::FAudioModulationImpl* ModulationImpl = GetModulationImpl(World))
	{
		ModulationImpl->ActivateBus(*Bus);
	}
}

void UAudioModulationStatics::ActivateBusMix(const UObject* WorldContextObject, USoundControlBusMix* BusMix)
{
	if (BusMix)
	{
		UWorld* World = GetAudioWorld(WorldContextObject);
		if (AudioModulation::FAudioModulationImpl* ModulationImpl = GetModulationImpl(World))
		{
			ModulationImpl->ActivateBusMix(*BusMix);
		}
	}
}

void UAudioModulationStatics::ActivateBusModulator(const UObject* WorldContextObject, USoundBusModulatorBase* Modulator)
{
	UWorld* World = GetAudioWorld(WorldContextObject);
	if (AudioModulation::FAudioModulationImpl* ModulationImpl = GetModulationImpl(World))
	{
		if (USoundBusModulatorLFO* LFO = Cast<USoundBusModulatorLFO>(Modulator))
		{
			ModulationImpl->ActivateLFO(*LFO);
		}
	}
}

UWorld* UAudioModulationStatics::GetAudioWorld(const UObject* WorldContextObject)
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

AudioModulation::FAudioModulationImpl* UAudioModulationStatics::GetModulationImpl(UWorld* World)
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
			AudioDevice = GEngine->GetMainAudioDevice();
		}
	}

	if (AudioDevice && AudioDevice->IsModulationPluginEnabled())
	{
		if (IAudioModulation* ModulationInterface = AudioDevice->ModulationInterface.Get())
		{
			return static_cast<AudioModulation::FAudioModulation*>(ModulationInterface)->GetImpl();
		}
	}

	return nullptr;
}

USoundVolumeControlBus* UAudioModulationStatics::CreateVolumeBus(const UObject* WorldContextObject, FName Name, float DefaultValue, bool Activate)
{
	return CreateModulatorBus<USoundVolumeControlBus>(WorldContextObject, Name, DefaultValue, Activate);
}

USoundPitchControlBus* UAudioModulationStatics::CreatePitchBus(const UObject* WorldContextObject, FName Name, float DefaultValue, bool Activate)
{
	return CreateModulatorBus<USoundPitchControlBus>(WorldContextObject, Name, DefaultValue, Activate);
}

USoundLPFControlBus* UAudioModulationStatics::CreateLPFBus(const UObject* WorldContextObject, FName Name, float DefaultValue, bool Activate)
{
	return CreateModulatorBus<USoundLPFControlBus>(WorldContextObject, Name, DefaultValue, Activate);
}

USoundHPFControlBus* UAudioModulationStatics::CreateHPFBus(const UObject* WorldContextObject, FName Name, float DefaultValue, bool Activate)
{
	return CreateModulatorBus<USoundHPFControlBus>(WorldContextObject, Name, DefaultValue, Activate);
}

USoundBusModulatorLFO* UAudioModulationStatics::CreateLFO(const UObject* WorldContextObject, FName Name, float Amplitude, float Frequency, float Offset, bool Activate)
{
	UWorld* World = GetAudioWorld(WorldContextObject);
	if (!World)
	{
		return nullptr;
	}

	USoundBusModulatorLFO* NewLFO = NewObject<USoundBusModulatorLFO>(nullptr, Name, RF_Transient);
	NewLFO->Amplitude = Amplitude;
	NewLFO->Frequency = Frequency;
	NewLFO->Offset    = Offset;

	if (Activate)
	{
		if (AudioModulation::FAudioModulationImpl* ModulationImpl = GetModulationImpl(World))
		{
			ModulationImpl->ActivateLFO(*NewLFO);
		}
	}

	return NewLFO;
}

USoundControlBusMix* UAudioModulationStatics::CreateBusMix(const UObject* WorldContextObject, FName Name, TArray<USoundControlBusBase*> Buses, float TargetValue, bool Activate)
{
	UWorld* World = GetAudioWorld(WorldContextObject);
	if (!World)
	{
		return nullptr;
	}

	USoundControlBusMix* NewBusMix = NewObject<USoundControlBusMix>(nullptr, Name, RF_Transient);
	for (USoundControlBusBase* Bus : Buses)
	{
		if (Bus)
		{
			NewBusMix->Channels.Emplace_GetRef(Bus, TargetValue);
		}
		else
		{
			UE_LOG(LogAudioModulation, Warning,
				TEXT("USoundControlBusMix '%s' was created but bus provided is null. Channel not added."),
				*Name.ToString());
		}
	}

	if (Activate)
	{
		if (AudioModulation::FAudioModulationImpl* ModulationImpl = GetModulationImpl(World))
		{
			ModulationImpl->ActivateBusMix(*NewBusMix, false);
		}
	}

	return NewBusMix;
}

void UAudioModulationStatics::DeactivateBus(const UObject* WorldContextObject, USoundControlBusBase* Bus)
{
	if (!Bus)
	{
		return;
	}

	UWorld* World = GetAudioWorld(WorldContextObject);
	if (AudioModulation::FAudioModulationImpl* ModulationImpl = GetModulationImpl(World))
	{
		auto BusId = static_cast<const AudioModulation::FBusId>(Bus->GetUniqueID());
		ModulationImpl->DeactivateBus(BusId);
	}
}

void UAudioModulationStatics::DeactivateBusMix(const UObject* WorldContextObject, USoundControlBusMix* BusMix)
{
	if (BusMix)
	{
		UWorld* World = GetAudioWorld(WorldContextObject);
		if (AudioModulation::FAudioModulationImpl* ModulationImpl = GetModulationImpl(World))
		{
			auto BusMixId = static_cast<const AudioModulation::FBusMixId>(BusMix->GetUniqueID());
			ModulationImpl->DeactivateBusMix(BusMixId);
		}
	}
}

void UAudioModulationStatics::DeactivateBusModulator(const UObject* WorldContextObject, USoundBusModulatorBase* Modulator)
{
	UWorld* World = GetAudioWorld(WorldContextObject);
	if (AudioModulation::FAudioModulationImpl* ModulationImpl = GetModulationImpl(World))
	{
		if (USoundBusModulatorLFO* LFO = Cast<USoundBusModulatorLFO>(Modulator))
		{
			auto LFOId = static_cast<const AudioModulation::FLFOId>(Modulator->GetUniqueID());
			ModulationImpl->DeactivateLFO(LFOId);
		}
	}
}

void UAudioModulationStatics::UpdateModulator(const UObject* WorldContextObject, USoundModulatorBase* Modulator)
{
	if (Modulator)
	{
		UWorld* World = GetAudioWorld(WorldContextObject);
		if (AudioModulation::FAudioModulationImpl* ModulationImpl = GetModulationImpl(World))
		{
			ModulationImpl->UpdateModulator(*Modulator);
		}
	}
}
#undef LOCTEXT_NAMESPACE

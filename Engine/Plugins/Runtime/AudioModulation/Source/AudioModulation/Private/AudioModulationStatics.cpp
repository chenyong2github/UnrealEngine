// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AudioModulationStatics.h"

#include "AudioDevice.h"
#include "AudioModulation.h"
#include "AudioModulationInternal.h"
#include "AudioModulationLogging.h"

#include "Engine/Engine.h"
#include "SoundModulatorBus.h"

#define LOCTEXT_NAMESPACE "AudioModulationStatics"


namespace
{
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
} // namespace <>

//////////////////////////////////////////////////////////////////////////
// UAudioModulationStatics

UAudioModulationStatics::UAudioModulationStatics(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UAudioModulationStatics::ActivateBus(const UObject* WorldContextObject, USoundModulatorBusBase* Bus)
{
	if (!Bus)
	{
		return;
	}

	UWorld* World = AudioModulation::GetAudioWorld(WorldContextObject);
	if (AudioModulation::FAudioModulationImpl* ModulationImpl = AudioModulation::GetModulationImpl(World))
	{
		ModulationImpl->ActivateBus(*Bus);
	}
}

void UAudioModulationStatics::ActivateBusMix(const UObject* WorldContextObject, USoundModulatorBusMix* BusMix)
{
	if (BusMix)
	{
		UWorld* World = AudioModulation::GetAudioWorld(WorldContextObject);
		if (AudioModulation::FAudioModulationImpl* ModulationImpl = AudioModulation::GetModulationImpl(World))
		{
			ModulationImpl->ActivateBusMix(*BusMix, false);
		}
	}
}

void UAudioModulationStatics::ActivateModulator(const UObject* WorldContextObject, USoundModulatorBase* Modulator)
{
	UWorld* World = AudioModulation::GetAudioWorld(WorldContextObject);
	if (AudioModulation::FAudioModulationImpl* ModulationImpl = AudioModulation::GetModulationImpl(World))
	{
		if (USoundModulatorLFO* LFO = Cast<USoundModulatorLFO>(Modulator))
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
			if (FAudioDeviceManager* DeviceManager = GEngine->GetAudioDeviceManager())
			{
				AudioDevice = DeviceManager->GetActiveAudioDevice();
			}
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

USoundVolumeModulatorBus* UAudioModulationStatics::CreateVolumeBus(const UObject* WorldContextObject, FName Name, float DefaultValue, bool Activate)
{
	return CreateModulatorBus<USoundVolumeModulatorBus>(WorldContextObject, Name, DefaultValue, Activate);
}

USoundPitchModulatorBus* UAudioModulationStatics::CreatePitchBus(const UObject* WorldContextObject, FName Name, float DefaultValue, bool Activate)
{
	return CreateModulatorBus<USoundPitchModulatorBus>(WorldContextObject, Name, DefaultValue, Activate);
}

USoundLPFModulatorBus* UAudioModulationStatics::CreateLPFBus(const UObject* WorldContextObject, FName Name, float DefaultValue, bool Activate)
{
	return CreateModulatorBus<USoundLPFModulatorBus>(WorldContextObject, Name, DefaultValue, Activate);
}

USoundHPFModulatorBus* UAudioModulationStatics::CreateHPFBus(const UObject* WorldContextObject, FName Name, float DefaultValue, bool Activate)
{
	return CreateModulatorBus<USoundHPFModulatorBus>(WorldContextObject, Name, DefaultValue, Activate);
}

USoundModulatorLFO* UAudioModulationStatics::CreateLFO(const UObject* WorldContextObject, FName Name, float Amplitude, float Frequency, float Offset, bool Activate)
{
	UWorld* World = AudioModulation::GetAudioWorld(WorldContextObject);
	if (!World)
	{
		return nullptr;
	}

	USoundModulatorLFO* NewLFO = NewObject<USoundModulatorLFO>(nullptr, Name, RF_Transient);
	NewLFO->Amplitude = Amplitude;
	NewLFO->Frequency = Frequency;
	NewLFO->Offset    = Offset;

	if (Activate)
	{
		if (AudioModulation::FAudioModulationImpl* ModulationImpl = AudioModulation::GetModulationImpl(World))
		{
			ModulationImpl->ActivateLFO(*NewLFO);
		}
	}

	return NewLFO;
}

USoundModulatorBusMix* UAudioModulationStatics::CreateBusMix(const UObject* WorldContextObject, FName Name, TArray<USoundModulatorBusBase*> Buses, float TargetValue, bool Activate)
{
	UWorld* World = AudioModulation::GetAudioWorld(WorldContextObject);
	if (!World)
	{
		return nullptr;
	}

	USoundModulatorBusMix* NewBusMix = NewObject<USoundModulatorBusMix>(nullptr, Name, RF_Transient);
	for (USoundModulatorBusBase* Bus : Buses)
	{
		if (Bus)
		{
			NewBusMix->Channels.Emplace_GetRef(Bus, TargetValue);
		}
		else
		{
			UE_LOG(LogAudioModulation, Warning,
				TEXT("USoundModulatorBusMix '%s' was created but bus provided is null. Channel not added."),
				*Name.ToString());
		}
	}

	if (Activate)
	{
		if (AudioModulation::FAudioModulationImpl* ModulationImpl = AudioModulation::GetModulationImpl(World))
		{
			ModulationImpl->ActivateBusMix(*NewBusMix, false);
		}
	}

	return NewBusMix;
}

void UAudioModulationStatics::DeactivateBus(const UObject* WorldContextObject, USoundModulatorBusBase* Bus)
{
	if (!Bus)
	{
		return;
	}

	UWorld* World = AudioModulation::GetAudioWorld(WorldContextObject);
	if (AudioModulation::FAudioModulationImpl* ModulationImpl = AudioModulation::GetModulationImpl(World))
	{
		auto BusId = static_cast<const AudioModulation::BusId>(Bus->GetUniqueID());
		ModulationImpl->DeactivateBus(BusId);
	}
}

void UAudioModulationStatics::DeactivateBusMix(const UObject* WorldContextObject, USoundModulatorBusMix* BusMix)
{
	if (BusMix)
	{
		UWorld* World = AudioModulation::GetAudioWorld(WorldContextObject);
		if (AudioModulation::FAudioModulationImpl* ModulationImpl = AudioModulation::GetModulationImpl(World))
		{
			auto BusMixId = static_cast<const AudioModulation::BusMixId>(BusMix->GetUniqueID());
			ModulationImpl->DeactivateBusMix(BusMixId);
		}
	}
}

void UAudioModulationStatics::DeactivateModulator(const UObject* WorldContextObject, USoundModulatorBase* Modulator)
{
	UWorld* World = AudioModulation::GetAudioWorld(WorldContextObject);
	if (AudioModulation::FAudioModulationImpl* ModulationImpl = AudioModulation::GetModulationImpl(World))
	{
		if (USoundModulatorLFO* LFO = Cast<USoundModulatorLFO>(Modulator))
		{
			auto LFOId = static_cast<const AudioModulation::LFOId>(Modulator->GetUniqueID());
			ModulationImpl->DeactivateLFO(LFOId);
		}
	}
}

void UAudioModulationStatics::SetBusDefault(const UObject* WorldContextObject, USoundModulatorBusBase* Bus, float Value)
{
	if (Bus)
	{
		UWorld* World = AudioModulation::GetAudioWorld(WorldContextObject);
		if (AudioModulation::FAudioModulationImpl* ModulationImpl = AudioModulation::GetModulationImpl(World))
		{
			ModulationImpl->SetBusDefault(*Bus, Value);
		}
	}
	else
	{
		UE_LOG(LogAudioModulation, Warning, TEXT("Bus not specified. Bus default set request ignored."));
	}
}

void UAudioModulationStatics::SetBusMixChannel(const UObject* WorldContextObject, USoundModulatorBusMix* BusMix, USoundModulatorBusBase* Bus, float TargetValue)
{
	if (BusMix && Bus)
	{
		UWorld* World = AudioModulation::GetAudioWorld(WorldContextObject);
		if (AudioModulation::FAudioModulationImpl* ModulationImpl = AudioModulation::GetModulationImpl(World))
		{
			ModulationImpl->SetBusMixChannel(*BusMix, *Bus, TargetValue);
		}
	}
	else
	{
		UE_LOG(LogAudioModulation, Warning, TEXT("USoundModulatorBusMix or bus not specified. Bus channel mix set request ignored."));
	}
}

void UAudioModulationStatics::SetLFOFrequency(const UObject* WorldContextObject, USoundModulatorLFO* LFO, float Freq)
{
	if (LFO)
	{
		UWorld* World = AudioModulation::GetAudioWorld(WorldContextObject);
		if (AudioModulation::FAudioModulationImpl* ModulationImpl = AudioModulation::GetModulationImpl(World))
		{
			ModulationImpl->SetLFOFrequency(*LFO, Freq);
		}
	}
	else
	{
		UE_LOG(LogAudioModulation, Warning,
			TEXT("USoundModulatorLFO not specified. LFO frequency set ignored."));
	}
}

#undef LOCTEXT_NAMESPACE

// Copyright Epic Games, Inc. All Rights Reserved.
#include "AudioModulationStatics.h"

#include "AudioDevice.h"
#include "AudioModulation.h"
#include "AudioModulationInternal.h"
#include "AudioModulationLogging.h"

#include "Engine/Engine.h"
#include "SoundControlBus.h"
#include "SoundControlBusMix.h"

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

		T* NewBus = NewObject<T>(GetTransientPackage(), Name);
		NewBus->DefaultValue = DefaultValue;
		NewBus->Address = Name.ToString();

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

	USoundBusModulatorLFO* NewLFO = NewObject<USoundBusModulatorLFO>(GetTransientPackage(), Name);
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

FSoundControlBusMixChannel UAudioModulationStatics::CreateBusMixChannel(const UObject* WorldContextObject, USoundControlBusBase* Bus, float Value, float AttackTime, float ReleaseTime)
{
	FSoundControlBusMixChannel MixChannel;
	MixChannel.Bus = Bus;
	MixChannel.Value = FSoundModulationValue(Value, AttackTime, ReleaseTime);
	return MixChannel;
}

USoundControlBusMix* UAudioModulationStatics::CreateBusMix(const UObject* WorldContextObject, FName Name, TArray<FSoundControlBusMixChannel> Channels, bool Activate)
{
	UWorld* World = GetAudioWorld(WorldContextObject);
	if (!World)
	{
		return nullptr;
	}

	USoundControlBusMix* NewBusMix = NewObject<USoundControlBusMix>(GetTransientPackage(), Name);
	for (FSoundControlBusMixChannel& Channel : Channels)
	{
		if (Channel.Bus)
		{
			NewBusMix->Channels.Emplace_GetRef(Channel);
		}
		else
		{
			UE_LOG(LogAudioModulation, Warning,
				TEXT("USoundControlBusMix '%s' was created but bus provided is null. Channel not added to mix."),
				*Name.ToString());
		}
	}

	if (Activate)
	{
		if (AudioModulation::FAudioModulationImpl* ModulationImpl = GetModulationImpl(World))
		{
			ModulationImpl->ActivateBusMix(*NewBusMix);
		}
	}

	return NewBusMix;
}

void UAudioModulationStatics::DeactivateBus(const UObject* WorldContextObject, USoundControlBusBase* Bus)
{
	if (Bus)
	{
		UWorld* World = GetAudioWorld(WorldContextObject);
		if (AudioModulation::FAudioModulationImpl* ModulationImpl = GetModulationImpl(World))
		{
			ModulationImpl->DeactivateBus(*Bus);
		}
	}
}

void UAudioModulationStatics::DeactivateBusMix(const UObject* WorldContextObject, USoundControlBusMix* BusMix)
{
	if (BusMix)
	{
		UWorld* World = GetAudioWorld(WorldContextObject);
		if (AudioModulation::FAudioModulationImpl* ModulationImpl = GetModulationImpl(World))
		{
			ModulationImpl->DeactivateBusMix(*BusMix);
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
			ModulationImpl->DeactivateLFO(*LFO);
		}
	}
}

void UAudioModulationStatics::UpdateMix(const UObject* WorldContextObject, USoundControlBusMix* Mix, TArray<FSoundControlBusMixChannel> Channels)
{
	if (Mix)
	{
		UWorld* World = GetAudioWorld(WorldContextObject);
		if (AudioModulation::FAudioModulationImpl* ModulationImpl = GetModulationImpl(World))
		{
			ModulationImpl->UpdateMix(*Mix, Channels);
		}
	}
}

void UAudioModulationStatics::UpdateMixByFilter(
	const UObject*						WorldContextObject,
	USoundControlBusMix*				Mix,
	FString								AddressFilter,
	TSubclassOf<USoundControlBusBase>	BusClassFilter,
	float								Value,
	float								AttackTime,
	float								ReleaseTime)
{
	if (Mix)
	{
		UWorld* World = GetAudioWorld(WorldContextObject);
		if (AudioModulation::FAudioModulationImpl* ModulationImpl = GetModulationImpl(World))
		{
			FSoundModulationValue ModValue(Value, AttackTime, ReleaseTime);
			ModulationImpl->UpdateMixByFilter(*Mix, AddressFilter, BusClassFilter, ModValue);
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

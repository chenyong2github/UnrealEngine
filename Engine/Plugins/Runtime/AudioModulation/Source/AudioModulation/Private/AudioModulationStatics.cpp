// Copyright Epic Games, Inc. All Rights Reserved.
#include "AudioModulationStatics.h"

#include "Async/Async.h"
#include "AudioDevice.h"
#include "AudioModulation.h"
#include "AudioModulationInternal.h"
#include "AudioModulationLogging.h"
#include "AudioModulationProfileSerializer.h"
#include "CoreGlobals.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "SoundControlBus.h"
#include "SoundControlBusMix.h"

#define LOCTEXT_NAMESPACE "AudioModulationStatics"


static FAutoConsoleCommand GModulationSaveMixProfile(
	TEXT("au.Modulation.SaveMixProfile"),
	TEXT("Saves modulation mix profile to the config save directory.\n"
		"Path - Path to Object\n"
		"ProfileIndex - (Optional) Index of profile (defaults to 0)"),
	FConsoleCommandWithArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args)
		{
			if (Args.Num() < 1)
			{
				UE_LOG(LogAudioModulation, Error, TEXT("Failed to save mix profile: Path not provided"));
				return;
			}

			const FString& Path = Args[0];
			int32 ProfileIndex = 0;
			if (Args.Num() > 1)
			{
				ProfileIndex = FCString::Atoi(*Args[1]);
			}

			FSoftObjectPath ObjPath = Path;
			if (UObject* MixObj = ObjPath.TryLoad())
			{
				if (USoundControlBusMix* Mix = Cast<USoundControlBusMix>(MixObj))
				{
					UAudioModulationStatics::SaveMixToProfile(Mix, Mix, ProfileIndex);
					return;
				}
			}

			UE_LOG(LogAudioModulation, Error, TEXT("Failed to save mix '%s' to profile index '%i'"), *Path, ProfileIndex);
		}
	)
);

static FAutoConsoleCommand GModulationLoadMixProfile(
	TEXT("au.Modulation.LoadMixProfile"),
	TEXT("Loads modulation mix profile from the config save directory.\n"
		"Path - Path to Object to load\n"
		"Activate - (Optional) Whether or not to activate/update the mix once it is loaded (default: true)."
		"ProfileIndex - (Optional) Index of profile (default: 0)"),
	FConsoleCommandWithArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args)
		{
			if (Args.Num() < 1)
			{
				UE_LOG(LogAudioModulation, Error, TEXT("Failed to load mix profile: Object path not provided"));
				return;
			}

			const FString& Path = Args[0];
			int32 ProfileIndex = 0;
			if (Args.Num() > 1)
			{
				ProfileIndex = FCString::Atoi(*Args[1]);
			}

			bool bActivateUpdate = true;
			if (Args.Num() > 2)
			{
				bActivateUpdate = FCString::ToBool(*Args[2]);
			}

			FSoftObjectPath ObjPath = Path;
			if (UObject* MixObj = ObjPath.TryLoad())
			{
				if (USoundControlBusMix* Mix = Cast<USoundControlBusMix>(MixObj))
				{
					UAudioModulationStatics::LoadMixFromProfile(Mix, Mix, bActivateUpdate, ProfileIndex);

					if (bActivateUpdate)
					{
						UAudioModulationStatics::UpdateModulator(Mix, Mix);
					}
					return;
				}
			}

			UE_LOG(LogAudioModulation, Error, TEXT("Failed to load mix '%s' from profile index '%i'"), *Path, ProfileIndex);
		}
	)
);


namespace AudioModulation
{
	template <class T>
	T* CreateBus(const UObject* WorldContextObject, FName Name, float DefaultValue, bool Activate)
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
			if (FAudioModulationImpl* ModulationImpl = UAudioModulationStatics::GetModulationImpl(World))
			{
				ModulationImpl->ActivateBus(*NewBus);
			}
		}

		return NewBus;
	}
} // namespace AudioModulation


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
	FAudioDeviceHandle AudioDevice;
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
	return AudioModulation::CreateBus<USoundVolumeControlBus>(WorldContextObject, Name, DefaultValue, Activate);
}

USoundPitchControlBus* UAudioModulationStatics::CreatePitchBus(const UObject* WorldContextObject, FName Name, float DefaultValue, bool Activate)
{
	return AudioModulation::CreateBus<USoundPitchControlBus>(WorldContextObject, Name, DefaultValue, Activate);
}

USoundLPFControlBus* UAudioModulationStatics::CreateLPFBus(const UObject* WorldContextObject, FName Name, float DefaultValue, bool Activate)
{
	return AudioModulation::CreateBus<USoundLPFControlBus>(WorldContextObject, Name, DefaultValue, Activate);
}

USoundHPFControlBus* UAudioModulationStatics::CreateHPFBus(const UObject* WorldContextObject, FName Name, float DefaultValue, bool Activate)
{
	return AudioModulation::CreateBus<USoundHPFControlBus>(WorldContextObject, Name, DefaultValue, Activate);
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

void UAudioModulationStatics::SaveMixToProfile(const UObject* WorldContextObject, USoundControlBusMix* BusMix, int32 ProfileIndex)
{
	UWorld* World = GetAudioWorld(WorldContextObject);
	if (AudioModulation::FAudioModulationImpl* ModulationImpl = GetModulationImpl(World))
	{
		if (BusMix)
		{
			return ModulationImpl->SaveMixToProfile(*BusMix, ProfileIndex);
		}
	}
}

TArray<FSoundControlBusMixChannel> UAudioModulationStatics::LoadMixFromProfile(const UObject* WorldContextObject, USoundControlBusMix* BusMix, bool bActivate, int32 ProfileIndex)
{
	if (BusMix)
	{
		UWorld* World = GetAudioWorld(WorldContextObject);
		if (AudioModulation::FAudioModulationImpl* ModulationImpl = GetModulationImpl(World))
		{
			if (bActivate)
			{
				ActivateBusMix(WorldContextObject, BusMix);
			}
			return ModulationImpl->LoadMixFromProfile(ProfileIndex, *BusMix);
		}
	}

	return TArray<FSoundControlBusMixChannel>();
}

void UAudioModulationStatics::UpdateMix(const UObject* WorldContextObject, USoundControlBusMix* Mix, TArray<FSoundControlBusMixChannel> Channels)
{
	if (Mix)
	{
		UWorld* World = GetAudioWorld(WorldContextObject);
		if (AudioModulation::FAudioModulationImpl* ModulationImpl = GetModulationImpl(World))
		{
			// UObject representation is not updated in this form of the call as doing so from
			// PIE can result in an unstable state where UObject is modified but not properly dirtied.
			ModulationImpl->UpdateMix(Channels, *Mix, false /* bUpdateObject */);
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

			// UObject representation is not updated in this form of the call as doing so from
			// PIE can result in an unstable state where UObject is modified but not properly dirtied.
			ModulationImpl->UpdateMixByFilter(AddressFilter, BusClassFilter, ModValue, *Mix, false /* bUpdateObject */);
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

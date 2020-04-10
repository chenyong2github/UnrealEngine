// Copyright Epic Games, Inc. All Rights Reserved.
#include "SoundControlBus.h"

#include "AudioDevice.h"
#include "AudioDeviceManager.h"
#include "AudioModulation.h"
#include "AudioModulationLogging.h"
#include "AudioModulationSystem.h"
#include "Engine/World.h"
#include "SoundControlBusProxy.h"
#include "SoundModulatorLFO.h"

USoundControlBusBase::USoundControlBusBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bBypass(false)
	, DefaultValue(1.0f)
	, Min(0.0f)
	, Max(1.0f)
#if WITH_EDITORONLY_DATA
	, bOverrideAddress(false)
#endif
{
}

#if WITH_EDITOR
void USoundControlBusBase::PostDuplicate(EDuplicateMode::Type DuplicateMode)
{
	if (!bOverrideAddress)
	{
		Address = GetName();
	}

	Super::PostDuplicate(DuplicateMode);
}

void USoundControlBusBase::PostEditChangeProperty(FPropertyChangedEvent& InPropertyChangedEvent)
{
	if (FProperty* Property = InPropertyChangedEvent.Property)
	{
		if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(USoundControlBusBase, bOverrideAddress) && !bOverrideAddress)
		{
			Address = GetName();
		}

		if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(USoundControlBusBase, DefaultValue)
			|| Property->GetFName() == GET_MEMBER_NAME_CHECKED(USoundControlBusBase, Min)
			|| Property->GetFName() == GET_MEMBER_NAME_CHECKED(USoundControlBusBase, Max))
		{
			Min = FMath::Min(Min, Max);
			DefaultValue = FMath::Clamp(DefaultValue, Min, Max);
		}

		AudioModulation::OnEditModulator(InPropertyChangedEvent, *this);
	}

	Super::PostEditChangeProperty(InPropertyChangedEvent);
}

void USoundControlBusBase::PostInitProperties()
{
	if (!bOverrideAddress)
	{
		Address = GetName();
	}

	Super::PostInitProperties();
}

void USoundControlBusBase::PostRename(UObject* OldOuter, const FName OldName)
{
	if (!bOverrideAddress)
	{
		Address = GetName();
	}
}
#endif // WITH_EDITOR

USoundVolumeControlBus::USoundVolumeControlBus(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

USoundPitchControlBus::USoundPitchControlBus(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

USoundLPFControlBus::USoundLPFControlBus(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

USoundHPFControlBus::USoundHPFControlBus(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DefaultValue = 0.0f;
}

USoundControlBus::USoundControlBus(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void USoundControlBusBase::BeginDestroy()
{
	Super::BeginDestroy();

	if (UWorld* World = GetWorld())
	{
		if (FAudioDeviceHandle AudioDevice = World->GetAudioDevice())
		{
			check(AudioDevice->IsModulationPluginEnabled());
			if (IAudioModulation* ModulationInterface = AudioDevice->ModulationInterface.Get())
			{
				auto ModSystem = static_cast<AudioModulation::FAudioModulation*>(ModulationInterface)->GetModulationSystem();
				check(ModSystem);
				ModSystem->DeactivateBus(*this);
			}
		}
	}
}

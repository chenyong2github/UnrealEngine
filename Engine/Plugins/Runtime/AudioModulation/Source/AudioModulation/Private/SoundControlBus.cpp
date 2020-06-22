// Copyright Epic Games, Inc. All Rights Reserved.
#include "SoundControlBus.h"

#include "AudioDevice.h"
#include "AudioDeviceManager.h"
#include "AudioModulation.h"
#include "AudioModulationLogging.h"
#include "AudioModulationSystem.h"
#include "DSP/BufferVectorOperations.h"
#include "Engine/World.h"
#include "SoundControlBusProxy.h"
#include "SoundModulatorLFO.h"

USoundControlBusBase::USoundControlBusBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bBypass(false)
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

		AudioModulation::IterateModSystems([this](AudioModulation::FAudioModulationSystem & OutModSystem)
		{
			OutModSystem.UpdateModulator(*this);
		});
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

const Audio::FModulationMixFunction& USoundControlBusBase::GetMixFunction() const
{
	return Audio::FModulationParameter::GetDefaultMixFunction();
}


USoundVolumeControlBus::USoundVolumeControlBus(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

#if WITH_EDITOR
void USoundVolumeControlBus::PostEditChangeProperty(FPropertyChangedEvent& InPropertyChangedEvent)
{
	Super::PostEditChangeProperty(InPropertyChangedEvent);
}
#endif // WITH_EDITOR

USoundPitchControlBus::USoundPitchControlBus(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

#if WITH_EDITOR
void USoundPitchControlBus::PostEditChangeProperty(FPropertyChangedEvent& InPropertyChangedEvent)
{
	Super::PostEditChangeProperty(InPropertyChangedEvent);
}
#endif // WITH_EDITOR

USoundLPFControlBus::USoundLPFControlBus(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

#if WITH_EDITOR
void USoundLPFControlBus::PostEditChangeProperty(FPropertyChangedEvent& InPropertyChangedEvent)
{
	Super::PostEditChangeProperty(InPropertyChangedEvent);
}
#endif // WITH_EDITOR

USoundHPFControlBus::USoundHPFControlBus(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DefaultValue = 0.0f;
}

#if WITH_EDITOR
void USoundHPFControlBus::PostEditChangeProperty(FPropertyChangedEvent& InPropertyChangedEvent)
{
	Super::PostEditChangeProperty(InPropertyChangedEvent);
}
#endif // WITH_EDITOR

USoundControlBus::USoundControlBus(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, Parameter(nullptr)
{
}

#if WITH_EDITOR
void USoundControlBus::PostEditChangeProperty(FPropertyChangedEvent& InPropertyChangedEvent)
{
	Super::PostEditChangeProperty(InPropertyChangedEvent);
}
#endif // WITH_EDITOR

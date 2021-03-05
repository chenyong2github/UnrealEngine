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


USoundControlBus::USoundControlBus(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bBypass(false)
#if WITH_EDITORONLY_DATA
	, bOverrideAddress(false)
#endif // WITH_EDITORONLY_DATA
	, Parameter(nullptr)
{
}

#if WITH_EDITOR
void USoundControlBus::PostDuplicate(EDuplicateMode::Type DuplicateMode)
{
	if (!bOverrideAddress)
	{
		Address = GetName();
	}

	Super::PostDuplicate(DuplicateMode);
}

void USoundControlBus::PostEditChangeProperty(FPropertyChangedEvent& InPropertyChangedEvent)
{
	if (FProperty* Property = InPropertyChangedEvent.Property)
	{
		if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(USoundControlBus, bOverrideAddress) && !bOverrideAddress)
		{
			Address = GetName();
		}

		if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(USoundControlBus, Parameter))
		{
			for (TObjectIterator<USoundControlBusMix> Iter; Iter; ++Iter)
			{
				if (USoundControlBusMix* Mix = *Iter)
				{
					for (FSoundControlBusMixStage& Stage : Mix->MixStages)
					{
						if (Stage.Bus == this)
						{
							float UnitValue = Stage.Value.TargetValue;
							if (Parameter)
							{
								UnitValue = Parameter->ConvertNormalizedToUnit(Stage.Value.TargetValue);
							}

							if (!FMath::IsNearlyEqual(Stage.Value.TargetUnitValue, UnitValue, KINDA_SMALL_NUMBER))
							{
								Stage.Value.TargetUnitValue = UnitValue;
							}
						}
					}
				}
			}
		}

		AudioModulation::IterateModulationImpl([this](AudioModulation::FAudioModulation& OutModSystem)
		{
			OutModSystem.UpdateModulator(*this);
		});
	}

	Super::PostEditChangeProperty(InPropertyChangedEvent);
}

void USoundControlBus::PostInitProperties()
{
	if (!bOverrideAddress)
	{
		Address = GetName();
	}

	Super::PostInitProperties();
}

void USoundControlBus::PostRename(UObject* OldOuter, const FName OldName)
{
	if (!bOverrideAddress)
	{
		Address = GetName();
	}
}
#endif // WITH_EDITOR

void USoundControlBus::BeginDestroy()
{
	using namespace AudioModulation;

	Super::BeginDestroy();

	if (UWorld* World = GetWorld())
	{
		FAudioDeviceHandle AudioDevice = World->GetAudioDevice();
		if (AudioDevice.IsValid())
		{
			check(AudioDevice->IsModulationPluginEnabled());
			if (IAudioModulation* ModulationInterface = AudioDevice->ModulationInterface.Get())
			{
				FAudioModulation* Modulation = static_cast<FAudioModulation*>(ModulationInterface);
				check(Modulation);
				Modulation->DeactivateBus(*this);
			}
		}
	}
}

const Audio::FModulationMixFunction USoundControlBus::GetMixFunction() const
{
	if (Parameter)
	{
		return Parameter->GetMixFunction();
	}

	return Audio::FModulationParameter::GetDefaultMixFunction();
}

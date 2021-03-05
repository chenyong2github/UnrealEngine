// Copyright Epic Games, Inc. All Rights Reserved.
#include "SoundControlBusMix.h"

#include "Audio/AudioAddressPattern.h"
#include "AudioDevice.h"
#include "AudioModulation.h"
#include "AudioModulationLogging.h"
#include "AudioModulationProfileSerializer.h"
#include "AudioModulationStatics.h"
#include "Engine/World.h"
#include "SoundControlBus.h"

#if WITH_EDITOR
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#endif // WITH_EDITOR

#define LOCTEXT_NAMESPACE "AudioModulation"


FSoundControlBusMixStage::FSoundControlBusMixStage()
	: Bus(nullptr)
{
}

FSoundControlBusMixStage::FSoundControlBusMixStage(USoundControlBus* InBus, const float TargetValue)
	: Bus(InBus)
{
	Value.TargetValue = FMath::Clamp(TargetValue, 0.0f, 1.0f);
}

USoundControlBusMix::USoundControlBusMix(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, ProfileIndex(0)
{
}

void USoundControlBusMix::BeginDestroy()
{
	using namespace AudioModulation;

	Super::BeginDestroy();

	if (UWorld* World = GetWorld())
	{
		FAudioDeviceHandle AudioDevice = World->GetAudioDevice();
		if (AudioDevice.IsValid())
		{
			if (AudioDevice->IsModulationPluginEnabled())
			{
				if (IAudioModulation* ModulationInterface = AudioDevice->ModulationInterface.Get())
				{
					FAudioModulation* Modulation = static_cast<FAudioModulation*>(ModulationInterface);
					check(Modulation);
					Modulation->DeactivateBusMix(*this);
				}
			}
		}
	}
}

void USoundControlBusMix::ActivateMix()
{
	AudioModulation::IterateModulationImpl([this](AudioModulation::FAudioModulation& OutModulation)
	{
		OutModulation.ActivateBusMix(*this);
	});
}

void USoundControlBusMix::DeactivateMix()
{
	AudioModulation::IterateModulationImpl([this](AudioModulation::FAudioModulation& OutModulation)
	{
		OutModulation.DeactivateBusMix(*this);
	});
}

void USoundControlBusMix::DeactivateAllMixes()
{
	AudioModulation::IterateModulationImpl([this](AudioModulation::FAudioModulation& OutModulation)
	{
		OutModulation.DeactivateAllBusMixes();
	});
}

void USoundControlBusMix::LoadMixFromProfile()
{
	const bool bSucceeded = AudioModulation::FProfileSerializer::Deserialize(ProfileIndex, *this);
#if WITH_EDITOR
	if (bSucceeded)
	{
		FNotificationInfo Info(FText::Format(
			LOCTEXT("SoundControlBusMix_LoadSucceeded", "'Control Bus Mix '{0}' profile {1} loaded successfully."),
			FText::FromName(GetFName()),
			FText::AsNumber(ProfileIndex)
		));
		Info.bFireAndForget = true;
		Info.ExpireDuration = 2.0f;
		Info.bUseThrobber = true;
		FSlateNotificationManager::Get().AddNotification(Info);
	}
#endif // WITH_EDITOR

	ActivateMix();
}

#if WITH_EDITOR
void USoundControlBusMix::PostEditChangeProperty(FPropertyChangedEvent& InPropertyChangedEvent)
{
	OnPropertyChanged(InPropertyChangedEvent.Property, InPropertyChangedEvent.ChangeType);
	Super::PostEditChangeProperty(InPropertyChangedEvent);
}

void USoundControlBusMix::PostEditChangeChainProperty(FPropertyChangedChainEvent& InPropertyChangedEvent)
{
	OnPropertyChanged(InPropertyChangedEvent.Property, InPropertyChangedEvent.ChangeType);
	Super::PostEditChangeChainProperty(InPropertyChangedEvent);
}

void USoundControlBusMix::OnPropertyChanged(FProperty* Property, EPropertyChangeType::Type InChangeType)
{
	if (Property)
	{
		if (InChangeType == EPropertyChangeType::Interactive || InChangeType == EPropertyChangeType::ValueSet)
		{
			if ((Property->GetFName() == GET_MEMBER_NAME_CHECKED(FSoundModulationMixValue, TargetValue))
				|| (Property->GetFName() == GET_MEMBER_NAME_CHECKED(FSoundControlBusMixStage, Bus)))
			{
				for (FSoundControlBusMixStage& Stage : MixStages)
				{
					if (Stage.Bus)
					{
						Stage.Value.TargetValue = FMath::Clamp(Stage.Value.TargetValue, 0.0f, 1.0f);
						float UnitValue = Stage.Value.TargetValue;
						if (USoundModulationParameter* Parameter = Stage.Bus->Parameter)
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

	AudioModulation::IterateModulationImpl([this](AudioModulation::FAudioModulation& OutModulation)
	{
		OutModulation.UpdateMix(*this);
	});
}
#endif // WITH_EDITOR

void USoundControlBusMix::SaveMixToProfile()
{
	const bool bSucceeded = AudioModulation::FProfileSerializer::Serialize(*this, ProfileIndex);
#if WITH_EDITOR
	if (bSucceeded)
	{
		FNotificationInfo Info(FText::Format(
			LOCTEXT("SoundControlBusMix_SaveSucceeded", "'Control Bus Mix '{0}' profile {1} saved successfully."),
			FText::FromName(GetFName()),
			FText::AsNumber(ProfileIndex)
		));
		Info.bFireAndForget = true;
		Info.ExpireDuration = 2.0f;
		Info.bUseThrobber = true;
		FSlateNotificationManager::Get().AddNotification(Info);
	}
#endif // WITH_EDITOR
}

void USoundControlBusMix::SoloMix()
{
	AudioModulation::IterateModulationImpl([this](AudioModulation::FAudioModulation& OutModulation)
	{
		OutModulation.SoloBusMix(*this);
	});
}

#undef LOCTEXT_NAMESPACE // AudioModulation

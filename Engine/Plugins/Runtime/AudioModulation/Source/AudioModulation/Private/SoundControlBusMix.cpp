// Copyright Epic Games, Inc. All Rights Reserved.
#include "SoundControlBusMix.h"

#include "Audio/AudioAddressPattern.h"
#include "AudioDevice.h"
#include "AudioModulation.h"
#include "AudioModulationLogging.h"
#include "AudioModulationProfileSerializer.h"
#include "AudioModulationStatics.h"
#include "AudioModulationSystem.h"
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

FSoundControlBusMixStage::FSoundControlBusMixStage(USoundControlBusBase* InBus, const float TargetValue)
	: Bus(InBus)
{
	if (Bus)
	{
		Value.TargetValue = FMath::Clamp(TargetValue, Bus->GetMin(), Bus->GetMax());
	}
	else
	{
		Value.TargetValue = FMath::Clamp(TargetValue, 0.0f, 1.0f);
	}
}

USoundControlBusMix::USoundControlBusMix(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
#if WITH_EDITORONLY_DATA
	, ProfileIndex(0)
#endif // WITH_EDITORONLY_DATA
{
}

void USoundControlBusMix::BeginDestroy()
{
	Super::BeginDestroy();

	if (UWorld* World = GetWorld())
	{
		if (FAudioDeviceHandle AudioDevice = World->GetAudioDevice())
		{
			if (AudioDevice->IsModulationPluginEnabled())
			{
				if (IAudioModulation* ModulationInterface = AudioDevice->ModulationInterface.Get())
				{
					auto ModSystem = static_cast<AudioModulation::FAudioModulation*>(ModulationInterface)->GetModulationSystem();
					check(ModSystem);
					ModSystem->DeactivateBusMix(*this);
				}
			}
		}
	}
}

#if WITH_EDITOR

void USoundControlBusMix::ActivateMix()
{
	AudioModulation::IterateModSystems([this](AudioModulation::FAudioModulationSystem& OutModSystem)
	{
		OutModSystem.ActivateBusMix(*this);
	});
}

void USoundControlBusMix::DeactivateMix()
{
	AudioModulation::IterateModSystems([this](AudioModulation::FAudioModulationSystem& OutModSystem)
	{
		OutModSystem.DeactivateBusMix(*this);
	});
}

void USoundControlBusMix::DeactivateAllMixes()
{
	AudioModulation::IterateModSystems([this](AudioModulation::FAudioModulationSystem& OutModSystem)
	{
		OutModSystem.DeactivateAllBusMixes();
	});
}

void USoundControlBusMix::LoadMixFromProfile()
{
	if (AudioModulation::FProfileSerializer::Deserialize(ProfileIndex, *this))
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
}

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
			if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(FSoundModulationValue, TargetValue))
			{
				for (FSoundControlBusMixStage& Stage : MixStages)
				{
					if (Stage.Bus)
					{
						Stage.Value.TargetValue = FMath::Clamp(Stage.Value.TargetValue, Stage.Bus->GetMin(), Stage.Bus->GetMax());
					}
				}
			}
		}
	}

	AudioModulation::IterateModSystems([this](AudioModulation::FAudioModulationSystem& OutModSystem)
	{
		OutModSystem.UpdateMix(*this);
	});
}

void USoundControlBusMix::SaveMixToProfile()
{
	if (AudioModulation::FProfileSerializer::Serialize(*this, ProfileIndex))
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
}

void USoundControlBusMix::SoloMix()
{
	AudioModulation::IterateModSystems([this](AudioModulation::FAudioModulationSystem& OutModSystem)
	{
		OutModSystem.SoloBusMix(*this);
	});
}
#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE // AudioModulation

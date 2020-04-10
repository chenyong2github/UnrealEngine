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

FSoundControlBusMixChannel::FSoundControlBusMixChannel()
	: Bus(nullptr)
{
}

FSoundControlBusMixChannel::FSoundControlBusMixChannel(USoundControlBusBase* InBus, const float TargetValue)
	: Bus(InBus)
{
	Value.TargetValue = FMath::Clamp(TargetValue, 0.0f, 1.0f);
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
			check(AudioDevice->IsModulationPluginEnabled());
			if (IAudioModulation* ModulationInterface = AudioDevice->ModulationInterface.Get())
			{
				auto ModSystem = static_cast<AudioModulation::FAudioModulation*>(ModulationInterface)->GetModulationSystem();
				check(ModSystem);
				ModSystem->DeactivateBusMix(*this);
			}
		}
	}
}

#if WITH_EDITOR
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
	if (FProperty* Property = InPropertyChangedEvent.Property)
	{
		if (!GEngine)
		{
			return;
		}

		FAudioDeviceManager* DeviceManager = GEngine->GetAudioDeviceManager();
		if (!DeviceManager)
		{
			return;
		}

		TArray<FAudioDevice*> Devices = DeviceManager->GetAudioDevices();
		for (FAudioDevice* Device : Devices)
		{
			if (Device && Device->IsModulationPluginEnabled() && Device->ModulationInterface.IsValid())
			{
				auto ModulationInterface = static_cast<AudioModulation::FAudioModulation*>(Device->ModulationInterface.Get());
				AudioModulation::FAudioModulationSystem* ModulationSystem = ModulationInterface->GetModulationSystem();
				check(ModulationSystem);

				ModulationSystem->UpdateMix(*this);
			}
		}
	}

	Super::PostEditChangeProperty(InPropertyChangedEvent);
}

void USoundControlBusMix::SaveMixToProfile()
{
	if (AudioModulation::FProfileSerializer::Serialize(*this, ProfileIndex))
	{
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
}
#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE // AudioModulation

// Copyright Epic Games, Inc. All Rights Reserved.
#include "SoundControlBusMix.h"

#include "Audio/AudioAddressPattern.h"
#include "AudioDevice.h"
#include "AudioModulation.h"
#include "AudioModulationInternal.h"
#include "AudioModulationLogging.h"
#include "AudioModulationProfileSerializer.h"
#include "AudioModulationStatics.h"
#include "Engine/World.h"

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
				auto ModulationImpl = static_cast<AudioModulation::FAudioModulation*>(ModulationInterface)->GetImpl();
				check(ModulationImpl);
				ModulationImpl->DeactivateBusMix(*this);
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

namespace AudioModulation
{
	FModulatorBusMixChannelProxy::FModulatorBusMixChannelProxy(const FSoundControlBusMixChannel& Channel, FAudioModulationImpl& InModulationImpl)
		: TModulatorProxyBase<FBusId>(Channel.Bus->GetName(), Channel.Bus->GetUniqueID())
		, Address(Channel.Bus->Address)
		, ClassId(Channel.Bus->GetClass()->GetUniqueID())
		, Value(Channel.Value)
		, BusHandle(FBusHandle::Create(*Channel.Bus, InModulationImpl.RefProxies.Buses, InModulationImpl))
	{
	}

	FModulatorBusMixProxy::FModulatorBusMixProxy(const USoundControlBusMix& InBusMix, FAudioModulationImpl& InModulationImpl)
		: TModulatorProxyRefType(InBusMix.GetName(), InBusMix.GetUniqueID(), InModulationImpl)
	{
		SetEnabled(InBusMix);
	}

	FModulatorBusMixProxy& FModulatorBusMixProxy::operator =(const USoundControlBusMix& InBusMix)
	{
		SetEnabled(InBusMix);

		return *this;
	}

	FModulatorBusMixProxy::EStatus FModulatorBusMixProxy::GetStatus() const
	{
		return Status;
	}

	void FModulatorBusMixProxy::Reset()
	{
		Channels.Reset();
	}

	void FModulatorBusMixProxy::SetEnabled(const USoundControlBusMix& InBusMix)
	{
		FChannelMap CachedChannels = Channels;
		Channels.Reset();

		Status = EStatus::Enabled;
		for (const FSoundControlBusMixChannel& Channel : InBusMix.Channels)
		{
			if (Channel.Bus)
			{
				auto BusId = static_cast<const FBusId>(Channel.Bus->GetUniqueID());

				check(ModulationImpl);
				FModulatorBusMixChannelProxy ChannelProxy(Channel, *ModulationImpl);

				if (const FModulatorBusMixChannelProxy* CachedChannel = CachedChannels.Find(BusId))
				{
					ChannelProxy.Value.SetCurrentValue(CachedChannel->Value.GetCurrentValue());
				}

				Channels.Emplace(BusId, ChannelProxy);
			}
			else
			{
				UE_LOG(LogAudioModulation, Warning,
					TEXT("USoundControlBusMix '%s' has channel with no bus specified. "
						"Mix instance initialized but channel ignored."),
					*InBusMix.GetFullName());
			}
		}
	}

	void FModulatorBusMixProxy::SetMix(const TArray<FSoundControlBusMixChannel>& InChannels)
	{
		for (const FSoundControlBusMixChannel& NewChannel : InChannels)
		{
			if(NewChannel.Bus)
			{
				FBusId BusId = static_cast<FBusId>(NewChannel.Bus->GetUniqueID());
				if (FModulatorBusMixChannelProxy* ChannelProxy = Channels.Find(BusId))
				{
					ChannelProxy->Value.TargetValue = NewChannel.Value.TargetValue;
					ChannelProxy->Value.AttackTime = NewChannel.Value.AttackTime;
					ChannelProxy->Value.ReleaseTime = NewChannel.Value.ReleaseTime;
				}
			}
		}
	}

	void FModulatorBusMixProxy::SetMixByFilter(const FString& InAddressFilter, uint32 InFilterClassId, const FSoundModulationValue& InValue)
	{
		static const uint32 BaseClassId = USoundControlBusBase::StaticClass()->GetUniqueID();

		for (TPair<FBusId, FModulatorBusMixChannelProxy>& IdProxyPair : Channels)
		{
			FModulatorBusMixChannelProxy& ChannelProxy = IdProxyPair.Value;
			if (InFilterClassId != BaseClassId && ChannelProxy.ClassId != InFilterClassId)
			{
				continue;
			}

			if (!FAudioAddressPattern::PartsMatch(InAddressFilter, ChannelProxy.Address))
			{
				continue;
			}

			ChannelProxy.Value.TargetValue = InValue.TargetValue;

			if (InValue.AttackTime >= 0.0f)
			{
				ChannelProxy.Value.AttackTime = InValue.AttackTime;
			}

			if (InValue.ReleaseTime >= 0.0f)
			{
				ChannelProxy.Value.ReleaseTime = InValue.ReleaseTime;
			}
		}
	}

	void FModulatorBusMixProxy::SetStopping()
	{
		if (Status == EStatus::Enabled)
		{
			Status = EStatus::Stopping;
		}
	}

	void FModulatorBusMixProxy::Update(float Elapsed, FBusProxyMap& ProxyMap)
	{
		bool bRequestStop = true;
		for (TPair<FBusId, FModulatorBusMixChannelProxy>& Channel : Channels)
		{
			FModulatorBusMixChannelProxy& ChannelProxy = Channel.Value;
			FSoundModulationValue& MixChannelValue = ChannelProxy.Value;

			if (FControlBusProxy* BusProxy = ProxyMap.Find(ChannelProxy.GetId()))
			{
				MixChannelValue.Update(Elapsed);

				const float CurrentValue = MixChannelValue.GetCurrentValue();
				if (Status == EStatus::Stopping)
				{
					MixChannelValue.TargetValue = BusProxy->GetDefaultValue();
					if (!FMath::IsNearlyEqual(MixChannelValue.TargetValue, CurrentValue))
					{
						bRequestStop = false;
					}
				}
				else
				{
					bRequestStop = false;
				}
				BusProxy->MixIn(CurrentValue);
			}
		}

		if (bRequestStop)
		{
			Status = EStatus::Stopped;
		}
	}
} // namespace AudioModulation

#undef LOCTEXT_NAMESPACE // AudioModulation

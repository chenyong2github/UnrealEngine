// Copyright Epic Games, Inc. All Rights Reserved.
#include "SoundControlBusMixProxy.h"

#include "Audio/AudioAddressPattern.h"
#include "AudioDevice.h"
#include "AudioModulation.h"
#include "AudioModulationLogging.h"
#include "AudioModulationProfileSerializer.h"
#include "AudioModulationStatics.h"
#include "AudioModulationSystem.h"
#include "SoundControlBus.h"
#include "SoundControlBusMix.h"

#define LOCTEXT_NAMESPACE "AudioModulation"


namespace AudioModulation
{
	const FBusMixId InvalidBusMixId = INDEX_NONE;

	FModulatorBusMixChannelSettings::FModulatorBusMixChannelSettings(const FSoundControlBusMixChannel& InChannel)
		: TModulatorBase<FBusId>(InChannel.Bus->GetName(), InChannel.Bus->GetUniqueID())
		, Address(InChannel.Bus->Address)
		, ClassId(InChannel.Bus->GetClass()->GetUniqueID())
		, Value(InChannel.Value)
		, BusSettings(FControlBusSettings(*InChannel.Bus))
	{
	}

	FModulatorBusMixChannelProxy::FModulatorBusMixChannelProxy(const FModulatorBusMixChannelSettings& InSettings, FAudioModulationSystem& OutModSystem)
		: TModulatorBase<FBusId>(InSettings.BusSettings.GetName(), InSettings.BusSettings.GetId())
		, Address(InSettings.Address)
		, ClassId(InSettings.ClassId)
		, Value(InSettings.Value)
		, BusHandle(FBusHandle::Create(InSettings.BusSettings, OutModSystem.RefProxies.Buses, OutModSystem))
	{
	}

	FModulatorBusMixSettings::FModulatorBusMixSettings(const USoundControlBusMix& InBusMix)
		: TModulatorBase<FBusMixId>(InBusMix.GetName(), InBusMix.GetUniqueID())
	{
		for (const FSoundControlBusMixChannel& Channel : InBusMix.Channels)
		{
			if (Channel.Bus)
			{
				Channels.Add(FModulatorBusMixChannelSettings(Channel));
			}
			else
			{
				UE_LOG(LogAudioModulation, Warning,
					TEXT("USoundControlBusMix '%s' has channel with no bus specified. "
						"Mix instance initialized with channel ignored."),
					*InBusMix.GetFullName());
			}
		}
	}

	FModulatorBusMixProxy::FModulatorBusMixProxy(const FModulatorBusMixSettings& InSettings, FAudioModulationSystem& OutModSystem)
		: TModulatorProxyRefType(InSettings.GetName(), InSettings.GetId(), OutModSystem)
	{
		SetEnabled(InSettings);
	}

	FModulatorBusMixProxy& FModulatorBusMixProxy::operator =(const FModulatorBusMixSettings& InSettings)
	{
		SetEnabled(InSettings);

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

	void FModulatorBusMixProxy::SetEnabled(const FModulatorBusMixSettings& InSettings)
	{
		check(ModSystem);

		// Cache channels to avoid releasing channel state (and potentially referenced bus state) when re-enabling
		FChannelMap CachedChannels = Channels;

		Status = EStatus::Enabled;
		Channels.Reset();
		for (const FModulatorBusMixChannelSettings& ChannelSettings : InSettings.Channels)
		{
			FModulatorBusMixChannelProxy ChannelProxy(ChannelSettings, *ModSystem);

			const FBusId BusId = ChannelSettings.GetId();
			if (const FModulatorBusMixChannelProxy* CachedChannel = CachedChannels.Find(BusId))
			{
				ChannelProxy.Value.SetCurrentValue(CachedChannel->Value.GetCurrentValue());
			}

			Channels.Emplace(BusId, ChannelProxy);
		}
	}

	void FModulatorBusMixProxy::SetMix(const TArray<FModulatorBusMixChannelSettings>& InChannels)
	{
		for (const FModulatorBusMixChannelSettings& NewChannel : InChannels)
		{
			const FBusId BusId = NewChannel.GetId();
			if (FModulatorBusMixChannelProxy* ChannelProxy = Channels.Find(BusId))
			{
				ChannelProxy->Value.TargetValue = NewChannel.Value.TargetValue;
				ChannelProxy->Value.AttackTime = NewChannel.Value.AttackTime;
				ChannelProxy->Value.ReleaseTime = NewChannel.Value.ReleaseTime;
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

	void FModulatorBusMixProxy::Update(const double InElapsed, FBusProxyMap& OutProxyMap)
	{
		bool bRequestStop = true;
		for (TPair<FBusId, FModulatorBusMixChannelProxy>& Channel : Channels)
		{
			FModulatorBusMixChannelProxy& ChannelProxy = Channel.Value;
			FSoundModulationValue& MixChannelValue = ChannelProxy.Value;

			if (FControlBusProxy* BusProxy = OutProxyMap.Find(ChannelProxy.GetId()))
			{
				MixChannelValue.Update(InElapsed);

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

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

#define LOCTEXT_NAMESPACE "AudioModulation"


namespace AudioModulation
{
	const FBusMixId InvalidBusMixId = INDEX_NONE;

	FModulatorBusMixChannelProxy::FModulatorBusMixChannelProxy(const FSoundControlBusMixChannel& Channel, FAudioModulationSystem& InModSystem)
		: TModulatorProxyBase<FBusId>(Channel.Bus->GetName(), Channel.Bus->GetUniqueID())
		, Address(Channel.Bus->Address)
		, ClassId(Channel.Bus->GetClass()->GetUniqueID())
		, Value(Channel.Value)
		, BusHandle(FBusHandle::Create(*Channel.Bus, InModSystem.RefProxies.Buses, InModSystem))
	{
	}

	FModulatorBusMixProxy::FModulatorBusMixProxy(const USoundControlBusMix& InBusMix, FAudioModulationSystem& InModSystem)
		: TModulatorProxyRefType(InBusMix.GetName(), InBusMix.GetUniqueID(), InModSystem)
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

				check(ModSystem);
				FModulatorBusMixChannelProxy ChannelProxy(Channel, *ModSystem);

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

// Copyright Epic Games, Inc. All Rights Reserved.
#include "SoundControlBusMix.h"

#include "Audio/AudioAddressPattern.h"
#include "AudioDevice.h"
#include "AudioModulation.h"
#include "AudioModulationInternal.h"
#include "AudioModulationLogging.h"
#include "Engine/World.h"


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
{
}

void USoundControlBusMix::BeginDestroy()
{
	Super::BeginDestroy();

	if (UWorld* World = GetWorld())
	{
		if (FAudioDevice* AudioDevice = World->GetAudioDevice())
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

namespace AudioModulation
{
	FModulatorBusMixChannelProxy::FModulatorBusMixChannelProxy(const FSoundControlBusMixChannel& Channel)
		: TModulatorProxyBase<FBusId>(Channel.Bus->GetName(), Channel.Bus->GetUniqueID())
		, Address(Channel.Bus->Address)
		, ClassId(Channel.Bus->GetClass()->GetUniqueID())
		, Value(Channel.Value)
	{
	}

	FModulatorBusMixProxy::FModulatorBusMixProxy(const USoundControlBusMix& InBusMix)
		: TModulatorProxyRefType(InBusMix.GetName(), InBusMix.GetUniqueID())
	{
		Init(InBusMix);
	}

	FModulatorBusMixProxy& FModulatorBusMixProxy::operator =(const USoundControlBusMix& InBusMix)
	{
		Init(InBusMix);

		return *this;
	}

	bool FModulatorBusMixProxy::CanDestroy() const
	{
		return Status == BusMixStatus::Stopped;
	}

	void FModulatorBusMixProxy::Init(const USoundControlBusMix& InBusMix)
	{
		Channels.Reset();

		Status = BusMixStatus::Enabled;
		for (const FSoundControlBusMixChannel& Channel : InBusMix.Channels)
		{
			if (Channel.Bus)
			{
				auto BusId = static_cast<const AudioModulation::FBusId>(Channel.Bus->GetUniqueID());
#if !UE_BUILD_SHIPPING
				if (Channels.Contains(BusId))
				{
					UE_LOG(LogAudioModulation, Warning,
						TEXT("USoundControlBusMix '%s' already contains bus '%s'. Only one representative channel for this bus added."),
						*InBusMix.GetFullName(), *Channel.Bus->GetFullName());
				}
#endif // UE_BUILD_SHIPPING
				Channels.Emplace(BusId, FModulatorBusMixChannelProxy(Channel));
			}
			else
			{
				UE_LOG(LogAudioModulation, Warning,
					TEXT("USoundControlBusMix '%s' has channel with no bus specified. "
						"Mix activated but channel ignored."),
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

	void FModulatorBusMixProxy::SetEnabled()
	{
		Status = BusMixStatus::Enabled;
	}

	void FModulatorBusMixProxy::SetStopping()
	{
		if (Status == BusMixStatus::Enabled)
		{
			Status = BusMixStatus::Stopping;
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
				if (Status == BusMixStatus::Stopping)
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
			Status = BusMixStatus::Stopped;
		}
	}
} // namespace AudioModulation

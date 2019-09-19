// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "SoundControlBusMix.h"

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

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FAudioDevice* AudioDevice = World->GetAudioDevice();
	if (!AudioDevice)
	{
		return;
	}

	check(AudioDevice->IsModulationPluginEnabled());
	if (IAudioModulation* ModulationInterface = AudioDevice->ModulationInterface.Get())
	{
		auto ModulationImpl = static_cast<AudioModulation::FAudioModulation*>(ModulationInterface)->GetImpl();
		check(ModulationImpl);

		auto BusMixId = static_cast<const AudioModulation::FBusMixId>(GetUniqueID());
		ModulationImpl->DeactivateBusMix(BusMixId);
	}
}

namespace AudioModulation
{
	FModulatorBusMixChannelProxy::FModulatorBusMixChannelProxy(const FSoundControlBusMixChannel& Channel)
		: TModulatorProxyBase<FBusId>(Channel.Bus->GetName(), Channel.Bus->GetUniqueID())
		, Value(Channel.Value)
	{
	}

	FModulatorBusMixProxy::FModulatorBusMixProxy(const USoundControlBusMix& Mix)
		: TModulatorProxyRefBase<FBusMixId>(Mix.GetName(), Mix.GetUniqueID(), Mix.bAutoActivate)
		, Status(BusMixStatus::Enabled)
	{
		for (const FSoundControlBusMixChannel& Channel : Mix.Channels)
		{
			if (Channel.Bus)
			{
				auto BusId = static_cast<const AudioModulation::FBusId>(Channel.Bus->GetUniqueID());
#if !UE_BUILD_SHIPPING
				if (Channels.Contains(BusId))
				{
					UE_LOG(LogAudioModulation, Warning,
						TEXT("USoundControlBusMix '%s' already contains bus '%s'. Only one representative channel for this bus added."),
						*Mix.GetFullName(), *Channel.Bus->GetFullName());
				}
#endif // UE_BUILD_SHIPPING
				Channels.Emplace(BusId, FModulatorBusMixChannelProxy(Channel));
			}
			else
			{
				UE_LOG(LogAudioModulation, Warning,
					TEXT("USoundControlBusMix '%s' has channel with no bus specified. "
					"Mix activated but channel ignored."),
					*Mix.GetFullName());
			}
		}
	}

	bool FModulatorBusMixProxy::CanDestroy() const
	{
		if (Status != BusMixStatus::Stopped)
		{
			return false;
		}

		return TModulatorProxyRefBase<FBusMixId>::CanDestroy();
	}

	void FModulatorBusMixProxy::OnUpdateProxy(const USoundModulatorBase& InModulatorArchetype)
	{
		const FModulatorBusMixProxy CopyProxy(*CastChecked<USoundControlBusMix>(&InModulatorArchetype));
		auto UpdateProxy = [this, CopyProxy]()
		{
			check(IsInAudioThread());

			Channels = CopyProxy.Channels;
		};

		IsInAudioThread() ? UpdateProxy() : FAudioThread::RunCommandOnAudioThread(UpdateProxy);
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

	void FModulatorBusMixProxy::Update(float Elapsed, BusProxyMap& ProxyMap)
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

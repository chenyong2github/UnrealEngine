// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "SoundModulatorBusMix.h"

#include "AudioDevice.h"
#include "AudioModulation.h"
#include "AudioModulationInternal.h"
#include "AudioModulationLogging.h"
#include "Engine/World.h"


FSoundModulatorBusMixChannel::FSoundModulatorBusMixChannel()
	: Bus(nullptr)
{
}

FSoundModulatorBusMixChannel::FSoundModulatorBusMixChannel(USoundModulatorBusBase* InBus, const float TargetValue)
	: Bus(InBus)
{
	Value.TargetValue = FMath::Clamp(TargetValue, 0.0f, 1.0f);
}

USoundModulatorBusMix::USoundModulatorBusMix(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void USoundModulatorBusMix::BeginDestroy()
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

		auto BusMixId = static_cast<const AudioModulation::BusMixId>(GetUniqueID());
		ModulationImpl->DeactivateBusMix(BusMixId);
	}
}

namespace AudioModulation
{
	FModulatorBusMixChannelProxy::FModulatorBusMixChannelProxy(const FSoundModulatorBusMixChannel& Channel)
		: Value(Channel.Value)
	{
		check(Channel.Bus);
		BusId = static_cast<BusMixId>(Channel.Bus->GetUniqueID());

#if !UE_BUILD_SHIPPING
		Name = Channel.Bus->GetName();
#endif // !UE_BUILD_SHIPPING
	}

	FModulatorBusMixProxy::FModulatorBusMixProxy(const USoundModulatorBusMix& Mix)
		: Status(BusMixStatus::Enabled)
		, SoundRefCount(0)
		, bAutoActivate(0)
#if UE_BUILD_SHIPPING
		, Name(Mix.GetName())
#endif // UE_BUILD_SHIPPING
	{
		for (const FSoundModulatorBusMixChannel& Channel : Mix.Channels)
		{
			if (Channel.Bus)
			{
				auto BusMixId = static_cast<const AudioModulation::BusMixId>(Channel.Bus->GetUniqueID());
#if !UE_BUILD_SHIPPING
				if (Channels.Contains(BusMixId))
				{
					UE_LOG(LogAudioModulation, Warning,
						TEXT("USoundModulatorBusMix '%s' already contains bus '%s'. Only one representative channel for this bus added."),
						*Mix.GetFullName(), *Channel.Bus->GetFullName());
				}
#endif // UE_BUILD_SHIPPING
				Channels.Emplace(BusMixId, FModulatorBusMixChannelProxy(Channel));
			}
			else
			{
				UE_LOG(LogAudioModulation, Warning,
					TEXT("USoundModulatorBusMix '%s' has channel with no bus specified. "
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

		return !bAutoActivate || (bAutoActivate && SoundRefCount == 0);
	}

	bool FModulatorBusMixProxy::GetAutoActivate() const
	{
		return bAutoActivate;
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
		for (TPair<BusId, FModulatorBusMixChannelProxy>& Channel : Channels)
		{
			FModulatorBusMixChannelProxy& ChannelProxy = Channel.Value;
			FSoundModulationValue& MixChannelValue = ChannelProxy.Value;

			if (FModulatorBusProxy* BusProxy = ProxyMap.Find(ChannelProxy.BusId))
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

#if !UE_BUILD_SHIPPING
	const FString& FModulatorBusMixProxy::GetName() const
	{
		return Name;
	}
#endif // !UE_BUILD_SHIPPING

	int32 FModulatorBusMixProxy::DecRefSound()
	{
		check(SoundRefCount > 0);
		return SoundRefCount--;
	}

	int32 FModulatorBusMixProxy::IncRefSound()
	{
		return SoundRefCount++;
	}
} // namespace AudioModulation

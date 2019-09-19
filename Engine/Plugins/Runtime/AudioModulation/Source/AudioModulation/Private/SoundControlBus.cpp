// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "SoundControlBus.h"

#include "AudioDevice.h"
#include "AudioDeviceManager.h"
#include "AudioModulation.h"
#include "AudioModulationInternal.h"
#include "AudioModulationLogging.h"
#include "Engine/World.h"
#include "SoundModulatorLFO.h"


USoundControlBusBase::USoundControlBusBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, DefaultValue(1.0f)
	, Min(0.0f)
	, Max(1.0f)
{
}

USoundVolumeControlBus::USoundVolumeControlBus(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

USoundPitchControlBus::USoundPitchControlBus(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

USoundLPFControlBus::USoundLPFControlBus(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

USoundHPFControlBus::USoundHPFControlBus(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DefaultValue = 0.0f;
}

void USoundControlBusBase::BeginDestroy()
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

		auto BusId = static_cast<const AudioModulation::FBusId>(GetUniqueID());
		ModulationImpl->DeactivateBus(BusId);
	}
}

namespace AudioModulation
{
	FControlBusProxy::FControlBusProxy()
		: TModulatorProxyRefBase<FBusId>()
		, DefaultValue(0.0f)
		, LFOValue(1.0f)
		, MixValue(NAN)
		, Operator(ESoundModulatorOperator::Multiply)
		, Range(0.0f, 1.0f)
	{
	}

	FControlBusProxy::FControlBusProxy(const USoundControlBusBase& Bus)
		: TModulatorProxyRefBase<FBusId>(Bus.GetName(), Bus.GetUniqueID(), Bus.bAutoActivate)
		, DefaultValue(Bus.DefaultValue)
		, LFOValue(1.0f)
		, MixValue(NAN)
		, Operator(Bus.GetOperator())
		, Range(Bus.Min, Bus.Max)
	{
		if (Bus.Min > Bus.Max)
		{
			Range.X = Bus.Max;
			Range.Y = Bus.Min;
		}

		DefaultValue = FMath::Clamp(DefaultValue, Range.X, Range.Y);

		for (const USoundBusModulatorBase* Modulator: Bus.Modulators)
		{
			if (const USoundBusModulatorLFO* LFO = Cast<USoundBusModulatorLFO>(Modulator))
			{
				const FLFOId RefLFOId = static_cast<FLFOId>(LFO->GetUniqueID());
				LFOIds.Add(RefLFOId);
			}
		}
	}

	float FControlBusProxy::GetDefaultValue() const
	{
		return DefaultValue;
	}

	const TArray<FLFOId>& FControlBusProxy::GetLFOIds() const
	{
		return LFOIds;
	}

	float FControlBusProxy::GetLFOValue() const
	{
		return LFOValue;
	}

	FVector2D FControlBusProxy::GetRange() const
	{
		return Range;
	}

	float FControlBusProxy::GetMixValue() const
	{
		return MixValue;
	}

	float FControlBusProxy::GetValue() const
	{
		const float DefaultMixed = Mix(DefaultValue);
		return FMath::Clamp(DefaultMixed * LFOValue, Range.X, Range.Y);
	}

	float FControlBusProxy::Mix(float ValueA) const
	{
		// If mix value is NaN, it is uninitialized (effectively, the parent bus is inactive)
		// and therefore not mixable, so just return the second value.
		if (FMath::IsNaN(MixValue))
		{
			return ValueA;
		}
		return Mix(MixValue, ValueA);
	}
		
	float FControlBusProxy::Mix(float ValueA, float ValueB) const
	{
		switch (Operator)
		{
			case ESoundModulatorOperator::Min:
			{
				return FMath::Min(ValueA, ValueB);
			}
			break;

			case ESoundModulatorOperator::Max:
			{
				return FMath::Max(ValueA, ValueB);
			}
			break;

			case ESoundModulatorOperator::Multiply:
			{
				return ValueA * ValueB;
			}
			break;

			default:
			{
				static_assert(static_cast<int32>(ESoundModulatorOperator::Count) == 3, "Possible missing ESoundModulationOperator case coverage");
			}
			break;
		}

		check(false);
		return NAN;
	}

	void FControlBusProxy::MixIn(const float InValue)
	{
		MixValue = Mix(InValue);
	}

	void FControlBusProxy::MixLFO(LFOProxyMap& LFOMap)
	{
		for (const FLFOId& LFOId : LFOIds)
		{
			if (FModulatorLFOProxy* LFOProxy = LFOMap.Find(LFOId))
			{
				LFOValue *= LFOProxy->GetValue();
			}
		}
	}

	void FControlBusProxy::OnUpdateProxy(const USoundModulatorBase& InModulatorArchetype)
	{
		const FControlBusProxy CopyProxy(*CastChecked<USoundControlBusBase>(&InModulatorArchetype));
		auto UpdateProxy = [this, CopyProxy]()
		{
			// LFOIds cannot be updated as this would swap sounds subscribing to LFOs if set to AutoActivate
			DefaultValue = FMath::Clamp(CopyProxy.DefaultValue, CopyProxy.Range.GetMin(), CopyProxy.Range.GetMax());
			Operator = CopyProxy.Operator;
			Range = CopyProxy.Range;
		};

		IsInAudioThread() ? UpdateProxy() : FAudioThread::RunCommandOnAudioThread(UpdateProxy);
	}

	void FControlBusProxy::Reset()
	{
		LFOValue = 1.0f;
		MixValue = NAN;
	}
} // namespace AudioModulation

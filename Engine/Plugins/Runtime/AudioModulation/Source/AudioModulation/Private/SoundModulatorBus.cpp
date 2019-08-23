// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "SoundModulatorBus.h"

#include "AudioDevice.h"
#include "AudioDeviceManager.h"
#include "AudioModulation.h"
#include "AudioModulationInternal.h"
#include "AudioModulationLogging.h"
#include "Engine/World.h"
#include "SoundModulatorLFO.h"


USoundModulatorBusBase::USoundModulatorBusBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bAutoActivate(0)
	, bAutoDeactivate(0)
	, DefaultValue(1.0f)
	, Min(0.0f)
	, Max(1.0f)
{
}

#if WITH_EDITOR
void USoundModulatorBusBase::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	FAudioDeviceManager* DeviceManager = GEngine->GetAudioDeviceManager();
	if (!DeviceManager || !PropertyChangedEvent.Property)
	{
		return;
	}

	const TArray<FAudioDevice*>& Devices = DeviceManager->GetAudioDevices();
	for (FAudioDevice* Device : Devices)
	{
		if (!Device || !Device->IsModulationPluginEnabled() || !Device->ModulationInterface.IsValid())
		{
			continue;
		}

		auto Impl = static_cast<AudioModulation::FAudioModulation*>(Device->ModulationInterface.Get())->GetImpl();

		FName Name = PropertyChangedEvent.Property->GetFName();
		if (Name == TEXT("DefaultValue"))
		{
			Impl->SetBusDefault(*this, DefaultValue);
		}
		else if (Name == TEXT("Min") || Name == TEXT("Max"))
		{
			Impl->SetBusRange(*this, FVector2D(Min, Max));
		}
		else if (Name == TEXT("Modulators"))
		{
			const AudioModulation::BusId BusId = static_cast<AudioModulation::BusId>(GetUniqueID());
			if (Impl->IsBusActive(BusId))
			{
				Impl->DeactivateBus(BusId);
				Impl->ActivateBus(*this);
			}
		}
	}
}
#endif // WITH_EDITOR

USoundVolumeModulatorBus::USoundVolumeModulatorBus(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

USoundPitchModulatorBus::USoundPitchModulatorBus(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

USoundLPFModulatorBus::USoundLPFModulatorBus(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

USoundHPFModulatorBus::USoundHPFModulatorBus(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void USoundModulatorBusBase::BeginDestroy()
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

		auto BusId = static_cast<const AudioModulation::BusId>(GetUniqueID());
		ModulationImpl->DeactivateBus(BusId);
	}
}

namespace AudioModulation
{
	FModulatorBusProxy::FModulatorBusProxy()
		: BusId(0)
		, DefaultValue(0.0f)
		, LFOValue(1.0f)
		, MixValue(NAN)
		, Operator(ESoundModulatorOperator::Multiply)
		, Range(0.0f, 1.0f)
		, bAutoDeactivate(false) 
	{
	}

	FModulatorBusProxy::FModulatorBusProxy(const USoundModulatorBusBase& Bus)
		: BusId(static_cast<AudioModulation::BusId>(Bus.GetUniqueID()))
#if !UE_BUILD_SHIPPING
		, Name(Bus.GetName())
#endif // !UE_BUILD_SHIPPING
		, DefaultValue(Bus.DefaultValue)
		, LFOValue(1.0f)
		, MixValue(NAN)
		, Operator(Bus.GetOperator())
		, Range(Bus.Min, Bus.Max)
		, bAutoDeactivate(Bus.bAutoDeactivate)
	{
		if (Bus.Min > Bus.Max)
		{
			Range.X = Bus.Max;
			Range.Y = Bus.Min;
		}

		DefaultValue = FMath::Clamp(DefaultValue, Range.X, Range.Y);

		for (const USoundModulatorBase* Modulator: Bus.Modulators)
		{
			if (const USoundModulatorLFO* LFO = Cast<USoundModulatorLFO>(Modulator))
			{
				LFOId Id = static_cast<LFOId>(LFO->GetUniqueID());
				LFOIds.Add(Id);
			}
		}
	}

	bool FModulatorBusProxy::CanDeactivate() const
	{
		return bAutoDeactivate;
	}

	AudioModulation::BusId FModulatorBusProxy::GetBusId() const
	{
		return BusId;
	}

	float FModulatorBusProxy::GetDefaultValue() const
	{
		return DefaultValue;
	}

	float FModulatorBusProxy::GetLFOValue() const
	{
		return LFOValue;
	}

	FVector2D FModulatorBusProxy::GetRange() const
	{
		return Range;
	}

	float FModulatorBusProxy::GetMixValue() const
	{
		return MixValue;
	}

#if !UE_BUILD_SHIPPING
	const FString& FModulatorBusProxy::GetName() const
	{
		return Name;
	}
#endif // !UE_BUILD_SHIPPING

	float FModulatorBusProxy::GetValue() const
	{
		const float DefaultMixed = Mix(DefaultValue);
		return FMath::Clamp(Mix(DefaultMixed, LFOValue), Range.X, Range.Y);
	}

	float FModulatorBusProxy::Mix(float ValueA) const
	{
		// If mix value is NaN, it is uninitialized (effectively, the parent bus is inactive)
		// and therefore not mixable, so just return the second value.
		if (FMath::IsNaN(MixValue))
		{
			return ValueA;
		}
		return Mix(MixValue, ValueA);
	}
		
	float FModulatorBusProxy::Mix(float ValueA, float ValueB) const
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
				static_assert(static_cast<int32>(ESoundModulatorOperator::Count) == 3, "Missing mod operator case coverage");
			}
			break;
		}

		check(false);
		return NAN;
	}

	void FModulatorBusProxy::MixIn(const float InValue)
	{
		MixValue = Mix(InValue);
	}

	void FModulatorBusProxy::MixLFO(LFOProxyMap& LFOMap)
	{
		for (const LFOId& LFOId : LFOIds)
		{
			if (FModulatorLFOProxy* LFOProxy = LFOMap.Find(LFOId))
			{
				LFOProxy->SetIsActive();
				LFOValue *= LFOProxy->GetValue();
			}
		}
	}

	void FModulatorBusProxy::Reset()
	{
		LFOValue = 1.0f;
		MixValue = NAN;
	}

	void FModulatorBusProxy::SetDefaultValue(const float Value)
	{
		DefaultValue = FMath::Clamp(Value, Range.X, Range.Y);
	}

	void FModulatorBusProxy::SetRange(const FVector2D& InRange)
	{
		Range = InRange;
	}
} // namespace AudioModulation

// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "SoundControlBus.h"

#include "AudioDevice.h"
#include "AudioDeviceManager.h"
#include "AudioModulation.h"
#include "AudioModulationInternal.h"
#include "AudioModulationLogging.h"
#include "Engine/World.h"
#include "SoundModulatorLFO.h"
#include "SoundModulationProxy.h"

USoundControlBusBase::USoundControlBusBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bBypass(false)
	, DefaultValue(1.0f)
	, Min(0.0f)
	, Max(1.0f)
#if WITH_EDITORONLY_DATA
	, bOverrideAddress(false)
#endif
{
}

#if WITH_EDITOR
void USoundControlBusBase::PostDuplicate(EDuplicateMode::Type DuplicateMode)
{
	if (!bOverrideAddress)
	{
		Address = GetName();
	}

	Super::PostDuplicate(DuplicateMode);
}

void USoundControlBusBase::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (FProperty* Property = PropertyChangedEvent.Property)
	{
		if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(USoundControlBusBase, bOverrideAddress) && !bOverrideAddress)
		{
			Address = GetName();
		}

		if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(USoundControlBusBase, DefaultValue)
			|| Property->GetFName() == GET_MEMBER_NAME_CHECKED(USoundControlBusBase, Min)
			|| Property->GetFName() == GET_MEMBER_NAME_CHECKED(USoundControlBusBase, Max))
		{
			Min = FMath::Min(Min, Max);
			DefaultValue = FMath::Clamp(DefaultValue, Min, Max);
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void USoundControlBusBase::PostInitProperties()
{
	if (!bOverrideAddress)
	{
		Address = GetName();
	}

	Super::PostInitProperties();
}

void USoundControlBusBase::PostRename(UObject* OldOuter, const FName OldName)
{
	if (!bOverrideAddress)
	{
		Address = GetName();
	}
}
#endif // WITH_EDITOR

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

USoundControlBus::USoundControlBus(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void USoundControlBusBase::BeginDestroy()
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
				ModulationImpl->DeactivateBus(*this);
			}
		}
	}
}

namespace AudioModulation
{
	FControlBusProxy::FControlBusProxy()
		: DefaultValue(0.0f)
		, LFOValue(1.0f)
		, MixValue(NAN)
		, bBypass(false)
		, Operator(ESoundModulatorOperator::Multiply)
		, Range(0.0f, 1.0f)
	{
	}

	FControlBusProxy::FControlBusProxy(const USoundControlBusBase& InBus)
		: TModulatorProxyRefType(InBus.GetName(), InBus.GetUniqueID())
	{
		Init(InBus);
	}

	FControlBusProxy& FControlBusProxy::operator =(const USoundControlBusBase& InBus)
	{
		Init(InBus);
		return *this;
	}

	float FControlBusProxy::GetDefaultValue() const
	{
		return DefaultValue;
	}

	const TArray<FLFOHandle>& FControlBusProxy::GetLFOHandles() const
	{
		return LFOHandles;
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

	void FControlBusProxy::Init(const USoundControlBusBase& InBus)
	{
		DefaultValue = InBus.DefaultValue;
		LFOValue = 1.0f;
		MixValue = NAN;
		Operator = InBus.GetOperator();
		Range = FVector2D(InBus.Min, InBus.Max);
		if (InBus.Min > InBus.Max)
		{
			Range.X = InBus.Max;
			Range.Y = InBus.Min;
		}

		DefaultValue = FMath::Clamp(DefaultValue, Range.X, Range.Y);
		bBypass = InBus.bBypass;
	}

	void FControlBusProxy::InitLFOs(const USoundControlBusBase& InBus, FLFOProxyMap& OutActiveLFOs)
	{
		LFOHandles.Reset();

		for (const USoundBusModulatorBase* Modulator : InBus.Modulators)
		{
			if (const USoundBusModulatorLFO* LFO = Cast<USoundBusModulatorLFO>(Modulator))
			{
				LFOHandles.Add(FLFOHandle::Create(*LFO, OutActiveLFOs));
			}
		}
	}

	bool FControlBusProxy::IsBypassed() const
	{
		return bBypass;
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

	void FControlBusProxy::MixLFO()
	{
		for (const FLFOHandle& Handle: LFOHandles)
		{
			if (Handle.IsValid())
			{
				const FModulatorLFOProxy& LFOProxy = Handle.FindProxy();
				if (!LFOProxy.IsBypassed())
				{
					LFOValue *= LFOProxy.GetValue();
				}
			}
		}
	}

	void FControlBusProxy::Reset()
	{
		LFOValue = 1.0f;
		MixValue = NAN;
	}
} // namespace AudioModulation

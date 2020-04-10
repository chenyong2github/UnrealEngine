// Copyright Epic Games, Inc. All Rights Reserved.
#include "SoundControlBusProxy.h"

#include "AudioDevice.h"
#include "AudioDeviceManager.h"
#include "AudioModulation.h"
#include "AudioModulationLogging.h"
#include "AudioModulationSystem.h"
#include "Engine/World.h"
#include "SoundModulatorLFO.h"


namespace AudioModulation
{
	const FBusId InvalidBusId = INDEX_NONE;

	FControlBusProxy::FControlBusProxy()
		: DefaultValue(0.0f)
		, LFOValue(1.0f)
		, MixValue(NAN)
		, bBypass(false)
		, Operator(ESoundModulatorOperator::Multiply)
		, Range(0.0f, 1.0f)
	{
	}

	FControlBusProxy::FControlBusProxy(const USoundControlBusBase& InBus, FAudioModulationSystem& InModSystem)
		: TModulatorProxyRefType(InBus.GetName(), InBus.GetUniqueID(), InModSystem)
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

	void FControlBusProxy::InitLFOs(const USoundControlBusBase& InBus)
	{
		TArray<FLFOHandle> NewHandles;

		for (const USoundBusModulatorBase* Modulator : InBus.Modulators)
		{
			if (const USoundBusModulatorLFO* LFO = Cast<USoundBusModulatorLFO>(Modulator))
			{
				check(ModSystem);
				NewHandles.Add(FLFOHandle::Create(*LFO, ModSystem->RefProxies.LFOs, *ModSystem));
			}
		}

		// Move vs. reset and adding to original array to avoid potentially clearing handles (and thus current LFO state)
		// and destroying LFOs if function is called while reinitializing/updating the modulator
		LFOHandles = MoveTemp(NewHandles);
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
		return SoundModulatorOperator::Apply(Operator, MixValue, ValueA);
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

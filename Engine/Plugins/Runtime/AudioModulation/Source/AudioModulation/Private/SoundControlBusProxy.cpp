// Copyright Epic Games, Inc. All Rights Reserved.
#include "SoundControlBusProxy.h"

#include "AudioDevice.h"
#include "AudioDeviceManager.h"
#include "AudioModulation.h"
#include "AudioModulationLogging.h"
#include "AudioModulationSystem.h"
#include "Engine/World.h"
#include "SoundModulationGeneratorLFO.h"


namespace AudioModulation
{
	const FBusId InvalidBusId = INDEX_NONE;

	FControlBusProxy::FControlBusProxy()
		: DefaultValue(0.0f)
		, LFOValue(1.0f)
		, MixValue(NAN)
		, bBypass(false)
	{
	}

	FControlBusProxy::FControlBusProxy(const FControlBusSettings& InSettings, FAudioModulationSystem& InModSystem)
		: TModulatorProxyRefType(InSettings.GetName(), InSettings.GetId(), InModSystem)
	{
		Init(InSettings);
	}

	FControlBusProxy& FControlBusProxy::operator =(const FControlBusSettings& InSettings)
	{
		Init(InSettings);
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

	float FControlBusProxy::GetMixValue() const
	{
		return MixValue;
	}

	float FControlBusProxy::GetValue() const
	{
		const float DefaultMixed = Mix(DefaultValue);
		return FMath::Clamp(DefaultMixed * LFOValue, 0.0f, 1.0f);
	}

	void FControlBusProxy::Init(const FControlBusSettings& InSettings)
	{
		check(ModSystem);

		LFOValue = 1.0f;
		MixValue = NAN;
		MixFunction = InSettings.MixFunction;

		DefaultValue = FMath::Clamp(InSettings.DefaultValue, 0.0f, 1.0f);
		bBypass = InSettings.bBypass;

		TArray<FLFOHandle> NewHandles;
		for (const FModulatorLFOSettings& LFOSettings : InSettings.LFOSettings)
		{
			NewHandles.Add(FLFOHandle::Create(LFOSettings, ModSystem->RefProxies.LFOs, *ModSystem));
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

		float OutValue = MixValue;
		MixFunction(&OutValue, &ValueA, 1);
		return OutValue;
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

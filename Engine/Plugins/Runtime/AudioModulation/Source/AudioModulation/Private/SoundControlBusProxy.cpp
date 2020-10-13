// Copyright Epic Games, Inc. All Rights Reserved.
#include "SoundControlBusProxy.h"

#include "AudioDevice.h"
#include "AudioDeviceManager.h"
#include "AudioModulation.h"
#include "AudioModulationLogging.h"
#include "AudioModulationSystem.h"
#include "Engine/World.h"
#include "SoundModulationGenerator.h"


namespace AudioModulation
{
	const FBusId InvalidBusId = INDEX_NONE;

	FControlBusProxy::FControlBusProxy()
		: DefaultValue(0.0f)
		, GeneratorValue(1.0f)
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

	const TArray<FGeneratorHandle>& FControlBusProxy::GetGeneratorHandles() const
	{
		return GeneratorHandles;
	}

	float FControlBusProxy::GetGeneratorValue() const
	{
		return GeneratorValue;
	}

	float FControlBusProxy::GetMixValue() const
	{
		return MixValue;
	}

	float FControlBusProxy::GetValue() const
	{
		const float DefaultMixed = Mix(DefaultValue);
		return FMath::Clamp(DefaultMixed * GeneratorValue, 0.0f, 1.0f);
	}

	void FControlBusProxy::Init(const FControlBusSettings& InSettings)
	{
		check(ModSystem);

		GeneratorValue = 1.0f;
		MixValue = NAN;
		MixFunction = InSettings.MixFunction;

		DefaultValue = FMath::Clamp(InSettings.DefaultValue, 0.0f, 1.0f);
		bBypass = InSettings.bBypass;

		TArray<FGeneratorHandle> NewHandles;
		for (const FModulationGeneratorSettings& GeneratorSettings : InSettings.GeneratorSettings)
		{
			NewHandles.Add(FGeneratorHandle::Create(GeneratorSettings, ModSystem->RefProxies.Generators, *ModSystem));
		}

		// Move vs. reset and adding to original array to avoid potentially clearing handles (and thus current Generator state)
		// and destroying generators if function is called while reinitializing/updating the modulator
		GeneratorHandles = MoveTemp(NewHandles);
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

	void FControlBusProxy::MixGenerators()
	{
		for (const FGeneratorHandle& Handle: GeneratorHandles)
		{
			if (Handle.IsValid())
			{
				const FModulatorGeneratorProxy& GeneratorProxy = Handle.FindProxy();
				if (!GeneratorProxy.IsBypassed())
				{
					GeneratorValue *= GeneratorProxy.GetValue();
				}
			}
		}
	}

	void FControlBusProxy::Reset()
	{
		GeneratorValue = 1.0f;
		MixValue = NAN;
	}
} // namespace AudioModulation

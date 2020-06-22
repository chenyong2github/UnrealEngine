// Copyright Epic Games, Inc. All Rights Reserved.
#include "SoundModulationPatchProxy.h"

#include "AudioDefines.h"
#include "AudioDevice.h"
#include "AudioModulation.h"
#include "AudioModulationStatics.h"
#include "AudioModulationSystem.h"
#include "IAudioModulation.h"
#include "SoundControlBusProxy.h"
#include "SoundModulationParameter.h"
#include "SoundModulationProxy.h"
#include "SoundModulationTransform.h"



namespace AudioModulation
{
	const FPatchId InvalidPatchId = INDEX_NONE;

	FModulationInputProxy::FModulationInputProxy(const FModulationInputSettings& InSettings, FAudioModulationSystem& OutModSystem)
		: BusHandle(FBusHandle::Create(InSettings.BusSettings, OutModSystem.RefProxies.Buses, OutModSystem))
		, Transform(InSettings.Transform)
		, bSampleAndHold(InSettings.bSampleAndHold)
	{
	}

	FModulationOutputProxy::FModulationOutputProxy(FSoundModulationOutputTransform InTransform, float InDefaultValue, const Audio::FModulationMixFunction& InMixFunction)
		: MixFunction(InMixFunction)
		, DefaultValue(InDefaultValue)
	{
	}

	FModulationPatchProxy::FModulationPatchProxy(const FModulationPatchSettings& InSettings, FAudioModulationSystem& OutModSystem)
	{
		Init(InSettings, OutModSystem);
	}

	void FModulationPatchProxy::Init(const FModulationPatchSettings& InSettings, FAudioModulationSystem& OutModSystem)
	{
		bBypass = InSettings.bBypass;
		DefaultInputValue = InSettings.DefaultInputValue;

		// Cache proxies to avoid releasing bus state (and potentially referenced bus state) when reinitializing
		TArray<FModulationInputProxy> CachedProxies = InputProxies;

		InputProxies.Reset();
		for (const FModulationInputSettings& Input : InSettings.InputSettings)
		{
			InputProxies.Emplace(Input, OutModSystem);
		}

		OutputProxy = FModulationOutputProxy(InSettings.Transform, InSettings.DefaultOutputValue, InSettings.MixFunction);
	}

	bool FModulationPatchProxy::IsBypassed() const
	{
		return bBypass;
	}

	float FModulationPatchProxy::GetValue() const
	{
		if (bBypass)
		{
			return OutputProxy.DefaultValue;
		}

		return Value;
	}

	void FModulationPatchProxy::Update()
	{
		Value = DefaultInputValue;

		float& OutSampleHold = OutputProxy.SampleAndHoldValue;
		if (!OutputProxy.bInitialized)
		{
			OutSampleHold = DefaultInputValue;
		}

		for (const FModulationInputProxy& Input : InputProxies)
		{
			if (Input.bSampleAndHold)
			{
				if (!OutputProxy.bInitialized && Input.BusHandle.IsValid())
				{
					const FControlBusProxy& BusProxy = Input.BusHandle.FindProxy();
					if (!BusProxy.IsBypassed())
					{
						float ModStageValue = BusProxy.GetValue();
						Input.Transform.Apply(ModStageValue);
						OutputProxy.MixFunction(&OutSampleHold, &ModStageValue, 1);
					}
				}
			}
			else
			{
				if (Input.BusHandle.IsValid())
				{
					const FControlBusProxy& BusProxy = Input.BusHandle.FindProxy();
					if (!BusProxy.IsBypassed())
					{
						float ModStageValue = BusProxy.GetValue();
						Input.Transform.Apply(ModStageValue);
						OutputProxy.MixFunction(&Value, &ModStageValue, 1);
					}
				}
			}
		}

		if (!OutputProxy.bInitialized)
		{
			const float OutputMin = OutputProxy.Transform.OutputMin;
			const float OutputMax = OutputProxy.Transform.OutputMax;
			OutSampleHold = FMath::Clamp(OutSampleHold, OutputMin, OutputMax);
			OutputProxy.bInitialized = true;
		}

		OutputProxy.Transform.Apply(Value);
		OutputProxy.MixFunction(&Value, &OutSampleHold, 1);
	}

	FModulationPatchRefProxy::FModulationPatchRefProxy()
		: FModulationPatchProxy()
		, TModulatorProxyRefType()
	{
	}

	FModulationPatchRefProxy::FModulationPatchRefProxy(const FModulationPatchSettings& InSettings, FAudioModulationSystem& OutModSystem)
		: FModulationPatchProxy(InSettings, OutModSystem)
		, TModulatorProxyRefType(InSettings.GetName(), InSettings.GetId(), OutModSystem)
	{
	}

	FModulationPatchRefProxy& FModulationPatchRefProxy::operator =(const FModulationPatchSettings& InSettings)
	{
		check(ModSystem);
		Init(InSettings, *ModSystem);
		return *this;
	}
} // namespace AudioModulation
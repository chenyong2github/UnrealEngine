// Copyright Epic Games, Inc. All Rights Reserved.
#include "SoundModulationPatchProxy.h"

#include "AudioDefines.h"
#include "AudioDevice.h"
#include "AudioModulation.h"
#include "AudioModulationStatics.h"
#include "AudioModulationSystem.h"
#include "SoundControlBusProxy.h"
#include "SoundModulationProxy.h"
#include "SoundModulationTransform.h"


namespace AudioModulation
{
	const FPatchId InvalidPatchId = INDEX_NONE;

	void MixInModulationValue(ESoundModulatorOperator& Operator, float ModStageValue, float& Value)
	{
		switch (Operator)
		{
			case ESoundModulatorOperator::Max:
			{
				Value = FMath::Max(ModStageValue, Value);
			}
			break;

			case ESoundModulatorOperator::Min:
			{
				Value = FMath::Min(ModStageValue, Value);
			}
			break;

			case ESoundModulatorOperator::Multiply:
			{
				Value *= ModStageValue;
			}
			break;

			case ESoundModulatorOperator::Divide:
			{
				Value /= ModStageValue;
			}
			break;

			case ESoundModulatorOperator::Add:
			{
				Value += ModStageValue;
			}
			break;

			case ESoundModulatorOperator::Subtract:
			{
				Value -= ModStageValue;
			}
			break;

			case ESoundModulatorOperator::None:
			default:
			{
				checkf(false, TEXT("Cannot apply 'None' as operator to modulator"));
				static_assert(static_cast<int32>(ESoundModulatorOperator::Count) == 7, "Possible missing operator switch case coverage");
			}
			break;
		}
	}

	FModulationInputProxy::FModulationInputProxy(const FModulationInputSettings& InSettings, FAudioModulationSystem& OutModSystem)
		: BusHandle(FBusHandle::Create(InSettings.BusSettings, OutModSystem.RefProxies.Buses, OutModSystem))
		, Transform(InSettings.Transform)
		, bSampleAndHold(InSettings.bSampleAndHold)
	{
	}

	FModulationOutputSettings::FModulationOutputSettings(const FSoundModulationOutputBase& InOutput)
		: Operator(InOutput.GetOperator())
		, Transform(InOutput.Transform)
	{
	}

	FModulationOutputProxy::FModulationOutputProxy(const FModulationOutputSettings& InSettings)
		: Settings(InSettings)
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

		OutputProxy.Settings = InSettings.OutputSettings;
	}

	bool FModulationPatchProxy::IsBypassed() const
	{
		return bBypass;
	}

	float FModulationPatchProxy::GetValue() const
	{
		return Value;
	}

	void FModulationPatchProxy::Update()
	{
		Value = DefaultInputValue;

		float& OutSampleHold = OutputProxy.SampleAndHoldValue;
		if (!OutputProxy.bInitialized)
		{
			OutSampleHold = SoundModulatorOperator::GetDefaultValue(
				OutputProxy.Settings.Operator,
				OutputProxy.Settings.Transform.OutputMin,
				OutputProxy.Settings.Transform.OutputMax);
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
						MixInModulationValue(OutputProxy.Settings.Operator, ModStageValue, OutSampleHold);
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
						MixInModulationValue(OutputProxy.Settings.Operator, ModStageValue, Value);
					}
				}
			}
		}

		if (!OutputProxy.bInitialized)
		{
			const float OutputMin = OutputProxy.Settings.Transform.OutputMin;
			const float OutputMax = OutputProxy.Settings.Transform.OutputMax;
			OutSampleHold = FMath::Clamp(OutSampleHold, OutputMin, OutputMax);
			OutputProxy.bInitialized = true;
		}

		OutputProxy.Settings.Transform.Apply(Value);
		MixInModulationValue(OutputProxy.Settings.Operator, OutSampleHold, Value);
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
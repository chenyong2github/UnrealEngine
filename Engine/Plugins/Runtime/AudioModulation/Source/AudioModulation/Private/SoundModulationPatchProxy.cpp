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

	FModulationInputProxy::FModulationInputProxy(const FSoundModulationInputBase& Input, FAudioModulationSystem& ModSystem)
		: Transform(Input.Transform)
		, bSampleAndHold(Input.bSampleAndHold)
	{
		if (const USoundControlBusBase* Bus = Input.GetBus())
		{
			auto OnCreate = [Bus](FControlBusProxy& NewProxy)
			{
				NewProxy.InitLFOs(*Bus);
			};
			BusHandle = FBusHandle::Create(*Bus, ModSystem.RefProxies.Buses, ModSystem, OnCreate);
		}
	}

	FModulationOutputProxy::FModulationOutputProxy(const FSoundModulationOutputBase& Output)
		: Operator(Output.GetOperator())
		, Transform(Output.Transform)
	{
	}

	FModulationPatchProxy::FModulationPatchProxy(const FSoundModulationPatchBase& InPatch, FAudioModulationSystem& InModSystem)
	{
		Init(InPatch, InModSystem);
	}

	void FModulationPatchProxy::Init(const FSoundModulationPatchBase& InPatch, FAudioModulationSystem& InModSystem)
	{
		bBypass = InPatch.bBypass;

		DefaultInputValue = InPatch.DefaultInputValue;

		InputProxies.Reset();
		TArray<const FSoundModulationInputBase*> Inputs = InPatch.GetInputs();
		for (const FSoundModulationInputBase* Input : Inputs)
		{
			InputProxies.Emplace_GetRef(*Input, InModSystem);
		}

		OutputProxy = *InPatch.GetOutput();
	}

	bool FModulationPatchProxy::IsBypassed() const
	{
		return bBypass;
	}

	float FModulationPatchProxy::Update()
	{
		float OutValue = DefaultInputValue;

		float& OutSampleHold = OutputProxy.SampleAndHoldValue;
		if (!OutputProxy.bInitialized)
		{
			OutSampleHold = SoundModulatorOperator::GetDefaultValue(OutputProxy.Operator, OutputProxy.Transform.OutputMin, OutputProxy.Transform.OutputMax);
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
						MixInModulationValue(OutputProxy.Operator, ModStageValue, OutSampleHold);
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
						MixInModulationValue(OutputProxy.Operator, ModStageValue, OutValue);
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

		OutputProxy.Transform.Apply(OutValue);
		MixInModulationValue(OutputProxy.Operator, OutSampleHold, OutValue);
		return OutValue;
	}

	FModulationPatchRefProxy::FModulationPatchRefProxy()
		: FModulationPatchProxy()
		, TModulatorProxyRefType()
	{
	}

	FModulationPatchRefProxy::FModulationPatchRefProxy(const USoundModulationPatch& InPatch, FAudioModulationSystem& InModSystem)
		: FModulationPatchProxy(InPatch.PatchSettings, InModSystem)
		, TModulatorProxyRefType(InPatch.GetName(), InPatch.GetUniqueID(), InModSystem)
	{
	}

	FModulationPatchRefProxy& FModulationPatchRefProxy::operator =(const USoundModulationPatch& InPatch)
	{
		Init(InPatch.PatchSettings, *ModSystem);
		return *this;
	}
} // namespace AudioModulation
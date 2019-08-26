// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "SoundModulationPatch.h"

#include "AudioDevice.h"
#include "AudioModulation.h"
#include "AudioModulationInternal.h"
#include "AudioModulationUtils.h"
#include "SoundModulationTransform.h"


USoundModulationSettings::USoundModulationSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Lowpass.Output.Transform.OutputMin = 20.0f;
	Lowpass.Output.Transform.OutputMax = 20000.0f;
	Lowpass.Output.Operator = ESoundModulatorOperator::Min;

	Highpass.DefaultInputValue = 0.0f;
	Highpass.Output.Transform.OutputMin = 20.0f;
	Highpass.Output.Transform.OutputMax = 20000.0f;
	Highpass.Output.Operator = ESoundModulatorOperator::Max;
}

#if WITH_EDITOR
void USoundModulationSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (!PropertyChangedEvent.Property)
	{
		return;
	}

	if (AudioModulation::FAudioModulationImpl* Impl = AudioModulation::GetModulationImpl(GetWorld()))
	{
		Impl->OnEditSource(*this);
	}
}

void USoundModulationSettings::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	if (!PropertyChangedEvent.Property)
	{
		return;
	}

	if (AudioModulation::FAudioModulationImpl* Impl = AudioModulation::GetModulationImpl(GetWorld()))
	{
		Impl->OnEditSource(*this);
	}
}
#endif // WITH_EDITOR

FSoundModulationOutput::FSoundModulationOutput()
	: Operator(ESoundModulatorOperator::Multiply)
{
}

FSoundModulationInputBase::FSoundModulationInputBase()
	: bSampleAndHold(0)
{
}

FSoundVolumeModulationInput::FSoundVolumeModulationInput()
	: Bus(nullptr)
{
}

FSoundPitchModulationInput::FSoundPitchModulationInput()
	: Bus(nullptr)
{
}

FSoundHPFModulationInput::FSoundHPFModulationInput()
	: Bus(nullptr)
{
}

FSoundLPFModulationInput::FSoundLPFModulationInput()
	: Bus(nullptr)
{
}

FSoundModulationPatchBase::FSoundModulationPatchBase()
	: DefaultInputValue(1.0f)
{
}

namespace AudioModulation
{
	FModulationInputProxy::FModulationInputProxy()
		: BusId(InvalidBusId)
		, bSampleAndHold(0)
	{
	}

	FModulationInputProxy::FModulationInputProxy(const FSoundModulationInputBase& Input)
		: BusId(InvalidBusId)
		, Transform(Input.Transform)
		, bSampleAndHold(Input.bSampleAndHold)
	{
		if (const USoundModulatorBusBase* Bus = Input.GetBus())
		{
			BusId = static_cast<AudioModulation::BusId>(Bus->GetUniqueID());
		}
	}

	FModulationOutputProxy::FModulationOutputProxy()
		: bInitialized(0)
		, Operator(ESoundModulatorOperator::Multiply)
		, SampleAndHoldValue(1.0f)
	{
	}

	FModulationOutputProxy::FModulationOutputProxy(const FSoundModulationOutput& Output)
		: bInitialized(0)
		, Operator(Output.Operator)
		, SampleAndHoldValue(1.0f)
		, Transform(Output.Transform)
	{
	}

	FModulationPatchProxy::FModulationPatchProxy()
		: DefaultInputValue(1.0f)
	{
	}

	FModulationPatchProxy::FModulationPatchProxy(const FSoundModulationPatchBase& Patch)
		: DefaultInputValue(Patch.DefaultInputValue)
		, OutputProxy(Patch.Output)
	{
		Patch.GenerateProxies(InputProxies);
	}

	FModulationSettingsProxy::FModulationSettingsProxy()
#if WITH_EDITOR
		: ObjectID(0)
#endif // WITH_EDITOR
	{
		Lowpass.OutputProxy.Transform.OutputMin  = 20.0f;
		Lowpass.OutputProxy.Transform.OutputMax  = 20000.0f;
		Lowpass.OutputProxy.SampleAndHoldValue   = 20000.0f;
		Lowpass.OutputProxy.Operator = ESoundModulatorOperator::Min;

		Highpass.DefaultInputValue = 0.0f;
		Highpass.OutputProxy.Transform.OutputMin = 20.0f;
		Highpass.OutputProxy.Transform.OutputMax = 20000.0f;
		Highpass.OutputProxy.SampleAndHoldValue  = 20.0f;
		Highpass.OutputProxy.Operator = ESoundModulatorOperator::Max;
	}

	FModulationSettingsProxy::FModulationSettingsProxy(const USoundModulationSettings& Settings)
		: Volume(Settings.Volume)
		, Pitch(Settings.Pitch)
		, Lowpass(Settings.Lowpass)
		, Highpass(Settings.Highpass)
#if WITH_EDITOR
		, ObjectID(Settings.GetUniqueID())
#endif // WITH_EDITOR
	{
		for (const USoundModulatorBusMix* Mix : Settings.Mixes)
		{
			if (Mix)
			{
				Mixes.Add(static_cast<BusMixId>(Mix->GetUniqueID()));
			}
		}
	}
} // namespace AudioModulation
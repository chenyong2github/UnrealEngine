// Copyright Epic Games, Inc. All Rights Reserved.
#include "SoundModulationPatch.h"

#include "AudioDefines.h"
#include "AudioDevice.h"
#include "AudioModulation.h"
#include "AudioModulationInternal.h"
#include "AudioModulationStatics.h"
#include "SoundModulationProxy.h"
#include "SoundModulationTransform.h"


namespace
{
	template <typename T>
	void ClampPatchInputs(TArray<T>& Inputs)
	{
		for (T& Input : Inputs)
		{
			if (Input.Transform.InputMin > Input.Transform.InputMax)
			{
				Input.Transform.InputMin = Input.Transform.InputMax;
			}
		}
	}
} // namespace <>

USoundModulationSettings::USoundModulationSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Volume.Output.Transform.Curve = ESoundModulatorOutputCurve::Exp;
	Volume.Output.Transform.Scalar = 0.5f;

	Lowpass.Output.Transform.OutputMin = 20.0f;
	Lowpass.Output.Transform.OutputMax = 20000.0f;
	Lowpass.Output.Transform.Curve = ESoundModulatorOutputCurve::Exp;
	Lowpass.Output.SetOperator(ESoundModulatorOperator::Min);

	Highpass.DefaultInputValue = 0.0f;
	Highpass.Output.Transform.OutputMin = 20.0f;
	Highpass.Output.Transform.OutputMax = 20000.0f;
	Highpass.Output.Transform.Curve = ESoundModulatorOutputCurve::Exp;
	Highpass.Output.SetOperator(ESoundModulatorOperator::Max);
}

#if WITH_EDITOR
void USoundModulationSettings::OnPostEditChange(UWorld* World)
{
	Volume.Clamp();
	Pitch.Clamp();
	Lowpass.Clamp();
	Highpass.Clamp();

	for (FSoundControlModulationPatch& Control : Controls)
	{
		Control.Clamp();
	}

	if (AudioModulation::FAudioModulationImpl* Impl = UAudioModulationStatics::GetModulationImpl(World))
	{
		Impl->OnEditPluginSettings(*this);
	}
}

void USoundModulationSettings::PostDuplicate(EDuplicateMode::Type DuplicateMode)
{
	if (DuplicateMode == EDuplicateMode::Normal)
	{
		OnPostEditChange(GetWorld());
	}
}

void USoundModulationSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	OnPostEditChange(GetWorld());
}

void USoundModulationSettings::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	OnPostEditChange(GetWorld());
}

void FSoundModulationPatchBase::Clamp()
{
	if (FSoundModulationOutputBase* Output = GetOutput())
	{
		if (Output->Transform.InputMin > Output->Transform.InputMax)
		{
			Output->Transform.InputMin = Output->Transform.InputMax;
		}

		if (Output->Transform.OutputMin > Output->Transform.OutputMax)
		{
			Output->Transform.OutputMin = Output->Transform.OutputMax;
		}
	}
}

void FSoundVolumeModulationPatch::Clamp()
{
	FSoundModulationPatchBase::Clamp();

	ClampPatchInputs<FSoundVolumeModulationInput>(Inputs);
	Output.Transform.OutputMin = FMath::Clamp(Output.Transform.OutputMin, 0.0f, MAX_VOLUME);
	Output.Transform.OutputMax = FMath::Clamp(Output.Transform.OutputMax, 0.0f, MAX_VOLUME);
}

void FSoundPitchModulationPatch::Clamp()
{
	FSoundModulationPatchBase::Clamp();

	ClampPatchInputs<FSoundPitchModulationInput>(Inputs);
	Output.Transform.OutputMin = FMath::Clamp(Output.Transform.OutputMin, MIN_PITCH, MAX_PITCH);
	Output.Transform.OutputMax = FMath::Clamp(Output.Transform.OutputMax, MIN_PITCH, MAX_PITCH);
}

void FSoundLPFModulationPatch::Clamp()
{
	FSoundModulationPatchBase::Clamp();

	ClampPatchInputs<FSoundLPFModulationInput>(Inputs);
	Output.Transform.OutputMin = FMath::Clamp(Output.Transform.OutputMin, MIN_FILTER_FREQUENCY, MAX_FILTER_FREQUENCY);
	Output.Transform.OutputMax = FMath::Clamp(Output.Transform.OutputMax, MIN_FILTER_FREQUENCY, MAX_FILTER_FREQUENCY);
}

void FSoundHPFModulationPatch::Clamp()
{
	FSoundModulationPatchBase::Clamp();

	ClampPatchInputs<FSoundHPFModulationInput>(Inputs);
	Output.Transform.OutputMin = FMath::Clamp(Output.Transform.OutputMin, MIN_FILTER_FREQUENCY, MAX_FILTER_FREQUENCY);
	Output.Transform.OutputMax = FMath::Clamp(Output.Transform.OutputMax, MIN_FILTER_FREQUENCY, MAX_FILTER_FREQUENCY);
}
#endif // WITH_EDITOR

TArray<const FSoundModulationInputBase*> FSoundVolumeModulationPatch::GetInputs() const
{
	TArray<const FSoundModulationInputBase*> OutInputs;
	for (const FSoundVolumeModulationInput& Input : Inputs)
	{
		OutInputs.Add(static_cast<const FSoundModulationInputBase*>(&Input));
	}

	return OutInputs;
}

TArray<const FSoundModulationInputBase*> FSoundPitchModulationPatch::GetInputs() const
{
	TArray<const FSoundModulationInputBase*> OutInputs;
	for (const FSoundPitchModulationInput& Input : Inputs)
	{
		OutInputs.Add(static_cast<const FSoundModulationInputBase*>(&Input));
	}

	return OutInputs;
}

TArray<const FSoundModulationInputBase*> FSoundLPFModulationPatch::GetInputs() const
{
	TArray<const FSoundModulationInputBase*> OutInputs;
	for (const FSoundLPFModulationInput& Input : Inputs)
	{
		OutInputs.Add(static_cast<const FSoundModulationInputBase*>(&Input));
	}

	return OutInputs;
}

TArray<const FSoundModulationInputBase*> FSoundHPFModulationPatch::GetInputs() const
{
	TArray<const FSoundModulationInputBase*> OutInputs;
	for (const FSoundHPFModulationInput& Input : Inputs)
	{
		OutInputs.Add(static_cast<const FSoundModulationInputBase*>(&Input));
	}

	return OutInputs;
}

TArray<const FSoundModulationInputBase*> FSoundControlModulationPatch::GetInputs() const
{
	TArray<const FSoundModulationInputBase*> OutInputs;
	for (const FSoundControlModulationInput& Input : Inputs)
	{
		OutInputs.Add(static_cast<const FSoundModulationInputBase*>(&Input));
	}

	return OutInputs;
}

FSoundModulationOutput::FSoundModulationOutput()
	: Operator(ESoundModulatorOperator::Multiply)
{
}

FSoundModulationOutputFixedOperator::FSoundModulationOutputFixedOperator()
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

FSoundControlModulationInput::FSoundControlModulationInput()
	: Bus(nullptr)
{
}

FSoundLPFModulationInput::FSoundLPFModulationInput()
	: Bus(nullptr)
{
}

FSoundModulationPatchBase::FSoundModulationPatchBase()
	: DefaultInputValue(1.0f)
	, bBypass(1)
{
}

namespace AudioModulation
{
	FModulationInputProxy::FModulationInputProxy()
		: bSampleAndHold(0)
	{
	}

	FModulationInputProxy::FModulationInputProxy(const FSoundModulationInputBase& Input, FReferencedProxies& OutRefProxies)
		: Transform(Input.Transform)
		, bSampleAndHold(Input.bSampleAndHold)
	{
		if (const USoundControlBusBase* Bus = Input.GetBus())
		{
			FReferencedProxies* RefProxies = &OutRefProxies;
			auto OnCreate = [RefProxies, Bus](FControlBusProxy& NewProxy)
			{
				NewProxy.InitLFOs(*Bus, RefProxies->LFOs);
			};
			BusHandle = FBusHandle::Create(*Bus, OutRefProxies.Buses, OnCreate);
		}
	}

	FModulationOutputProxy::FModulationOutputProxy()
		: bInitialized(0)
		, Operator(ESoundModulatorOperator::Multiply)
		, SampleAndHoldValue(1.0f)
	{
	}

	FModulationOutputProxy::FModulationOutputProxy(const FSoundModulationOutputBase& Output)
		: bInitialized(0)
		, Operator(Output.GetOperator())
		, SampleAndHoldValue(1.0f)
		, Transform(Output.Transform)
	{
	}

	FModulationPatchProxy::FModulationPatchProxy()
		: DefaultInputValue(1.0f)
		, bBypass(1)
	{
	}

	FModulationPatchProxy::FModulationPatchProxy(const FSoundModulationPatchBase& Patch, FReferencedProxies& OutRefProxies)
		: DefaultInputValue(Patch.DefaultInputValue)
		, bBypass(Patch.bBypass)
		, OutputProxy(*Patch.GetOutput())
	{
		TArray<const FSoundModulationInputBase*> Inputs = Patch.GetInputs();
		for (const FSoundModulationInputBase* Input : Inputs)
		{
			InputProxies.Emplace_GetRef(*Input, OutRefProxies);
		}
	}

	FModulationSettingsProxy::FModulationSettingsProxy()
	{
		Volume.OutputProxy.Transform.Curve = ESoundModulatorOutputCurve::Exp;
		Volume.OutputProxy.Transform.Scalar = 0.5f;

		Lowpass.OutputProxy.Transform.OutputMin = 20.0f;
		Lowpass.OutputProxy.Transform.OutputMax = 20000.0f;
		Lowpass.OutputProxy.Transform.Curve = ESoundModulatorOutputCurve::Exp;
		Lowpass.OutputProxy.Operator = ESoundModulatorOperator::Min;

		Highpass.DefaultInputValue = 0.0f;
		Highpass.OutputProxy.Transform.OutputMin = 20.0f;
		Highpass.OutputProxy.Transform.OutputMax = 20000.0f;
		Highpass.OutputProxy.Transform.Curve = ESoundModulatorOutputCurve::Exp;
		Highpass.OutputProxy.Operator = ESoundModulatorOperator::Max;
	}

	FModulationSettingsProxy::FModulationSettingsProxy(const USoundModulationSettings& Settings, FReferencedProxies& OutRefProxies)
		: TModulatorProxyBase<uint32>(Settings.GetName(), Settings.GetUniqueID())
		, Volume(Settings.Volume, OutRefProxies)
		, Pitch(Settings.Pitch, OutRefProxies)
		, Lowpass(Settings.Lowpass, OutRefProxies)
		, Highpass(Settings.Highpass, OutRefProxies)
	{
		for (const FSoundControlModulationPatch& Patch : Settings.Controls)
		{
			Controls.Add(Patch.Control, FModulationPatchProxy(Patch, OutRefProxies));
		}

		for (const USoundControlBusMix* Mix : Settings.Mixes)
		{
			if (Mix)
			{
				Mixes.Add(FBusMixHandle::Create(*Mix, OutRefProxies.BusMixes));
			}
		}
	}
} // namespace AudioModulation
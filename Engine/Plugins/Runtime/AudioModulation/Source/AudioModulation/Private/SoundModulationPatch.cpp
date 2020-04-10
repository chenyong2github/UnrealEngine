// Copyright Epic Games, Inc. All Rights Reserved.
#include "SoundModulationPatch.h"

#include "AudioDefines.h"
#include "AudioDevice.h"
#include "AudioModulation.h"
#include "AudioModulationStatics.h"
#include "AudioModulationSystem.h"
#include "SoundControlBus.h"
#include "SoundModulationTransform.h"


namespace AudioModulation
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
} // namespace AudioModulation

USoundModulationPatch::USoundModulationPatch(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

#if WITH_EDITOR
void USoundModulationPatch::PostEditChangeProperty(FPropertyChangedEvent& InPropertyChangedEvent)
{
	if (FProperty* Property = InPropertyChangedEvent.Property)
	{
		AudioModulation::OnEditModulator(InPropertyChangedEvent, *this);
	}

	Super::PostEditChangeProperty(InPropertyChangedEvent);
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

	AudioModulation::ClampPatchInputs<FSoundVolumeModulationInput>(Inputs);
	Output.Transform.OutputMin = FMath::Clamp(Output.Transform.OutputMin, 0.0f, MAX_VOLUME);
	Output.Transform.OutputMax = FMath::Clamp(Output.Transform.OutputMax, 0.0f, MAX_VOLUME);
}

void FSoundPitchModulationPatch::Clamp()
{
	FSoundModulationPatchBase::Clamp();

	AudioModulation::ClampPatchInputs<FSoundPitchModulationInput>(Inputs);
	Output.Transform.OutputMin = FMath::Clamp(Output.Transform.OutputMin, MIN_PITCH, MAX_PITCH);
	Output.Transform.OutputMax = FMath::Clamp(Output.Transform.OutputMax, MIN_PITCH, MAX_PITCH);
}

void FSoundLPFModulationPatch::Clamp()
{
	FSoundModulationPatchBase::Clamp();

	AudioModulation::ClampPatchInputs<FSoundLPFModulationInput>(Inputs);
	Output.Transform.OutputMin = FMath::Clamp(Output.Transform.OutputMin, MIN_FILTER_FREQUENCY, MAX_FILTER_FREQUENCY);
	Output.Transform.OutputMax = FMath::Clamp(Output.Transform.OutputMax, MIN_FILTER_FREQUENCY, MAX_FILTER_FREQUENCY);
}

void FSoundHPFModulationPatch::Clamp()
{
	FSoundModulationPatchBase::Clamp();

	AudioModulation::ClampPatchInputs<FSoundHPFModulationInput>(Inputs);
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

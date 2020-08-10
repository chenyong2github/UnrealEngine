// Copyright Epic Games, Inc. All Rights Reserved.
#include "SoundModulationPatch.h"

#include "AudioDefines.h"
#include "AudioDevice.h"
#include "AudioModulation.h"
#include "AudioModulationStatics.h"
#include "AudioModulationSystem.h"
#include "SoundControlBus.h"
#include "SoundModulationTransform.h"


#define LOCTEXT_NAMESPACE "SoundModulationPatch"

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
	AudioModulation::IterateModSystems([this](AudioModulation::FAudioModulationSystem& OutModSystem)
	{
		OutModSystem.UpdateModulator(*this);
	});

	Super::PostEditChangeProperty(InPropertyChangedEvent);
}

void USoundModulationPatch::PostEditChangeChainProperty(FPropertyChangedChainEvent& InPropertyChangedEvent)
{
	AudioModulation::IterateModSystems([this](AudioModulation::FAudioModulationSystem& OutModSystem)
	{
		OutModSystem.UpdateModulator(*this);
	});

	Super::PostEditChangeChainProperty(InPropertyChangedEvent);
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
#endif // WITH_EDITOR

FSoundModulationInputBase::FSoundModulationInputBase()
	: bSampleAndHold(0)
{
}

const USoundControlBus* FSoundControlModulationInput::GetBus() const
{
	return Bus;
}

FSoundModulationPatchBase::FSoundModulationPatchBase()
	: bBypass(1)
{
}

#undef LOCTEXT_NAMESPACE // SoundModulationPatch

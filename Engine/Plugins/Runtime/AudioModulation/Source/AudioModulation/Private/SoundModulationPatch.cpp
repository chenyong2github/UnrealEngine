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
	AudioModulation::IterateModulationImpl([this](AudioModulation::FAudioModulation& OutModulation)
	{
		OutModulation.UpdateModulator(*this);
	});

	Super::PostEditChangeProperty(InPropertyChangedEvent);
}

void USoundModulationPatch::PostEditChangeChainProperty(FPropertyChangedChainEvent& InPropertyChangedEvent)
{
	AudioModulation::IterateModulationImpl([this](AudioModulation::FAudioModulation& OutModulation)
	{
		OutModulation.UpdateModulator(*this);
	});

	Super::PostEditChangeChainProperty(InPropertyChangedEvent);
}
#endif // WITH_EDITOR

FSoundControlModulationInput::FSoundControlModulationInput()
	: bSampleAndHold(0)
{
}

const USoundControlBus* FSoundControlModulationInput::GetBus() const
{
	return Bus;
}

const USoundControlBus& FSoundControlModulationInput::GetBusChecked() const
{
	check(Bus);
	return *Bus;
}

#undef LOCTEXT_NAMESPACE // SoundModulationPatch

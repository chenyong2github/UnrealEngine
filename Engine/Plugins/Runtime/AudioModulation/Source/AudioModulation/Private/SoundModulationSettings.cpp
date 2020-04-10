// Copyright Epic Games, Inc. All Rights Reserved.
#include "SoundModulationSettings.h"

#include "AudioDefines.h"
#include "AudioDevice.h"
#include "AudioModulation.h"
#include "AudioModulationStatics.h"
#include "AudioModulationSystem.h"
#include "SoundModulationTransform.h"


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

	if (AudioModulation::FAudioModulationSystem* ModSystem = UAudioModulationStatics::GetModulationSystem(World))
	{
		ModSystem->OnEditPluginSettings(*this);
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
#endif // WITH_EDITOR

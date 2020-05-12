// Copyright Epic Games, Inc. All Rights Reserved.
#include "SoundModulationSettingsProxy.h"

#include "AudioModulationSystem.h"
#include "SoundModulationSettings.h"
#include "SoundModulationTransform.h"


namespace AudioModulation
{
	FSoundModulationSettings::FSoundModulationSettings(const USoundModulationSettings& InSettings)
		: TModulatorBase<uint32>(InSettings.GetName(), InSettings.GetUniqueID())
		, Volume(InSettings.Volume)
		, Pitch(InSettings.Pitch)
		, Lowpass(InSettings.Lowpass)
		, Highpass(InSettings.Highpass)
	{
	}

	FModulationSettingsProxy::FModulationSettingsProxy()
	{
		Volume.OutputProxy.Settings.Transform.Curve = ESoundModulatorOutputCurve::Exp;
		Volume.OutputProxy.Settings.Transform.Scalar = 0.5f;

		Lowpass.OutputProxy.Settings.Transform.OutputMin = 20.0f;
		Lowpass.OutputProxy.Settings.Transform.OutputMax = 20000.0f;
		Lowpass.OutputProxy.Settings.Transform.Curve = ESoundModulatorOutputCurve::Exp;
		Lowpass.OutputProxy.Settings.Operator = ESoundModulatorOperator::Min;

		Highpass.DefaultInputValue = 0.0f;
		Highpass.OutputProxy.Settings.Transform.OutputMin = 20.0f;
		Highpass.OutputProxy.Settings.Transform.OutputMax = 20000.0f;
		Highpass.OutputProxy.Settings.Transform.Curve = ESoundModulatorOutputCurve::Exp;
		Highpass.OutputProxy.Settings.Operator = ESoundModulatorOperator::Max;
	}

	FModulationSettingsProxy::FModulationSettingsProxy(const FSoundModulationSettings& InSettings, FAudioModulationSystem& OutModSystem)
		: TModulatorBase<uint32>(InSettings.GetName(), InSettings.GetId())
		, Volume(InSettings.Volume, OutModSystem)
		, Pitch(InSettings.Pitch, OutModSystem)
		, Lowpass(InSettings.Lowpass, OutModSystem)
		, Highpass(InSettings.Highpass, OutModSystem)
	{
	}
} // namespace AudioModulation

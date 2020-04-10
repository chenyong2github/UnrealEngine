// Copyright Epic Games, Inc. All Rights Reserved.
#include "SoundModulationSettingsProxy.h"

#include "AudioModulationSystem.h"
#include "SoundModulationSettings.h"
#include "SoundModulationTransform.h"


namespace AudioModulation
{
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

	FModulationSettingsProxy::FModulationSettingsProxy(const USoundModulationSettings& Settings, FAudioModulationSystem& InModSystem)
		: TModulatorProxyBase<uint32>(Settings.GetName(), Settings.GetUniqueID())
		, Volume(Settings.Volume, InModSystem)
		, Pitch(Settings.Pitch, InModSystem)
		, Lowpass(Settings.Lowpass, InModSystem)
		, Highpass(Settings.Highpass, InModSystem)
	{
	}
} // namespace AudioModulation

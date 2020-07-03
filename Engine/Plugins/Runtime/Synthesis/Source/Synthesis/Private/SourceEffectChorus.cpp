// Copyright Epic Games, Inc. All Rights Reserved.

#include "SourceEffects/SourceEffectChorus.h"

void FSourceEffectChorus::Init(const FSoundEffectSourceInitData& InitData)
{
	bIsActive = true;

	if (USourceEffectChorusPreset* ProcPreset = Cast<USourceEffectChorusPreset>(Preset.Get()))
	{
		Audio::FDeviceId DeviceId = static_cast<Audio::FDeviceId>(InitData.AudioDeviceId);
		const bool bIsBuffered = true;
		DepthMod.Init(DeviceId, bIsBuffered);
		FeedbackMod.Init(DeviceId, bIsBuffered);
		FrequencyMod.Init(DeviceId, FName("FilterFrequency"), bIsBuffered);
		WetMod.Init(DeviceId, bIsBuffered);
		DryMod.Init(DeviceId, bIsBuffered);
		SpreadMod.Init(DeviceId, bIsBuffered);
	}

	Chorus.Init(InitData.SampleRate, InitData.NumSourceChannels, 2.0f, 64);
}

void FSourceEffectChorus::OnPresetChanged()
{
	GET_EFFECT_SETTINGS(SourceEffectChorus);

	SettingsCopy = Settings;
}

void FSourceEffectChorus::ProcessAudio(const FSoundEffectSourceInputData& InData, float* OutAudioBufferData)
{
	bool bModulated = false;

	bModulated |= DepthMod.ProcessControl(SettingsCopy.DepthModulation.Value, InData.NumSamples);
	bModulated |= FeedbackMod.ProcessControl(SettingsCopy.FeedbackModulation.Value, InData.NumSamples);
	bModulated |= FrequencyMod.ProcessControl(SettingsCopy.FrequencyModulation.Value, InData.NumSamples);
	bModulated |= WetMod.ProcessControl(SettingsCopy.WetModulation.Value, InData.NumSamples);
	bModulated |= DryMod.ProcessControl(SettingsCopy.DryModulation.Value, InData.NumSamples);
	bModulated |= SpreadMod.ProcessControl(SettingsCopy.SpreadModulation.Value, InData.NumSamples);

	if (bModulated)
	{
		const int32 NumChannels = Chorus.GetNumChannels();
		for (int32 SampleIndex = 0; SampleIndex < InData.NumSamples; SampleIndex += NumChannels)
		{
			Chorus.SetWetLevel(WetMod.GetBuffer()[SampleIndex]);
			Chorus.SetDryLevel(DryMod.GetBuffer()[SampleIndex]);
			Chorus.SetSpread(SpreadMod.GetBuffer()[SampleIndex]);

			const float Depth = DepthMod.GetBuffer()[SampleIndex];
			Chorus.SetDepth(Audio::EChorusDelays::Left, Depth);
			Chorus.SetDepth(Audio::EChorusDelays::Center, Depth);
			Chorus.SetDepth(Audio::EChorusDelays::Right, Depth);

			const float Feedback = FeedbackMod.GetBuffer()[SampleIndex];
			Chorus.SetFeedback(Audio::EChorusDelays::Left, Feedback);
			Chorus.SetFeedback(Audio::EChorusDelays::Center, Feedback);
			Chorus.SetFeedback(Audio::EChorusDelays::Right, Feedback);

			const float Frequency = FrequencyMod.GetBuffer()[SampleIndex];
			Chorus.SetFrequency(Audio::EChorusDelays::Left, Frequency);
			Chorus.SetFrequency(Audio::EChorusDelays::Center, Frequency);
			Chorus.SetFrequency(Audio::EChorusDelays::Right, Frequency);

			Chorus.ProcessAudioFrame(&InData.InputSourceEffectBufferPtr[SampleIndex], &OutAudioBufferData[SampleIndex]);
		}
	}
	else
	{
		Chorus.ProcessAudio(InData.InputSourceEffectBufferPtr, InData.NumSamples, OutAudioBufferData);
	}
}

void FSourceEffectChorus::SetDepthModulator(const FSoundModulationDestinationSettings& InModulatorSettings)
{
	DepthMod.UpdateSettings(InModulatorSettings);
}

void FSourceEffectChorus::SetFeedbackModulator(const FSoundModulationDestinationSettings& InModulatorSettings)
{
	FeedbackMod.UpdateSettings(InModulatorSettings);
}

void FSourceEffectChorus::SetSpreadModulator(const FSoundModulationDestinationSettings& InModulatorSettings)
{
	SpreadMod.UpdateSettings(InModulatorSettings);
}

void FSourceEffectChorus::SetDryModulator(const FSoundModulationDestinationSettings& InModulatorSettings)
{
	DryMod.UpdateSettings(InModulatorSettings);
}

void FSourceEffectChorus::SetWetModulator(const FSoundModulationDestinationSettings& InModulatorSettings)
{
	WetMod.UpdateSettings(InModulatorSettings);
}

void FSourceEffectChorus::SetFrequencyModulator(const FSoundModulationDestinationSettings& InModulatorSettings)
{
	FrequencyMod.UpdateSettings(InModulatorSettings);
}

void USourceEffectChorusPreset::OnInit()
{
	SetDepthModulator(Settings.SpreadModulation);
	SetDryModulator(Settings.DryModulation);
	SetWetModulator(Settings.WetModulation);
	SetFeedbackModulator(Settings.FeedbackModulation);
	SetFrequencyModulator(Settings.FrequencyModulation);
	SetSpreadModulator(Settings.SpreadModulation);
}

void USourceEffectChorusPreset::SetDepthModulator(const FSoundModulationDestinationSettings& InModulatorSettings)
{
	IterateEffects<FSourceEffectChorus>([InModulatorSettings](FSourceEffectChorus& InDelay)
	{
		InDelay.SetDepthModulator(InModulatorSettings);
	});
}

void USourceEffectChorusPreset::SetFeedbackModulator(const FSoundModulationDestinationSettings& InModulatorSettings)
{
	IterateEffects<FSourceEffectChorus>([InModulatorSettings](FSourceEffectChorus& InDelay)
	{
		InDelay.SetFeedbackModulator(InModulatorSettings);
	});
}

void USourceEffectChorusPreset::SetFrequencyModulator(const FSoundModulationDestinationSettings& InModulatorSettings)
{
	IterateEffects<FSourceEffectChorus>([InModulatorSettings](FSourceEffectChorus& InDelay)
	{
		InDelay.SetFrequencyModulator(InModulatorSettings);
	});
}

void USourceEffectChorusPreset::SetWetModulator(const FSoundModulationDestinationSettings& InModulatorSettings)
{
	IterateEffects<FSourceEffectChorus>([InModulatorSettings](FSourceEffectChorus& InDelay)
	{
		InDelay.SetWetModulator(InModulatorSettings);
	});
}

void USourceEffectChorusPreset::SetDryModulator(const FSoundModulationDestinationSettings& InModulatorSettings)
{
	IterateEffects<FSourceEffectChorus>([InModulatorSettings](FSourceEffectChorus& InDelay)
	{
		InDelay.SetDryModulator(InModulatorSettings);
	});
}

void USourceEffectChorusPreset::SetSpreadModulator(const FSoundModulationDestinationSettings& InModulatorSettings)
{
	IterateEffects<FSourceEffectChorus>([InModulatorSettings](FSourceEffectChorus& InDelay)
	{
		InDelay.SetSpreadModulator(InModulatorSettings);
	});
}

void USourceEffectChorusPreset::SetSettings(const FSourceEffectChorusSettings& InSettings)
{
	UpdateSettings(InSettings);
	OnInit();
}
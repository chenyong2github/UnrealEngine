// Copyright Epic Games, Inc. All Rights Reserved.

#include "SourceEffects/SourceEffectChorus.h"

void FSourceEffectChorus::Init(const FSoundEffectSourceInitData& InitData)
{
	bIsActive = true;

	if (USourceEffectChorusPreset* ProcPreset = Cast<USourceEffectChorusPreset>(Preset.Get()))
	{
		const uint32 PresetId = ProcPreset->GetUniqueID();

		DepthMod.Init(InitData.AudioDeviceId, PresetId, true /* bInIsBuffered */);
		FeedbackMod.Init(InitData.AudioDeviceId, PresetId, true /* bInIsBuffered */);
		FrequencyMod.Init(InitData.AudioDeviceId, PresetId, true /* bInIsBuffered */, 0.0f, MAX_FILTER_FREQUENCY);
		WetMod.Init(InitData.AudioDeviceId, PresetId, true /* bInIsBuffered */);
		DryMod.Init(InitData.AudioDeviceId, PresetId, true /* bInIsBuffered */);
		SpreadMod.Init(InitData.AudioDeviceId, PresetId, true /* bInIsBuffered */);
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
			Chorus.SetWetLevel(WetMod.GetSample(SampleIndex));
			Chorus.SetDryLevel(DryMod.GetSample(SampleIndex));
			Chorus.SetSpread(SpreadMod.GetSample(SampleIndex));

			const float Depth = DepthMod.GetSample(SampleIndex);
			Chorus.SetDepth(Audio::EChorusDelays::Left, Depth);
			Chorus.SetDepth(Audio::EChorusDelays::Center, Depth);
			Chorus.SetDepth(Audio::EChorusDelays::Right, Depth);

			const float Feedback = FeedbackMod.GetSample(SampleIndex);
			Chorus.SetFeedback(Audio::EChorusDelays::Left, Feedback);
			Chorus.SetFeedback(Audio::EChorusDelays::Center, Feedback);
			Chorus.SetFeedback(Audio::EChorusDelays::Right, Feedback);

			const float Frequency = FrequencyMod.GetSample(SampleIndex);
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

void FSourceEffectChorus::SetDepthModulator(const FSoundModulationParameterSettings& InModulatorSettings)
{
	DepthMod.UpdateSettings(InModulatorSettings);
}

void FSourceEffectChorus::SetFeedbackModulator(const FSoundModulationParameterSettings& InModulatorSettings)
{
	FeedbackMod.UpdateSettings(InModulatorSettings);
}

void FSourceEffectChorus::SetSpreadModulator(const FSoundModulationParameterSettings& InModulatorSettings)
{
	SpreadMod.UpdateSettings(InModulatorSettings);
}

void FSourceEffectChorus::SetDryModulator(const FSoundModulationParameterSettings& InModulatorSettings)
{
	DryMod.UpdateSettings(InModulatorSettings);
}

void FSourceEffectChorus::SetWetModulator(const FSoundModulationParameterSettings& InModulatorSettings)
{
	WetMod.UpdateSettings(InModulatorSettings);
}

void FSourceEffectChorus::SetFrequencyModulator(const FSoundModulationParameterSettings& InModulatorSettings)
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

void USourceEffectChorusPreset::SetDepthModulator(const FSoundModulationParameterSettings& InModulatorSettings)
{
	IterateEffects<FSourceEffectChorus>([InModulatorSettings](FSourceEffectChorus& InDelay)
	{
		InDelay.SetDepthModulator(InModulatorSettings);
	});
}

void USourceEffectChorusPreset::SetFeedbackModulator(const FSoundModulationParameterSettings& InModulatorSettings)
{
	IterateEffects<FSourceEffectChorus>([InModulatorSettings](FSourceEffectChorus& InDelay)
	{
		InDelay.SetFeedbackModulator(InModulatorSettings);
	});
}

void USourceEffectChorusPreset::SetFrequencyModulator(const FSoundModulationParameterSettings& InModulatorSettings)
{
	IterateEffects<FSourceEffectChorus>([InModulatorSettings](FSourceEffectChorus& InDelay)
	{
		InDelay.SetFrequencyModulator(InModulatorSettings);
	});
}

void USourceEffectChorusPreset::SetWetModulator(const FSoundModulationParameterSettings& InModulatorSettings)
{
	IterateEffects<FSourceEffectChorus>([InModulatorSettings](FSourceEffectChorus& InDelay)
	{
		InDelay.SetWetModulator(InModulatorSettings);
	});
}

void USourceEffectChorusPreset::SetDryModulator(const FSoundModulationParameterSettings& InModulatorSettings)
{
	IterateEffects<FSourceEffectChorus>([InModulatorSettings](FSourceEffectChorus& InDelay)
	{
		InDelay.SetDryModulator(InModulatorSettings);
	});
}

void USourceEffectChorusPreset::SetSpreadModulator(const FSoundModulationParameterSettings& InModulatorSettings)
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
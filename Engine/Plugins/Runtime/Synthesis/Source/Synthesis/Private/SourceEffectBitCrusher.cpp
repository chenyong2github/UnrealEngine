// Copyright Epic Games, Inc. All Rights Reserved.

#include "SourceEffects/SourceEffectBitCrusher.h"

void FSourceEffectBitCrusher::Init(const FSoundEffectSourceInitData& InitData)
{
	bIsActive = true;
	BitCrusher.Init(InitData.SampleRate, InitData.NumSourceChannels);

	if (USourceEffectBitCrusherPreset* ProcPreset = Cast<USourceEffectBitCrusherPreset>(Preset.Get()))
	{
		const uint32 PresetId = ProcPreset->GetUniqueID();
		BitMod.Init(InitData.AudioDeviceId, PresetId, false /* bInIsBuffered */, 1.0f, 24.0f);
		SampleRateMod.Init(InitData.AudioDeviceId, PresetId, false /* bInIsBuffered */, 1.0f, 192000.0f);

		SetBitModulator(ProcPreset->Settings.BitModulation);
		SetSampleRateModulator(ProcPreset->Settings.SampleRateModulation);
	}
}

void FSourceEffectBitCrusher::OnPresetChanged()
{
	GET_EFFECT_SETTINGS(SourceEffectBitCrusher);

	SettingsCopy = Settings;
}

void FSourceEffectBitCrusher::ProcessAudio(const FSoundEffectSourceInputData& InData, float* OutAudioBufferData)
{
	if (BitMod.ProcessControl(SettingsCopy.BitModulation.Value))
	{
		BitCrusher.SetBitDepthCrush(BitMod.GetTarget());
	}

	if (SampleRateMod.ProcessControl(SettingsCopy.SampleRateModulation.Value))
	{
		BitCrusher.SetSampleRateCrush(SampleRateMod.GetTarget());
	}

	BitCrusher.ProcessAudio(InData.InputSourceEffectBufferPtr, InData.NumSamples, OutAudioBufferData);
}

void FSourceEffectBitCrusher::SetBitModulator(const FSoundModulationParameterSettings& InModulatorSettings)
{
	BitMod.UpdateSettings(InModulatorSettings);
}

void FSourceEffectBitCrusher::SetSampleRateModulator(const FSoundModulationParameterSettings& InModulatorSettings)
{
	SampleRateMod.UpdateSettings(InModulatorSettings);
}

void USourceEffectBitCrusherPreset::OnInit()
{
	SetBitModulator(Settings.BitModulation);
	SetSampleRateModulator(Settings.SampleRateModulation);
}

void USourceEffectBitCrusherPreset::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

#if WITH_EDITORONLY_DATA
	if (Ar.IsLoading() || Ar.IsSaving())
	{
		if (Settings.CrushedBits >= 0.0f)
		{
			Settings.BitModulation.Value = Settings.CrushedBits;
			Settings.CrushedBits = -1.0f;
		}

		if (Settings.CrushedSampleRate >= 0.0f)
		{
			Settings.SampleRateModulation.Value = Settings.CrushedSampleRate;
			Settings.CrushedSampleRate = -1.0f;
		}
	}
#endif // WITH_EDITORONLY_DATA
}

void USourceEffectBitCrusherPreset::SetBitModulator(const FSoundModulationParameterSettings& InModulatorSettings)
{
	IterateEffects<FSourceEffectBitCrusher>([InModulatorSettings](FSourceEffectBitCrusher& InCrusher)
	{
		InCrusher.SetBitModulator(InModulatorSettings);
	});
}

void USourceEffectBitCrusherPreset::SetSampleRateModulator(const FSoundModulationParameterSettings& InModulatorSettings)
{
	IterateEffects<FSourceEffectBitCrusher>([InModulatorSettings](FSourceEffectBitCrusher& InCrusher)
	{
		InCrusher.SetSampleRateModulator(InModulatorSettings);
	});
}

void USourceEffectBitCrusherPreset::SetSettings(const FSourceEffectBitCrusherSettings& InSettings)
{
	UpdateSettings(InSettings);
	OnInit();
}
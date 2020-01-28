// Copyright Epic Games, Inc. All Rights Reserved.

#include "SubmixEffects/AudioMixerSubmixEffectReverbFast.h"
#include "AudioMixerEffectsManager.h"
#include "Sound/ReverbEffect.h"
#include "Audio.h"
#include "AudioMixer.h"
#include "DSP/BufferVectorOperations.h"
#include "DSP/ReverbFast.h"


static int32 DisableSubmixReverbCVarFast = 0;
static FAutoConsoleVariableRef CVarDisableSubmixReverbFast(
	TEXT("au.DisableReverbSubmix"),
	DisableSubmixReverbCVarFast,
	TEXT("Disables the reverb submix.\n")
	TEXT("0: Not Disabled, 1: Disabled"),
	ECVF_Default);


static int32 EnableReverbStereoFlipForQuadCVarFast = 0;
static FAutoConsoleVariableRef CVarReverbStereoFlipForQuadFast(
	TEXT("au.EnableReverbStereoFlipForQuad"),
	EnableReverbStereoFlipForQuadCVarFast,
	TEXT("Enables doing a stereo flip for quad reverb when in surround.\n")
	TEXT("0: Not Enabled, 1: Enabled"),
	ECVF_Default);

static int32 DisableQuadReverbCVarFast = 0;
static FAutoConsoleVariableRef CVarDisableQuadReverbCVarFast(
	TEXT("au.DisableQuadReverb"),
	DisableQuadReverbCVarFast,
	TEXT("Disables quad reverb in surround.\n")
	TEXT("0: Not Disabled, 1: Disabled"),
	ECVF_Default);


const float FSubmixEffectReverbFast::MinWetness = 0.0f;
const float FSubmixEffectReverbFast::MaxWetness = 10.f;

FSubmixEffectReverbFast::FSubmixEffectReverbFast()
	: CurrentWetDry(-1.0f, -1.0f)
{
}

void FSubmixEffectReverbFast::Init(const FSoundEffectSubmixInitData& InitData)
{
	LLM_SCOPE(ELLMTag::AudioMixer);

	/* `FPlateReverbFast` produces slightly different quality effect than `FPlateReverb`. Comparing the Init
	 * settings between FSubmixEffectReverb and FSubmixEffectReverbFast slight differences will arise.
	 *
	 * The delay line implementations significantly differ between the `FPlateReverb` and `FPlateReverbFast` classes.
	 * Specifically, the `FPlateReverb` class utilizes linearly interpolated fractional delay line and fractional
	 * delays while the `FPlateReverbFast` class uses integer delay lines and integer delays whenever possible.
	 * Linearly interpolated fractional delay lines introduce a low pass filter dependent upon the fractional portion
	 * of the delay value. As a result, the `FPlateReverb` class produces a darker reverb.
	 */
	Audio::FPlateReverbFastSettings NewSettings;

	NewSettings.EarlyReflections.Decay = 0.9f;
	NewSettings.EarlyReflections.Absorption = 0.7f;
	NewSettings.EarlyReflections.Gain = 1.0f;
	NewSettings.EarlyReflections.PreDelayMsec = 0.0f;
	NewSettings.EarlyReflections.Bandwidth = 0.8f;

	NewSettings.LateReflections.LateDelayMsec = 0.0f;
	NewSettings.LateReflections.LateGainDB = 0.0f;
	NewSettings.LateReflections.Bandwidth = 0.54f;
	NewSettings.LateReflections.Diffusion = 0.60f;
	NewSettings.LateReflections.Dampening = 0.35f;
	NewSettings.LateReflections.Decay = 0.15f;
	NewSettings.LateReflections.Density = 0.85f;

	ReverbParams.SetParams(NewSettings);

	DecayCurve.AddKey(0.0f, 0.99f);
	DecayCurve.AddKey(2.0f, 0.45f);
	DecayCurve.AddKey(5.0f, 0.15f);
	DecayCurve.AddKey(10.0f, 0.1f);
	DecayCurve.AddKey(18.0f, 0.01f);
	DecayCurve.AddKey(19.0f, 0.002f);
	DecayCurve.AddKey(20.0f, 0.0001f);

	PlateReverb = MakeUnique<Audio::FPlateReverbFast>(InitData.SampleRate, 512, NewSettings);
	PlateReverb->EnableEarlyReflections(false);
	PlateReverb->EnableLateReflections(true);
}

void FSubmixEffectReverbFast::OnPresetChanged()
{
	LLM_SCOPE(ELLMTag::AudioMixer);

	GET_EFFECT_SETTINGS(SubmixEffectReverbFast);

	FAudioReverbEffect ReverbEffect;
	ReverbEffect.Density = Settings.Density;
	ReverbEffect.Diffusion = Settings.Diffusion;
	ReverbEffect.Gain = Settings.Gain;
	ReverbEffect.GainHF = Settings.GainHF;
	ReverbEffect.DecayTime = Settings.DecayTime;
	ReverbEffect.DecayHFRatio = Settings.DecayHFRatio;
	ReverbEffect.ReflectionsGain = Settings.ReflectionsGain;
	ReverbEffect.ReflectionsDelay = Settings.ReflectionsDelay;
	ReverbEffect.LateGain = Settings.LateGain;
	ReverbEffect.LateDelay = Settings.LateDelay;
	ReverbEffect.AirAbsorptionGainHF = Settings.AirAbsorptionGainHF;
	ReverbEffect.RoomRolloffFactor = 0.0f; // not used
	ReverbEffect.Volume = Settings.bBypass ? 0.0f : Settings.WetLevel;

	SetEffectParameters(ReverbEffect);

	Audio::FWetDry NewWetDryParams;
	NewWetDryParams.DryLevel = Settings.bBypass ? 1.0f : Settings.DryLevel;
	NewWetDryParams.WetLevel = Settings.bBypass ? 0.0f : FMath::Clamp(Settings.WetLevel, MinWetness, MaxWetness);

	WetDryParams.SetParams(NewWetDryParams);
}

void FSubmixEffectReverbFast::OnProcessAudio(const FSoundEffectSubmixInputData& InData, FSoundEffectSubmixOutputData& OutData)
{
	LLM_SCOPE(ELLMTag::AudioMixer);

	check(InData.NumChannels == 2);
 	if (OutData.NumChannels < 2 || DisableSubmixReverbCVarFast == 1)
	{
		// Not supported
		return;
	}

	UpdateParameters();

	float LastWet = CurrentWetDry.WetLevel;
	float LastDry = CurrentWetDry.DryLevel;
	WetDryParams.GetParams(&CurrentWetDry);

	// Set to most recent if uninitialized (less than 0.0f)
	if (LastWet < 0.0f)
	{
		LastWet = CurrentWetDry.WetLevel;
	}
	if (LastDry < 0.0f)
	{
		LastDry = CurrentWetDry.DryLevel;
	}

	PlateReverb->ProcessAudio(*InData.AudioBuffer, InData.NumChannels, *OutData.AudioBuffer, OutData.NumChannels);
	Audio::FadeBufferFast(*OutData.AudioBuffer, LastWet, CurrentWetDry.WetLevel);
	Audio::MixInBufferFast(*InData.AudioBuffer, *OutData.AudioBuffer, LastDry, CurrentWetDry.DryLevel);
}

void FSubmixEffectReverbFast::SetEffectParameters(const FAudioReverbEffect& InParams)
{
	LLM_SCOPE(ELLMTag::AudioMixer);

	/* `FPlateReverbFast` produces slightly different quality effect than `FPlateReverb`. Comparing the 
	 * settings between FSubmixEffectReverb and FSubmixEffectReverbFast slight differences will arise.
	 *
	 * The delay line implementations significantly differ between the `FPlateReverb` and `FPlateReverbFast` classes.
	 * Specifically, the `FPlateReverb` class utilizes linearly interpolated fractional delay line and fractional
	 * delays while the `FPlateReverbFast` class uses integer delay lines and integer delays whenever possible.
	 * Linearly interpolated fractional delay lines introduce a low pass filter dependent upon the fractional portion
	 * of the delay value. As a result, the `FPlateReverb` class produces a darker reverb.
	 */
	Audio::FPlateReverbFastSettings NewSettings;

	// Early Reflections
	NewSettings.EarlyReflections.Gain = FMath::GetMappedRangeValueClamped({ 0.0f, 3.16f }, { 0.0f, 1.0f }, InParams.ReflectionsGain);
	NewSettings.EarlyReflections.PreDelayMsec = FMath::GetMappedRangeValueClamped({ 0.0f, 0.3f }, { 0.0f, 300.0f }, InParams.ReflectionsDelay);
	NewSettings.EarlyReflections.Bandwidth = FMath::GetMappedRangeValueClamped({ 0.0f, 1.0f }, { 0.0f, 1.0f }, InParams.GainHF);

	// LateReflections
	NewSettings.LateReflections.LateDelayMsec = FMath::GetMappedRangeValueClamped({ 0.0f, 0.1f }, { 0.0f, 100.0f }, InParams.LateDelay);
	NewSettings.LateReflections.LateGainDB = FMath::GetMappedRangeValueClamped({ 0.0f, 1.0f }, { 0.0f, 1.0f }, InParams.Gain);
	NewSettings.LateReflections.Bandwidth = FMath::GetMappedRangeValueClamped({ 0.0f, 1.0f }, { 0.1f, 0.6f }, InParams.AirAbsorptionGainHF);
	NewSettings.LateReflections.Diffusion = FMath::GetMappedRangeValueClamped({ 0.05f, 1.0f }, { 0.0f, 0.95f }, InParams.Diffusion);
	NewSettings.LateReflections.Dampening = FMath::GetMappedRangeValueClamped({ 0.05f, 1.95f }, { 0.0f, 0.999f }, InParams.DecayHFRatio);
	NewSettings.LateReflections.Density = FMath::GetMappedRangeValueClamped({ 0.0f, 0.95f }, { 0.06f, 1.0f }, InParams.Density);

	// Use mapping function to get decay time in seconds to internal linear decay scale value
	const float DecayValue = DecayCurve.Eval(InParams.DecayTime);
	NewSettings.LateReflections.Decay = DecayValue;

	// Convert to db
	NewSettings.LateReflections.LateGainDB = Audio::ConvertToDecibels(NewSettings.LateReflections.LateGainDB);

	// Apply the settings the thread safe settings object
	ReverbParams.SetParams(NewSettings);
}

void FSubmixEffectReverbFast::UpdateParameters()
{
	Audio::FPlateReverbFastSettings NewSettings;
	if (ReverbParams.GetParams(&NewSettings))
	{
		PlateReverb->SetSettings(NewSettings);
	}

	// Check cVars for quad mapping
	Audio::FPlateReverbFastSettings::EQuadBehavior TargetQuadBehavior;
	if (DisableQuadReverbCVarFast)
	{
		// Disable quad mapping.
 		TargetQuadBehavior = Audio::FPlateReverbFastSettings::EQuadBehavior::StereoOnly;
	}
	else if (!DisableQuadReverbCVarFast && EnableReverbStereoFlipForQuadCVarFast)
	{
		// Enable quad flipped mapping
		TargetQuadBehavior = Audio::FPlateReverbFastSettings::EQuadBehavior::QuadFlipped;
	}
	else
	{
		// Enable quad mapping
		TargetQuadBehavior = Audio::FPlateReverbFastSettings::EQuadBehavior::QuadMatched;
	}

	// Check if settings need to be updated
	const Audio::FPlateReverbFastSettings& ReverbFastSettings = PlateReverb->GetSettings();
	if (ReverbFastSettings.QuadBehavior != TargetQuadBehavior)
	{
		// Update quad settings 
		NewSettings = ReverbFastSettings;
		NewSettings.QuadBehavior = TargetQuadBehavior;
		PlateReverb->SetSettings(NewSettings);
	}
}

void USubmixEffectReverbFastPreset::SetSettingsWithReverbEffect(const UReverbEffect* InReverbEffect, const float InWetLevel, const float InDryLevel)
{
	if (InReverbEffect)
	{
		Settings.Density = InReverbEffect->Density;
		Settings.Diffusion = InReverbEffect->Diffusion;
		Settings.Gain = InReverbEffect->Gain;
		Settings.GainHF = InReverbEffect->GainHF;
		Settings.DecayTime = InReverbEffect->DecayTime;
		Settings.DecayHFRatio = InReverbEffect->DecayHFRatio;
		Settings.ReflectionsGain = InReverbEffect->ReflectionsGain;
		Settings.ReflectionsDelay = InReverbEffect->ReflectionsDelay;
		Settings.LateGain = InReverbEffect->LateGain;
		Settings.LateDelay = InReverbEffect->LateDelay;
		Settings.AirAbsorptionGainHF = InReverbEffect->AirAbsorptionGainHF;
		Settings.WetLevel = InWetLevel;
		Settings.DryLevel = InDryLevel;

		Update();
	}
}

void USubmixEffectReverbFastPreset::SetSettings(const FSubmixEffectReverbFastSettings& InSettings)
{
	UpdateSettings(InSettings);
}

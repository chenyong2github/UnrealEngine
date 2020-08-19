// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DSP/DynamicsProcessor.h"
#include "Sound/SoundEffectSubmix.h"
#include "DSP/LinkwitzRileyBandSplitter.h"
#include "SubmixEffects/AudioMixerSubmixEffectDynamicsProcessor.h"
#include "SubmixEffectMultiBandCompressor.generated.h"

USTRUCT(BlueprintType)
struct FDynamicsBandSettings
{
	GENERATED_BODY()

	// Frequency of the crossover between this band and the next. The last band will have this property ignored
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SourceEffect|Preset", meta = (ClampMin = "20.0", ClampMax = "20000.0", UIMin = "20.0", UIMax = "20000"))
	float CrossoverTopFrequency = 20000.f;

	// The amount of time to ramp into any dynamics processing effect in milliseconds.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SourceEffect|Preset", meta = (ClampMin = "1.0", ClampMax = "300.0", UIMin = "1.0", UIMax = "200.0"))
	float AttackTimeMsec = 10.f;

	// The amount of time to release the dynamics processing effect in milliseconds
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SourceEffect|Preset", meta = (ClampMin = "20.0", ClampMax = "5000.0", UIMin = "20.0", UIMax = "5000.0"))
	float ReleaseTimeMsec = 100.f;

	// The threshold at which to perform a dynamics processing operation
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SourceEffect|Preset", meta = (ClampMin = "-72.0", ClampMax = "0.0", UIMin = "-72.0", UIMax = "0.0"))
	float ThresholdDb = -6.f;

	// The dynamics processor ratio -- has different meaning depending on the processor type.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SourceEffect|Preset", meta = (ClampMin = "1.0", ClampMax = "20.0", UIMin = "1.0", UIMax = "20.0"))
	float Ratio = 1.5f;

	// The knee bandwidth of the compressor to use in dB
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SourceEffect|Preset", meta = (ClampMin = "0.0", ClampMax = "20.0", UIMin = "0.0", UIMax = "20.0"))
	float KneeBandwidthDb = 10.f;

	// The input gain of the dynamics processor in dB
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SourceEffect|Preset", meta = (ClampMin = "-12.0", ClampMax = "20.0", UIMin = "-12.0", UIMax = "20.0"))
	float InputGainDb = 0.f;

	// The output gain of the dynamics processor in dB
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SourceEffect|Preset", meta = (ClampMin = "0.0", ClampMax = "20.0", UIMin = "0.0", UIMax = "20.0"))
	float OutputGainDb = 0.f;
};

// A submix dynamics processor
USTRUCT(BlueprintType)
struct FSubmixEffectMultibandCompressorSettings
{
	GENERATED_BODY()

	// Controls how each band will react to audio above its threshold
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SourceEffect|Preset")
	ESubmixEffectDynamicsProcessorType DynamicsProcessorType = ESubmixEffectDynamicsProcessorType::Compressor;

	// Controls how quickly the bands will react to a signal above its threshold
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SourceEffect|Preset")
	ESubmixEffectDynamicsPeakMode PeakMode = ESubmixEffectDynamicsPeakMode::RootMeanSquared;

	// The amount of time to look ahead of the current audio. Allows for transients to be included in dynamics processing.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SourceEffect|Preset",  meta = (ClampMin = "0.0", ClampMax = "50.0", UIMin = "0.0", UIMax = "50.0"))
	float LookAheadMsec = 3.f;

	// Whether or not to average all channels of audio before inputing into the dynamics processor
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SourceEffect|Preset")
	bool bLinkChannels = true;

	// Toggles treating the attack and release envelopes as analog-style vs digital-style. Analog will respond a bit more naturally/slower.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SourceEffect|Preset")
	bool bAnalogMode = true;

	// Turning off FourPole mode will use cheaper, shallower 2-pole crossovers
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SourceEffect|Preset")
	bool bFourPole = true;

	// Each band is a full dynamics processor, affecting at a unique frequency range
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SourceEffect|Preset")
	TArray<FDynamicsBandSettings> Bands;
};

class FSubmixEffectMultibandCompressor : public FSoundEffectSubmix
{
public:
	FSubmixEffectMultibandCompressor() {};

	// Called on an audio effect at initialization on main thread before audio processing begins.
	virtual void Init(const FSoundEffectSubmixInitData& InSampleRate) override;

	// Process the input block of audio. Called on audio thread.
	virtual void OnProcessAudio(const FSoundEffectSubmixInputData& InData, FSoundEffectSubmixOutputData& OutData) override;

	// Called when an audio effect preset is changed
	virtual void OnPresetChanged() override;

	// called from OnPresetChanged when something is changed that needs extra attention
	void Initialize(FSubmixEffectMultibandCompressorSettings& Settings);

protected:
	TArray<float> AudioInputFrame;
	TArray<float> AudioOutputFrame;
	TArray<Audio::FDynamicsProcessor> DynamicsProcessors;
	Audio::FLinkwitzRileyBandSplitter BandSplitter;
	Audio::FMultibandBuffer MultiBandBuffer;

	// cached crossover + band info, so we can check if they need a re-build when editing
	int32 PrevNumBands = 0; 
	TArray<float> PrevCrossovers;
	bool bPrevFourPole = true;

	int32 NumChannels = 0;
	int32 FrameSize = 0;
	float SampleRate = 48000.f;

	bool bInitialized = false;
};

UCLASS(ClassGroup = AudioSourceEffect, meta = (BlueprintSpawnableComponent))
class USubmixEffectMultibandCompressorPreset : public USoundEffectSubmixPreset
{
	GENERATED_BODY()

public:

	EFFECT_PRESET_METHODS(SubmixEffectMultibandCompressor)

	UFUNCTION(BlueprintCallable, Category = "Audio|Effects")
	void SetSettings(const FSubmixEffectMultibandCompressorSettings& InSettings);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SubmixEffectPreset)
	FSubmixEffectMultibandCompressorSettings Settings;
};
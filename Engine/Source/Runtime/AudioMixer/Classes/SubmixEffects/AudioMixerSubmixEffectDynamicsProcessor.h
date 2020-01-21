// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DSP/DynamicsProcessor.h"
#include "Sound/SoundEffectSubmix.h"
#include "Sound/SoundSubmix.h"

#include "AudioMixerSubmixEffectDynamicsProcessor.generated.h"


UENUM(BlueprintType)
enum class ESubmixEffectDynamicsProcessorType : uint8
{
	Compressor = 0,
	Limiter,
	Expander,
	Gate,
	Count UMETA(Hidden)
};

UENUM(BlueprintType)
enum class ESubmixEffectDynamicsPeakMode : uint8
{
	MeanSquared = 0,
	RootMeanSquared,
	Peak,
	Count UMETA(Hidden)
};

UENUM(BlueprintType)
enum class ESubmixEffectDynamicsChannelLinkMode : uint8
{
	Disabled = 0,
	Average,
	Peak,
	Count UMETA(Hidden)
};

USTRUCT(BlueprintType)
struct AUDIOMIXER_API FSubmixEffectDynamicProcessorFilterSettings
{
	GENERATED_USTRUCT_BODY()

	// Whether or not filter is enabled
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Filter, meta = (DisplayName = "Enabled"))
	uint8 bEnabled : 1;

	// The cutoff frequency of the HPF applied to key signal
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Filter, meta = (DisplayName = "Cutoff (Hz)", EditCondition = "bEnabled", ClampMin = "20.0", ClampMax = "20000.0", UIMin = "20.0", UIMax = "20000.0"))
	float Cutoff;

	// The gain of the filter shelf applied to the key signal
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Filter, meta = (DisplayName = "Gain (dB)", EditCondition = "bEnabled", ClampMin = "-60.0", ClampMax = "6.0", UIMin = "-60.0", UIMax = "6.0"))
	float GainDb;

	FSubmixEffectDynamicProcessorFilterSettings()
		: bEnabled(false)
		, Cutoff(20.0f)
		, GainDb(0.0f)
	{
	}
};

// Submix dynamics processor settings
USTRUCT(BlueprintType)
struct AUDIOMIXER_API FSubmixEffectDynamicsProcessorSettings
{
	GENERATED_USTRUCT_BODY()

	// Type of processor to apply
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = General, meta = (DisplayName = "Type"))
	ESubmixEffectDynamicsProcessorType DynamicsProcessorType;

	// Mode of peak detection used on input key signal
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Dynamics)
	ESubmixEffectDynamicsPeakMode PeakMode;

	// Mode of peak detection if key signal is multi-channel
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Dynamics)
	ESubmixEffectDynamicsChannelLinkMode LinkMode;

	// The input gain of the dynamics processor
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = General, meta = (DisplayName = "Input Gain (dB)", ClampMin = "-12.0", ClampMax = "20.0", UIMin = "-12.0", UIMax = "20.0"))
	float InputGainDb;

	// The threshold at which to perform a dynamics processing operation
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Dynamics, meta = (DisplayName = "Threshold (dB)", ClampMin = "-60.0", ClampMax = "0.0", UIMin = "-60.0", UIMax = "0.0"))
	float ThresholdDb;

	// The dynamics processor ratio used for compression/expansion
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Dynamics, meta = (
		EditCondition = "DynamicsProcessorType == ESubmixEffectDynamicsProcessorType::Compressor || DynamicsProcessorType == ESubmixEffectDynamicsProcessorType::Expander",
		ClampMin = "1.0", ClampMax = "20.0", UIMin = "1.0", UIMax = "20.0"))
	float Ratio;

	// The knee bandwidth of the processor to use
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Dynamics, meta = (DisplayName = "Knee (dB)", ClampMin = "0.0", ClampMax = "20.0", UIMin = "0.0", UIMax = "20.0"))
	float KneeBandwidthDb;

	// The amount of time to look ahead of the current audio (Allows for transients to be included in dynamics processing)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Response,  meta = (DisplayName = "Look Ahead (ms)", ClampMin = "0.0", ClampMax = "50.0", UIMin = "0.0", UIMax = "50.0"))
	float LookAheadMsec;

	// The amount of time to ramp into any dynamics processing effect
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Response, meta = (DisplayName = "AttackTime (ms)", ClampMin = "1.0", ClampMax = "300.0", UIMin = "1.0", UIMax = "200.0"))
	float AttackTimeMsec;

	// The amount of time to release the dynamics processing effect
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Response, meta = (DisplayName = "Release Time (ms)", ClampMin = "20.0", ClampMax = "5000.0", UIMin = "20.0", UIMax = "5000.0"))
	float ReleaseTimeMsec;

	// (Coming soon) If set, uses output of provided submix as modulator of input signal for dynamics processor (Uses input signal as default modulator)
	UPROPERTY(VisibleAnywhere, Category = Sidechain)
	USoundSubmix* ExternalSubmix;

	UPROPERTY()
	uint8 bChannelLinked_DEPRECATED : 1;

	// Toggles treating the attack and release envelopes as analog-style vs digital-style (Analog will respond a bit more naturally/slower)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Response)
	uint8 bAnalogMode : 1;

	// Audition the key modulation signal, bypassing enveloping and processing the input signal.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Sidechain, meta = (DisplayName = "Key Audition"))
	uint8 bKeyAudition : 1;

	// Gain to apply to key signal (external signal if supplied or input signal if disabled)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Sidechain, meta = (DisplayName = "Key Gain (dB)", ClampMin = "-60.0", ClampMax = "20.0", UIMin = "-60.0", UIMax = "20.0"))
	float KeyGainDb;

	// The output gain of the dynamics processor
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Output, meta = (DisplayName = "Output Gain (dB)", ClampMin = "-60.0", ClampMax = "0.0", UIMin = "-60.0", UIMax = "20.0"))
	float OutputGainDb;

	// High Shelf filter settings for key signal (external signal if supplied or input signal if not)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Sidechain, meta = (DisplayName = "Key Highshelf"))
	FSubmixEffectDynamicProcessorFilterSettings KeyHighshelf;

	// Low Shelf filter settings for key signal (external signal if supplied or input signal if not)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Sidechain, meta = (DisplayName = "Key Lowshelf"))
	FSubmixEffectDynamicProcessorFilterSettings KeyLowshelf;

	FSubmixEffectDynamicsProcessorSettings()
		: DynamicsProcessorType(ESubmixEffectDynamicsProcessorType::Compressor)
		, PeakMode(ESubmixEffectDynamicsPeakMode::RootMeanSquared)
		, LinkMode(ESubmixEffectDynamicsChannelLinkMode::Average)
		, InputGainDb(0.0f)
		, ThresholdDb(-6.0f)
		, Ratio(1.5f)
		, KneeBandwidthDb(10.0f)
		, LookAheadMsec(3.0f)
		, AttackTimeMsec(10.0f)
		, ReleaseTimeMsec(100.0f)
		, ExternalSubmix(nullptr)
		, bChannelLinked_DEPRECATED(true)
		, bAnalogMode(true)
		, bKeyAudition(false)
		, KeyGainDb(0.0f)
		, OutputGainDb(0.0f)
	{
		KeyLowshelf.Cutoff = 20000.0f;
	}
};


class AUDIOMIXER_API FSubmixEffectDynamicsProcessor : public FSoundEffectSubmix
{
public:
	FSubmixEffectDynamicsProcessor();

	// Called on an audio effect at initialization on main thread before audio processing begins.
	virtual void Init(const FSoundEffectSubmixInitData& InSampleRate) override;

	// Process the input block of audio. Called on audio thread.
	virtual void OnProcessAudio(const FSoundEffectSubmixInputData& InData, FSoundEffectSubmixOutputData& OutData) override;

	// Called when an audio effect preset is changed
	virtual void OnPresetChanged() override;

protected:
	TArray<float> AudioInputFrame;
	TArray<float> AudioOutputFrame;
	Audio::FDynamicsProcessor DynamicsProcessor;
};

UCLASS(ClassGroup = AudioSourceEffect, meta = (BlueprintSpawnableComponent))
class AUDIOMIXER_API USubmixEffectDynamicsProcessorPreset : public USoundEffectSubmixPreset
{
	GENERATED_BODY()

public:

	EFFECT_PRESET_METHODS(SubmixEffectDynamicsProcessor)

	void Serialize(FStructuredArchive::FRecord Record) override;

	UFUNCTION(BlueprintCallable, Category = "Audio|Effects")
	void SetSettings(const FSubmixEffectDynamicsProcessorSettings& InSettings);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SubmixEffectPreset, meta = (ShowOnlyInnerProperties))
	FSubmixEffectDynamicsProcessorSettings Settings;
};
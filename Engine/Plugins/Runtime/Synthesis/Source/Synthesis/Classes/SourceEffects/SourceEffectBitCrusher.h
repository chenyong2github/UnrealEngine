// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "DSP/BitCrusher.h"
#include "Sound/SoundEffectSource.h"
#include "Sound/SoundModulationDestination.h"

#include "SourceEffectBitCrusher.generated.h"


USTRUCT(BlueprintType)
struct SYNTHESIS_API FSourceEffectBitCrusherSettings
{
	GENERATED_USTRUCT_BODY()

#if WITH_EDITORONLY_DATA
	UPROPERTY(meta = (PropertyDeprecated))
	float CrushedSampleRate;
#endif // if WITH_EDITORONLY_DATA

	// The reduced frequency to use for the audio stream. 
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SourceEffect|Preset", meta = (DisplayName = "Sample Rate", AudioParam = "SampleRate", UIMin = "500.0", UIMax = "16000.0"))
	FSoundModulationDestinationSettings SampleRateModulation;

#if WITH_EDITORONLY_DATA
	UPROPERTY(meta = (PropertyDeprecated))
	float CrushedBits;
#endif // if WITH_EDITORONLY_DATA

	// The reduced bit depth to use for the audio stream
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SourceEffect|Preset", meta = (DisplayName = "Bit Depth", AudioParam = "BitDepth", ClampMin = "1.0", ClampMax = "24.0", UIMin = "1.0", UIMax = "16.0"))
	FSoundModulationDestinationSettings BitModulation;

	FSourceEffectBitCrusherSettings()
#if WITH_EDITORONLY_DATA
		: CrushedSampleRate(8000.0f)
		, CrushedBits(8.0f)
#endif // if WITH_EDITORONLY_DATA
	{
#if WITH_EDITORONLY_DATA
		SampleRateModulation.Value = CrushedSampleRate;
		BitModulation.Value = CrushedBits;
#endif // WITH_EDITORONLY_DATA
	}
};

class SYNTHESIS_API FSourceEffectBitCrusher : public FSoundEffectSource
{
public:
	// Called on an audio effect at initialization on main thread before audio processing begins.
	virtual void Init(const FSoundEffectSourceInitData& InitData) override;
	
	// Called when an audio effect preset is changed
	virtual void OnPresetChanged() override;

	// Process the input block of audio. Called on audio thread.
	virtual void ProcessAudio(const FSoundEffectSourceInputData& InData, float* OutAudioBufferData) override;

	void SetSampleRateModulator(const FSoundModulationDestinationSettings& InModulatorSettings);

	void SetBitModulator(const FSoundModulationDestinationSettings& InModulatorSettings);

protected:
	Audio::FBitCrusher BitCrusher;

	FSourceEffectBitCrusherSettings SettingsCopy;

	Audio::FModulationDestination SampleRateMod;
	Audio::FModulationDestination BitMod;
};

UCLASS(ClassGroup = AudioSourceEffect, meta = (BlueprintSpawnableComponent))
class SYNTHESIS_API USourceEffectBitCrusherPreset : public USoundEffectSourcePreset
{
	GENERATED_BODY()

public:
	EFFECT_PRESET_METHODS(SourceEffectBitCrusher)

	virtual void OnInit() override;

	virtual void Serialize(FArchive& Ar) override;

	virtual FColor GetPresetColor() const override { return FColor(196.0f, 185.0f, 121.0f); }

	UFUNCTION(BlueprintCallable, Category = "Audio|Effects")
	void SetBitModulator(const FSoundModulationDestinationSettings& InModulatorSettings);

	UFUNCTION(BlueprintCallable, Category = "Audio|Effects")
	void SetSampleRateModulator(const FSoundModulationDestinationSettings& InModulatorSettings);

	UFUNCTION(BlueprintCallable, Category = "Audio|Effects")
	void SetSettings(const FSourceEffectBitCrusherSettings& InSettings);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio|Effects")
	FSourceEffectBitCrusherSettings Settings;
};

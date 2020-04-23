// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SynthComponent.h"
#include "Sound/SoundBase.h"
#include "DSP/SinOsc.h"
#include "Sound/SoundGenerator.h"
#include "SynthComponentToneGenerator.generated.h"

class FToneGenerator : public ISoundGenerator
{
public:
	FToneGenerator(int32 InSampleRate, int32 InNumChannels, int32 InFrequency, float InVolume);
	virtual ~FToneGenerator();

	//~ Begin FSoundGenerator 
	virtual int32 GetNumChannels() { return NumChannels; };
	virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;
	//~ End FSoundGenerator

	void SetFrequency(float InFrequency);
	void SetVolume(float InVolume);

private:
	int32 NumChannels = 2;
	Audio::FSineOsc SineOsc;
	Audio::AlignedFloatBuffer Buffer;
};

UCLASS(ClassGroup = Synth, meta = (BlueprintSpawnableComponent))
class SYNTHESIS_API USynthComponentToneGenerator : public USynthComponent
{
	GENERATED_BODY()

	USynthComponentToneGenerator(const FObjectInitializer& ObjInitializer);
	virtual ~USynthComponentToneGenerator();

public:
	// The frequency (in hz) of the tone generator.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tone Generator", meta = (ClampMin = "10.0", ClampMax = "20000.0"))
	float Frequency;

	// The linear volume of the tone generator.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tone Generator", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Volume;

	// Sets the frequency of the tone generator
	UFUNCTION(BlueprintCallable, Category = "Tone Generator")
	void SetFrequency(float InFrequency);

	// Sets the volume of the tone generator
	UFUNCTION(BlueprintCallable, Category = "Tone Generator")
	void SetVolume(float InVolume);

	virtual ISoundGeneratorPtr CreateSoundGenerator(int32 InSampleRate, int32 InNumChannels) override;

public:

private:
	ISoundGeneratorPtr ToneGenerator;
};

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "UObject/Class.h"
#include "Engine/EngineTypes.h"
#include "Async/AsyncWork.h"
#include "DSP/BufferVectorOperations.h"
#include "Curves/CurveFloat.h"
#include "AudioDevice.h"
#include "MotoSynthPreset.generated.h"

class UMotoSynthSource;
class FMotoSynthEngine;

USTRUCT(BlueprintType)
struct MOTOSYNTH_API FMotoSynthRuntimeSettings
{
	GENERATED_USTRUCT_BODY()

	// If the synth tone is enabled
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Synth")
	bool bSynthToneEnabled = false;

	// The volume of the synth tone	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Synth", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0", EditCondition="bSynthToneEnabled"))
	float SynthToneVolume = 0.0f;

	// The filter frequency of the synth tone
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Synth", meta = (ClampMin = "20.0", ClampMax = "10000.0", UIMin = "20.0", UIMax = "10000.0", EditCondition = "bSynthToneEnabled"))
	float SynthToneFilterFrequency = 500.0f;

	// Octave shift of the synth
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Synth", meta = (ClampMin = "-3", ClampMax = "3", UIMin = "-3", UIMax = "3", EditCondition = "bSynthToneEnabled"))
	int32 SynthOctaveShift = 0;

	// If the granular engine is enabled
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Granular Engine")
	bool bGranularEngineEnabled = true;

	// The volume of the granular engine
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Granular Engine", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0", EditCondition = "bGranularEngineEnabled"))
	float GranularEngineVolume = 1.0f;

	// The pitch scale of the granular engine
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Granular Engine", meta = (ClampMin = "0.001", UIMin = "0.001", EditCondition = "bGranularEngineEnabled"))
	float GranularEnginePitchScale = 1.0f;

	// The volume of the granular engine
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Granular Engine", meta = (ClampMin = "0", UIMin = "0", EditCondition = "bGranularEngineEnabled"))
	int32 NumSamplesToCrossfadeBetweenGrains = 10;

	// How many grain-table entries to use per runtime grain
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Granular Engine", meta = (ClampMin = "1", ClampMax = "20", UIMin = "1", UIMax = "20", EditCondition = "bGranularEngineEnabled"))
	int32 NumGrainTableEntriesPerGrain = 3;

	// Random grain table offset for cases where RPM is constant. Allows random shuffling of grains to avoid a robotic sound.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Granular Engine", meta = (ClampMin = "0", ClampMax = "50", UIMin = "0", UIMax = "50", EditCondition = "bGranularEngineEnabled"))
	int32 GrainTableRandomOffsetForConstantRPMs = 20;

	// Number of samples to cross fade grains when on a constant-RPM state. More crossfaded samples can reduce the robotic sound.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Granular Engine", meta = (ClampMin = "0", ClampMax = "200", UIMin = "0", UIMax = "200", EditCondition = "bGranularEngineEnabled"))
	int32 GrainCrossfadeSamplesForConstantRPMs = 20;

	// Motosynth source to use for granular engine acceleration
	UPROPERTY(EditAnywhere, Category = "Granular Engine", meta = (EditCondition = "bGranularEngineEnabled"))
	UMotoSynthSource* AccelerationSource = nullptr;

	// Motosynth source to use for granular engine deceleration
	UPROPERTY(EditAnywhere, Category = "Granular Engine", meta = (EditCondition = "bGranularEngineEnabled"))
	UMotoSynthSource* DecelerationSource = nullptr;

	// If the stereo widener is enabled
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects")
	bool bStereoWidenerEnabled = true;
	
	// If the stereo widener is enabled
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects", meta = (ClampMin = "0.0", ClampMax = "200.0", UIMin = "0.0", UIMax = "200.0", EditCondition = "bStereoWidenerEnabled"))
	float StereoDelayMsec = 25.0f;

	// Amount of feedback for stereo widener
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0", EditCondition = "bStereoWidenerEnabled"))
	float StereoFeedback = 0.37;

	// Wet level of stereo delay used for stereo widener
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0", EditCondition = "bStereoWidenerEnabled"))
	float StereoWidenerWetlevel = 0.68f;

	// Dry level of stereo delay used for stereo widener
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0", EditCondition = "bStereoWidenerEnabled"))
	float StereoWidenerDryLevel = 0.8f;

	// Delay ratio of left/right channels for stereo widener effect
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0", EditCondition = "bStereoWidenerEnabled"))
	float StereoWidenerDelayRatio = 0.43f;

	// Delay ratio of left/right channels for stereo widener effect
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects", meta = (EditCondition = "bStereoWidenerEnabled"))
	bool bStereoWidenerFilterEnabled = true;

	// Delay ratio of left/right channels for stereo widener effect
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects", meta = (ClampMin = "20.0", ClampMax = "20000.0", UIMin = "20.0", UIMax = "20000.0", EditCondition = "bStereoWidenerEnabled && bStereoWidenerFilterEnabled"))
	float StereoWidenerFilterFrequency = 4000.0f;

	// Delay ratio of left/right channels for stereo widener effect
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects", meta = (ClampMin = "0.0", ClampMax = "2.0", UIMin = "0.0", UIMax = "2.0", EditCondition = "bStereoWidenerEnabled && bStereoWidenerFilterEnabled"))
	float StereoWidenerFilterQ = 0.5f;
};


#if WITH_EDITOR
/**
* FMotoSynthEnginePreviewer
* Class used to render the moto synth audio in the content browser. Used to preview the moto synth engine without running PIE.
*/
class FMotoSynthEnginePreviewer : public ISubmixBufferListener
{
public:
	FMotoSynthEnginePreviewer();
	virtual ~FMotoSynthEnginePreviewer();

	void StartPreviewing();
	void StopPreviewing();
	void Reset();

	void SetSettings(const FMotoSynthRuntimeSettings& InSettings);
	void SetPreviewRPMCurve(const FRichCurve& InRPMCurve);

	// ISubmixBufferListener
	void OnNewSubmixBuffer(const USoundSubmix* OwningSubmix, float* AudioData, int32 NumSamples, int32 NumChannels, const int32 InSampleRate, double AudioClock) override;
	// ~ ISubmixBufferListener

private:
	FCriticalSection PreviewEngineCritSect;
	FRichCurve PreviewRPMCurve;

	float CurrentPreviewCurveStartTime = 0.0f;
	float CurrentPreviewCurveTime = 0.0f;

	TArray<float> OutputBuffer;
	TUniquePtr<FMotoSynthEngine> SynthEngine;

	FVector2D RPMRange;

	UMotoSynthPreset* MotoSynthPreset;
	FMotoSynthRuntimeSettings Settings;

	bool bRegistered = false;
	bool bEngineInitialized = false;
	bool bPreviewFinished = false;
};
#endif // WITH_EDITOR

/** 
* Asset used to store moto synth preset data.
*/
UCLASS(ClassGroup = Synth, meta = (BlueprintSpawnableComponent))
class MOTOSYNTH_API UMotoSynthPreset : public UObject
{
	GENERATED_BODY()

public:

	virtual void BeginDestroy() override;

#if WITH_EDITORONLY_DATA
	// Engine preview RPM curve
	UPROPERTY(EditAnywhere, Category = "Engine Preview")
	FRuntimeFloatCurve EnginePreviewRPMCurve;
#endif // #if WITH_EDITORONLY_DATA

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = MotoSynthSettings, Meta = (ShowOnlyInnerProperties))
	FMotoSynthRuntimeSettings Settings;

#if WITH_EDITOR
	UFUNCTION(Category = "Engine Preview", meta = (CallInEditor = "true"))
	void StartEnginePreview();

	UFUNCTION(Category = "Engine Preview", meta = (CallInEditor = "true"))
	void StopEnginePreview();

	// Dumps memory usage of the preset (i.e. of the source assets)
	UFUNCTION(Category = "Memory", meta = (CallInEditor = "true"))
	void DumpRuntimeMemoryUsage();

	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

#if WITH_EDITORONLY_DATA
	// The engine previewer which is using this preset
	FMotoSynthEnginePreviewer EnginePreviewer;
#endif
};



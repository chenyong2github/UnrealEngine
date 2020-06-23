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
#include "Containers/SortedMap.h"
#include "AudioDevice.h"
#include "DSP/Osc.h"
#include "DSP/Filter.h"
#include "DSP/DelayStereo.h"
#include "MotoSynthSourceAsset.generated.h"

class UMotoSynthSource;
class FMotoSynthEnginePreviewer;

USTRUCT()
struct FGrainTableEntry
{
	GENERATED_BODY()

	UPROPERTY()
	int32 SampleIndex = 0;

	// The RPM of the grain when it starts
	UPROPERTY()
	float RPM = 0.0f;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	int32 AnalysisSampleIndex = 0;
#endif
};

#if WITH_EDITOR
// Class for playing a match tone for estimating RPMs
class FRPMEstimationSynthTone : public ISubmixBufferListener
{
public:
	FRPMEstimationSynthTone();
	virtual ~FRPMEstimationSynthTone();

	void StartTestTone(float InVolume);
	void StopTestTone();

	void Reset();
	void SetAudioFile(float* InBufferPtr, int32 NumBufferSamples, int32 SampleRate);
	void SetPitchCurve(FRichCurve& InRPMCurve);
	bool IsDone() const { return CurrentFrame >= NumBufferSamples; }

	// ISubmixBufferListener
	void OnNewSubmixBuffer(const USoundSubmix* OwningSubmix, float* AudioData, int32 NumSamples, int32 NumChannels, const int32 SampleRate, double AudioClock) override;
	// ~ISubmixBufferListener

private:
	// Critical section used to set params from game thread while synth is running
	FCriticalSection CallbackCritSect;

	// Scratch buffer to generate audio into
	TArray<float> ScratchBuffer;
	float* AudioFileBufferPtr = nullptr;
	int32 NumBufferSamples = 0;
	Audio::FOsc Osc;
	Audio::FLadderFilter Filter;
	int32 SampleRate = 0;
	FRichCurve RPMCurve;
	int32 CurrentFrame = 0;
	float VolumeScale = 1.0f;
	bool bRegistered = false;
};
#endif // WITH_EDITOR

struct FMotoSynthData
{
	// The raw audio source used for the data
	TArray<float> AudioSource;

	// The sample rate of the source file
	int32 SourceSampleRate = 0;

	// The RPM pitch curve used for the source
	FRichCurve RPMCurve;

	// The grain table derived during editor time
	TArray<FGrainTableEntry> GrainTable;
};

class FMotoSynthGrainRuntime
{
public:
	void Init(const TArrayView<const float>& InGrainView, int32 InNumSamplesCrossfade, float InGrainStartRPM, float InGrainEndRPM, float InStartingRPM);

	// Generates a sample from the grain. Returns true if fading out
	float GenerateSample();

	// Returns true if the grain is nearing its end (and a new grain needs to start fading in
	bool IsNearingEnd() const;

	// If the grain is done
	bool IsDone() const;

	// Updates the grain's RPM
	void SetRPM(int32 InRPM);

private:
	TArrayView<const float> GrainArrayView;
	float CurrentSampleIndex = 0.0f;
	float FadeSamples = 0;
	float FadeOutStartIndex = 0.0f;
	float GrainPitchScale = 1.0f;
	float CurrentRuntimeRPM = 0.0f;
	float GrainRPMStart = 0.0f;
	float GrainRPMDelta = 0.0f;
	float StartingRPM = 0.0f;
};

USTRUCT(BlueprintType)
struct SYNTHESIS_API FMotoSynthRuntimeSettings
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

	// The volume of the granular engine
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Granular Engine", meta = (ClampMin = "0", ClampMax = "100", UIMin = "0", UIMax = "100", EditCondition = "bGranularEngineEnabled"))
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
	UMotoSynthSource* AccelerationSource;

	// Motosynth source to use for granular engine deceleration
	UPROPERTY(EditAnywhere, Category = "Granular Engine", meta = (EditCondition = "bGranularEngineEnabled"))
	UMotoSynthSource* DecelerationSource;

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

UCLASS()
class SYNTHESIS_API UMotoSynthPreset : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = MotoSynthSettings, Meta = (ShowOnlyInnerProperties))
	FMotoSynthRuntimeSettings Settings;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

#if WITH_EDITORONLY_DATA
	// The engine previewer which is using this preset
	FMotoSynthEnginePreviewer* EnginePreviewer = nullptr;
#endif
};

class SYNTHESIS_API IMotoSynthEngine
{
public:
	virtual void SetSettings(const FMotoSynthRuntimeSettings& InSettings) = 0;
};

// Class for granulating an engine
class SYNTHESIS_API FMotoSynthEngine : public ISoundGenerator, 
									   public IMotoSynthEngine
{
public:
	FMotoSynthEngine();
	~FMotoSynthEngine();

	// Queries if the engine is enabled at all. Checks a cvar.
	static bool IsMotoSynthEngineEnabled();

	//~ Begin IMotoSynthEngine
	void SetSettings(const FMotoSynthRuntimeSettings& InSettings) final;
	//~ End IMotoSynthEngine

	//~ Begin FSoundGenerator 
	virtual int32 GetDesiredNumSamplesToRenderPerCallback() const { return 256; }
	virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;
	//~ End FSoundGenerator

	void Init(int32 InSampleRate);
	void Reset();

	// Sets all the source data for the moto synth
	void SetSourceData(FMotoSynthData& InAccelerationData, FMotoSynthData& InDecelerationData);

	// Returns the min and max RPM range, taking into account the acceleration and deceleration data.
	void GetRPMRange(FVector2D& OutRPMRange);

	// Sets the RPM directly. Used if the engine is in ManualRPM mode. Will be ignored if we're in simulation mode.
	void SetRPM(float InRPM, float InTimeSec = 0.0f);

private:
	FVector2D GetRPMRange(FMotoSynthData& InAccelerationData, FMotoSynthData& InDecelerationData);

	void GenerateGranularEngine(float* OutAudio, int32 NumSamples);

	bool NeedsSpawnGrain();
	void SpawnGrain(int32& StartingIndex, const FMotoSynthData& SynthData);

	float RendererSampleRate = 0.0f;

	// The current RPM state
	float CurrentRPM = 0.0f;
	float PreviousRPM = 0.0f;
	float CurrentRPMSlope = 0.0f;
	float PreviousRPMSlope = 0.0f;
	float TargetRPM = 0.0f;
	float StartingRPM = 0.0f;
	float RPMFadeTime = 0.0f;
	float CurrentRPMTime = 0.0f;

	int32 CurrentAccelerationSourceDataIndex = 0;
	int32 CurrentDecelerationSourceDataIndex = 0;

	// The source data
	FMotoSynthData AccelerationSourceData;
	FMotoSynthData DecelerationSourceData;

	FVector2D RPMRange;
	FVector2D RPMRange_RendererCallback;

	// Number of samples to use to do a grain crossfade. Smooths out discontinuities on grain boundaries.
	int32 GrainCrossfadeSamples = 10;
	int32 NumGrainTableEntriesPerGrain = 3;
	int32 GrainTableRandomOffsetForConstantRPMs = 20;
	int32 GrainCrossfadeSamplesForConstantRPMs = 40;

	// The grain pool for runtime generation of audio
	TArray<FMotoSynthGrainRuntime> GrainPool;

	// The grain state management arrays
	TArray<int32> ActiveGrains; // Grains actively generating and not fading out
	TArray<int32> FreeGrains; // Grain indicies which are free to be used. max size should be equal to grain pool size.

	TArray<float> SynthBuffer;
	FVector2D SynthFilterFreqRange = { 100.0f, 5000.0f };
	Audio::FLadderFilter SynthFilter;
	Audio::FOsc SynthOsc;
	int32 SynthPitchUpdateSampleIndex = 0;
	int32 SynthPitchUpdateDeltaSamples = 1023;

	// Stereo delay to "widen" the moto synth output
	Audio::FDelayStereo DelayStereo;

	// Mono scratch buffer for engine generation
	TArray<float> GrainEngineBuffer;

	float SynthToneVolume = 0.0f;
	int32 SynthOctaveShift = 0;
	float SynthFilterFrequency = 500.0f;
	float GranularEngineVolume = 1.0f;
	float TargetGranularEngineVolume = 1.0f;

	bool bWasAccelerating = false;
	bool bSynthToneEnabled = false;
	bool bGranularEngineEnabled = true;
	bool bStereoWidenerEnabled = true;
};

#if WITH_EDITOR
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
	FMotoSynthEngine SynthEngine;

	FVector2D RPMRange;

	UMotoSynthPreset* MotoSynthPreset;
	FMotoSynthRuntimeSettings Settings;

	bool bRegistered = false;
	bool bEngineInitialized = false;
	bool bPreviewFinished = false;
};
#endif // WITH_EDITOR

/*
 UMotoSynthSource
 UAsset used to represent Imported MotoSynth Sources
*/
UCLASS()
class SYNTHESIS_API UMotoSynthSource : public UObject
{
	GENERATED_BODY()

public:
	UMotoSynthSource();
	~UMotoSynthSource();

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category = "Engine Preview")
	UMotoSynthPreset* MotoSynthPreset;

	// Engine preview RPM curve
	UPROPERTY(EditAnywhere, Category = "Engine Preview")
	FRuntimeFloatCurve EnginePreviewRPMCurve;

	// The source to sue for the moto synth source
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grain Table | Analysis")
	USoundWave* SoundWaveSource;
#endif // #if WITH_EDITORONLY_DATA

	// A curve to define the RPM contour from the min and max estimated RPM 
	// Curve values are non-normalized and accurate to time
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grain Table | Analysis")
	FRuntimeFloatCurve RPMCurve;

#if WITH_EDITORONLY_DATA

	// Sets the volume of the RPM curve synth for testing RPM curve to source
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grain Table | Analysis")
	float RPMSynthVolume = 1.0f;

	// Whether not to enable a low pass filter frequency before analyzing the audio file
	UPROPERTY(EditAnywhere, Category = "Grain Table | Filtering")
	bool bEnableFilteringForAnalysis = true;

	// Frequency of a low pass filter to apply before running grain table analysis
	UPROPERTY(EditAnywhere, Category = "Grain Table | Filtering", meta = (ClampMin = "0.0", ClampMax = "20000.0", EditCondition = "bEnableFiltering"))
	float LowPassFilterFrequency = 500.0f;

	// Whether not to enable a low pass filter frequency before analyzing the audio file
	UPROPERTY(EditAnywhere, Category = "Grain Table | Filtering", meta = (ClampMin = "0.0", ClampMax = "20000.0", EditCondition = "bEnableFiltering"))
	float HighPassFilterFrequency = 0.0f;

	// Whether not to enable a dynamics processor to the analysis step
	UPROPERTY(EditAnywhere, Category = "Grain Table | Dynamics Processor")
	bool bEnableDynamicsProcessorForAnalysis = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grain Table | Dynamics Processor", meta = (ClampMin = "0.0", ClampMax = "50.0", UIMin = "0.0", UIMax = "50.0", EditCondition = "bEnableDynamicsProcessor"))
	float DynamicsProcessorLookahead = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Grain Table | Dynamics Processor", meta = (ClampMin = "-20.0", ClampMax = "100.0", EditCondition = "bEnableDynamicsProcessor"))
	float DynamicsProcessorInputGainDb = 20.0f;

	UPROPERTY(EditAnywhere, Category = "Grain Table | Dynamics Processor", meta = (ClampMin = "1.0", ClampMax = "20.0", EditCondition = "bEnableDynamicsProcessor"))
	float DynamicsProcessorRatio = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grain Table | Dynamics Processor", meta = (ClampMin = "0.0", ClampMax = "20.0", UIMin = "0.0", UIMax = "20.0", EditCondition = "bEnableDynamicsProcessor"))
	float DynamicsKneeBandwidth = 2.0f;

	UPROPERTY(EditAnywhere, Category = "Grain Table | Dynamics Processor", meta = (ClampMin = "-60.0", ClampMax = "0.0", EditCondition = "bEnableDynamicsProcessor"))
	float DynamicsProcessorThreshold = -6.0f;

	UPROPERTY(EditAnywhere, Category = "Grain Table | Dynamics Processor", meta = (ClampMin = "0.0", ClampMax = "5000.0", EditCondition = "bEnableDynamicsProcessor"))
	float DynamicsProcessorAttackTimeMsec = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Grain Table | Dynamics Processor", meta = (ClampMin = "0.0", ClampMax = "5000.0", EditCondition = "bEnableDynamicsProcessor"))
	float DynamicsProcessorReleaseTimeMsec = 20.0f;

	UPROPERTY(EditAnywhere, Category = "Grain Table | Normalization")
	bool bEnableNormalizationForAnalysis = true;

	UPROPERTY(EditAnywhere, Category = "Grain Table | Grain Table Algorithm")
	int32 SampleShiftOffset = 68;

	// A samples to use to calibrate when an engine cycle begins
	UPROPERTY(EditAnywhere, Category = "Grain Table | Grain Table Algorithm")
	int32 RPMCycleCalibrationSample = 0;

	// The end of the first cycle sample. Cut the source file to start exactly on the cycle start
	UPROPERTY(EditAnywhere, Category = "Grain Table | Grain Table Algorithm")
	int32 RPMFirstCycleSampleEnd = 0;

	UPROPERTY(EditAnywhere, Category = "Grain Table | Grain Table Algorithm")
	int32 RPMEstimationOctaveOffset = 0;

	// Whether not to write the audio used for analysis to a wav file
	UPROPERTY(EditAnywhere, Category = "Grain Table | File Writing")
	bool bWriteAnalysisInputToFile = true;

	// The path to write the audio analysis data (LPF and normalized asset)
	UPROPERTY(EditAnywhere, Category = "Grain Table | File Writing", meta = (EditCondition = "bWriteAnalysisInputToFile"))
	FString AnalysisInputFilePath;
#endif

#if WITH_EDITOR

	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	UFUNCTION(Category = "Grain Table", meta = (CallInEditor = "true"))
	void PerformGrainTableAnalysis();

	UFUNCTION(Category = "Grain Table", meta = (CallInEditor = "true"))
	void PlayToneMatch();

	UFUNCTION(Category = "Grain Table", meta = (CallInEditor = "true"))
	void StopToneMatch();

	UFUNCTION(Category = "Engine Preview", meta = (CallInEditor = "true"))
	void StartEnginePreview();

	UFUNCTION(Category = "Engine Preview", meta = (CallInEditor = "true"))
	void StopEnginePreview();

	// Updates the source data from the associated USoundWave
	void UpdateSourceData();

#endif // WITH_EDITOR

	// Retrieves a copy of the non-uobject data used by synth engine
	void GetData(FMotoSynthData& OutData);

protected:

#if WITH_EDITOR
	float GetCurrentRPMForSampleIndex(int32 CurrentSampleIndex);
	void FilterSourceDataForAnalysis();
	void DynamicsProcessForAnalysis();
	void NormalizeForAnalysis();
	void BuildGrainTableByRPMEstimation();
	void BuildGrainTableByFFT();

	void WriteDebugDataToWaveFiles();
	void WriteAnalysisBufferToWaveFile();
	void WriteGrainTableDataToWaveFile();
#endif // WITH_EDITOR

#if WITH_EDITORONLY_DATA
	Audio::AlignedFloatBuffer AnalysisBuffer;
#endif

	// Data containing PCM audio of the imported source asset (filled out by the factory)
	UPROPERTY()
	TArray<float> SourceData;

	// Sample rate of the imported sound wave and the serialized data of the granulator
	UPROPERTY()
	int32 SourceSampleRate = 0;

	// Grain table containing information about how to granulate the source data buffer.
	UPROPERTY()
	TArray<FGrainTableEntry> GrainTable;

#if WITH_EDITORONLY_DATA
	FRPMEstimationSynthTone MotoSynthSineToneTest;
	FMotoSynthEnginePreviewer EnginePreviewer;
#endif

#if WITH_EDITOR
	friend class UMotoSynthSourceFactory; // allow factory to fill the internal buffer
	friend class FMotoSynthSourceConverter; // allow async worker to raise flags upon completion
#endif
};
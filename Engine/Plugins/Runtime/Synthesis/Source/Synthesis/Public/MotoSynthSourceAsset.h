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
#include "MotoSynthSourceAsset.generated.h"

class UMotoSynthSource;

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
	void Init(const float* InAudioBuffer, int32 InStartIndex, int32 InEndIndex, int32 InNumSamplesCrossfade, float InGrainStartRPM, float InGrainEndRPM);

	// Generates a sample from the grain. Returns true if fading out
	float GenerateSample();

	// If we're in the fade-out region
	bool IsFadingOut() const;

	// If the grain is done
	bool IsDone() const;

	// Updates the grain's RPM
	void SetRPM(int32 InRPM);

private:
	const float* AudioBuffer = nullptr;
	float StartSampleIndex = 0.0f;
	float CurrentSampleIndex = 0.0f;
	int32 EndSampleIndex = 0;
	float FadeSamples = 0.0f;
	float FadeInEndIndex = 0.0f;
	float FadeOutStartIndex = 0.0f;
	float GrainPitchScale = 1.0f;
	float CurrentRuntimeRPM = 0.0f;
	float GrainDurationSamples = 0.0f;
	float GrainRPMStart = 0.0f;
	float GrainRPMDelta = 0.0f;
};

// Class for granulating an engine
class FMotoSynthEngine : public ISoundGenerator
{
public:
	FMotoSynthEngine();
	~FMotoSynthEngine();

	// Queries if the engine is enabled at all. Checks a cvar.
	static bool IsMotoSynthEngineEnabled();

	void Init(int32 InSampleRate);
	void Reset();

	// Sets all the source data for the moto synth
	void SetSourceData(FMotoSynthData& InAccelerationData, FMotoSynthData& InDecelerationData);

	// Returns the min and max RPM range, taking into account the acceleration and deceleration data.
	void GetRPMRange(FVector2D& OutRPMRange);

	// Sets the RPM directly. Used if the engine is in ManualRPM mode. Will be ignored if we're in simulation mode.
	void SetRPM(float InRPM, float InTimeSec = 0.0f);

	void SetSynthToneEnabled(bool bInEnabled);
	void SetSynthToneVolume(float InVolume);
	void SetGranularEngineEnabled(bool bInEnabled);
	void SetGranularEngineVolume(float InVolume);
	void SetGranularEngineGrainCrossfade(int32 NumSamples);
	void SetGranularEngineGrainCountToRPMDeltaCurve(const FRichCurve& InDeltaCurve, const FVector2D& InRPMDeltaRange, const FVector2D& InGrainCountRange);

	// Number of grains to oscillate/loop around the RPM point if it doesn't change by a given RPM delta
	// ConstantRPMDelta - the RPM delta within which we will decide we're on a constant RPM
	// NumGrains - the number of grains to loop around while in constant RPM mode
	// NumGrainJirtterDelta - a bit of jitter delta to use to randomize the grain index loop start and end points
	void SetGranularEngineNumLoopGrains(int32 InNumLoopGrains);
	void SetGranularEngineNumLoopJitterGrains(int32 InEnginePreviewNumLoopJitterGrains);

	//~ Begin FSoundGenerator 
	virtual int32 GetDesiredNumSamplesToRenderPerCallback() const { return 256; }
	virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;
	//~ End FSoundGenerator

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
	int32 CurrentLoopingSourceDataIndex = 0;

	// The source data
	FMotoSynthData AccelerationSourceData;
	FMotoSynthData DecelerationSourceData;

	FVector2D RPMRange;
	FVector2D RPMRange_RendererCallback;

	// Number of samples to use to do a grain crossfade. Smooths out discontinuities on grain boundaries.
	int32 GrainCrossfadeSamples = 20;

	// Number of grains to loop if the RPM value would repick the same grain index over and over
	int32 NumGrainsToLoopOnSameRPM = 50;

	int32 NumGrainTableEntriesPerGrain = 3;

	// Num of grain loop jitter delta. Basically when the loop happens, it picks a random grain +/- delta from this value
	int32 NumGrainLoopJitterDelta = 10;

	int32 MaxLoopingGrainIndex = 0;
	int32 MinLoopingGrainIndex = 0;

	// The grain pool for runtime generation of audio
	TArray<FMotoSynthGrainRuntime> GrainPool;

	// The grain state management arrays
	TArray<int32> ActiveGrains; // Grains actively generating and not fading out
	TArray<int32> FadingGrains; // Grains which are fading out, about to be freed
	TArray<int32> FreeGrains; // Grain indicies which are free to be used. max size should be equal to grain pool size.

	TArray<float> SynthBuffer;
	FVector2D SynthFilterFreqRange = { 100.0f, 5000.0f };
	Audio::FLadderFilter SynthFilter;
	Audio::FOsc SynthOsc;
	int32 SynthPitchUpdateSampleIndex = 0;
	int32 SynthPitchUpdateDeltaSamples = 1023;

	float SynthToneVolume = 0.0f;
	float GranularEngineVolume = 1.0f;

	bool bCrossfadeGrains = false;
	bool bWasAccelerating = false;
	bool bSynthToneEnabled = false;
	bool bGranularEngineEnabled = true;
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

	void SetSynthToneEnabled(bool bInEnabled);
	void SetSynthToneVolume(float InVolume);
	void SetGranularEngineEnabled(bool bInEnabled);
	void SetGranularEngineVolume(float InVolume);
	void SetGranularEngineGrainCrossfade(int32 InSample);
	void SetGranularEngineNumLoopGrains(int32 InNumLoopGrains);
	void SetGranularEngineNumLoopJitterGrains(int32 InNumLoopJitterGrains);
	void SetGranularEngineSources(UMotoSynthSource* InAccelSynthSource, UMotoSynthSource* InDecelSynthSource);

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
	float SynthToneVolume = 0.5f;
	float GranularEngineVolume = 1.0f;
	int32 GrainCrossfadeSamples = 10;
	int32 NumGrainEntriesPerGrain = 1;
	int32 EnginePreviewNumLoopGrains = 50;
	int32 EnginePreviewNumLoopJitterGrains = 10;

	bool bSynthToneEnabled = true;
	bool bGrainEngineEnabled = true;
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

	// Pitch-detector editor settings

#if WITH_EDITORONLY_DATA
	// Whether or not to enable a synth tone for preview. Synth matches pitch of RPM.
	UPROPERTY(EditAnywhere, Category = "Engine Preview")
	bool bEnginePreviewSynthToneEnabled = true;

	// Whether or not to enable a synth tone for preview. Synth matches pitch of RPM.
	UPROPERTY(EditAnywhere, Category = "Engine Preview", meta = (ClampMin = "0.0", ClampMax = "2.0", UIMin = "0.0", UIMax = "1.0", EditCondition = "bEnginePreviewSynthToneEnabled"))
	float EnginePreviewSynthToneVolume = 0.5f;

	// Whether or not to enable a the granular engine during preview. Can be useful to listen to just the synth tone during preview.
	UPROPERTY(EditAnywhere, Category = "Engine Preview")
	bool bEnginePreviewGranularEngineEnabled = true;

	// Whether or not to enable a the granular engine during preview. Can be useful to listen to just the synth tone during preview.
	UPROPERTY(EditAnywhere, Category = "Engine Preview", meta = (ClampMin = "0.0", ClampMax = "2.0", UIMin = "0.0", UIMax = "1.0", EditCondition = "bEnginePreviewGranularEngineEnabled"))
	float EnginePreviewGranularEngineVolume = 1.0f;

	// Number of samples to crossfade on grain boundary. Avoids any discontinuities in source file but avoids full grain "enveloping"
	UPROPERTY(EditAnywhere, Category = "Engine Preview", meta = (ClampMin = "0", UIMin = "0"))
	int32 EnginePreviewGrainCrossfade = 10;

	// Number of grains to loop if we're on the same RPM value for consecutive RPM values
	UPROPERTY(EditAnywhere, Category = "Engine Preview", meta = (ClampMin = "0", UIMin = "0"))
	int32 EnginePreviewNumLoopGrains = 50;

	// Number of grains to jitter the looping grain index start and end points. Used to avoid the exact same audio content on the same RPM value.
	UPROPERTY(EditAnywhere, Category = "Engine Preview", meta = (ClampMin = "0", UIMin = "0"))
	int32 EnginePreviewNumLoopJitterGrains = 10;

	// Motosynth source to use for granular engine acceleration
	UPROPERTY(EditAnywhere, Category = "Engine Preview")
	UMotoSynthSource* AccelerationSource;

	// Motosynth source to use for granular engine deceleration
	UPROPERTY(EditAnywhere, Category = "Engine Preview")
	UMotoSynthSource* DecelerationSource;

	// Engine preview RPM curve
	UPROPERTY(EditAnywhere, Category = "Engine Preview")
	FRuntimeFloatCurve EnginePreviewRPMCurve;

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
	int32 SampleShiftOffset = 68;

	UPROPERTY(EditAnywhere, Category = "Grain Table | Normalization")
	bool bEnableNormalizationForAnalysis = true;

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
// Copyright Epic Games, Inc. All Rights Reserved.

#include "MotoSynthSourceAsset.h"
#include "SynthesisModule.h"

#if WITH_EDITOR
#include "DSP/Filter.h"
#include "Sound/SampleBufferIO.h"
#include "SampleBuffer.h"
#include "DSP/DynamicsProcessor.h"
#include "DSP/Dsp.h"
#include "DSP/Granulator.h"
#endif

static int32 MotosynthDisabledCVar = 0;
FAutoConsoleVariableRef CVarDisableMotoSynth(
	TEXT("au.DisableMotoSynth"),
	MotosynthDisabledCVar,
	TEXT("Disables the moto synth.\n")
	TEXT("0: Not Disabled, 1: Disabled"),
	ECVF_Default);


///////////////////////////////////////////////////////////
// RPM estimation implementation
///////////////////////////////////////////////////////////
#if WITH_EDITOR
FRPMEstimationSynthTone::FRPMEstimationSynthTone()
{
}

FRPMEstimationSynthTone::~FRPMEstimationSynthTone()
{
	StopTestTone();
}

void FRPMEstimationSynthTone::StartTestTone(float InVolume)
{
	VolumeScale = InVolume;
	if (!bRegistered)
	{
		if (FAudioDeviceManager* AudioDeviceManager = FAudioDeviceManager::Get())
		{
			if (FAudioDevice* AudioDevice = AudioDeviceManager->GetMainAudioDeviceRaw())
			{
				bRegistered = true;
				AudioDevice->RegisterSubmixBufferListener(this);
			}
		}
	}
}

void FRPMEstimationSynthTone::StopTestTone()
{
	Reset();

	if (bRegistered)
	{
		if (FAudioDeviceManager* AudioDeviceManager = FAudioDeviceManager::Get())
		{
			if (FAudioDevice* AudioDevice = AudioDeviceManager->GetMainAudioDeviceRaw())
			{
				bRegistered = false;
				AudioDevice->UnregisterSubmixBufferListener(this);
			}
		}
	}
}

void FRPMEstimationSynthTone::Reset()
{
	FScopeLock Lock(&CallbackCritSect);
	AudioFileBufferPtr = nullptr;
	NumBufferSamples = 0;
	CurrentFrame = 0;
}

void FRPMEstimationSynthTone::SetAudioFile(float* InBufferPtr, int32 InNumBufferSamples, int32 InSampleRate)
{
	FScopeLock Lock(&CallbackCritSect);
	AudioFileBufferPtr = InBufferPtr;
	NumBufferSamples = InNumBufferSamples;

	SampleRate = InSampleRate;
	Osc.Init((float)InSampleRate);
	Osc.SetType(Audio::EOsc::Saw);
	Osc.SetGain(1.0f);
	Osc.Start();

	Filter.Init((float)InSampleRate, 1);
	Filter.SetFrequency(200.0f);
	Filter.Update();

	CurrentFrame = 0;
}

void FRPMEstimationSynthTone::SetPitchCurve(FRichCurve& InRPMCurve)
{
	FScopeLock Lock(&CallbackCritSect);
	RPMCurve = InRPMCurve;
}

void FRPMEstimationSynthTone::OnNewSubmixBuffer(const USoundSubmix* OwningSubmix, float* AudioData, int32 NumSamples, int32 NumChannels, const int32 InSampleRate, double AudioClock)
{
	FScopeLock Lock(&CallbackCritSect);

	if (CurrentFrame >= NumBufferSamples)
	{
		return;
	}

	int32 NumFrames = NumSamples / NumChannels;

	// Generate the test tone
	ScratchBuffer.Reset();
	ScratchBuffer.AddUninitialized(NumFrames);

	int32 ToneCurrentFrame = CurrentFrame;

	float* ScratchBufferPtr = ScratchBuffer.GetData();

	float LastFrequency = 0.0f;

	for (int32 FrameIndex = 0; FrameIndex < NumFrames; ++FrameIndex)
	{
		float CurrentTime = (float)CurrentFrame / SampleRate;
		LastFrequency = RPMCurve.Eval(CurrentTime) / 60.0f;		

		Osc.SetFrequency(LastFrequency);
		Osc.Update();

		ScratchBufferPtr[FrameIndex] = VolumeScale * Osc.Generate();
	}

	Filter.SetFrequency(LastFrequency + 200.0f);
	Filter.Update();

	// Apply filter to test tone
	Filter.ProcessAudio(ScratchBuffer.GetData(), ScratchBuffer.Num(), ScratchBuffer.GetData());

	int32 SampleIndex = 0;
	for (int32 FrameIndex = 0; FrameIndex < NumFrames; ++FrameIndex)
	{
		if (CurrentFrame >= NumBufferSamples)
		{
			return;
		}

		float SampleOutput = 0.5f * AudioFileBufferPtr[CurrentFrame++] + ScratchBufferPtr[FrameIndex];

		// Left channel
		for (int32 Channel = 0; Channel < 2; ++Channel)
		{
			AudioData[SampleIndex + Channel] += SampleOutput;
		}

		SampleIndex += NumChannels;
	}
}
#endif

UMotoSynthSource::UMotoSynthSource()
{
}

UMotoSynthSource::~UMotoSynthSource()
{
}

void UMotoSynthSource::GetData(FMotoSynthData& OutData)
{
	// Gotta copy the audio data out
	OutData.AudioSource = SourceData;
	OutData.SourceSampleRate = SourceSampleRate;
	OutData.GrainTable = GrainTable;

	// We need a curve data
	FRichCurve* RichRPMCurve = RPMCurve.GetRichCurve();
	if (ensure(RichRPMCurve))
	{
		OutData.RPMCurve = *RichRPMCurve;
	}
}

#if WITH_EDITOR
void UMotoSynthSource::PlayToneMatch()
{
	MotoSynthSineToneTest.Reset();

	FRichCurve* RichRPMCurve = RPMCurve.GetRichCurve();
	if (RichRPMCurve)
	{
		MotoSynthSineToneTest.SetPitchCurve(*RichRPMCurve);
	}

	MotoSynthSineToneTest.SetAudioFile(SourceData.GetData(), SourceData.Num(), SourceSampleRate);

	MotoSynthSineToneTest.StartTestTone(RPMSynthVolume);
}

void UMotoSynthSource::StopToneMatch()
{
	MotoSynthSineToneTest.StopTestTone();
}

void UMotoSynthSource::FilterSourceDataForAnalysis()
{
	// Filter the audio source and write the output to the analysis buffer. Do not modify the source audio data.
	if (bEnableFilteringForAnalysis)
	{
		Audio::AlignedFloatBuffer ScratchBuffer;
		ScratchBuffer.AddUninitialized(SourceData.Num());

		Audio::FBiquadFilter HPF;
		HPF.Init(SourceSampleRate, 1, Audio::EBiquadFilter::Highpass, HighPassFilterFrequency);
		HPF.ProcessAudio(SourceData.GetData(), SourceData.Num(), ScratchBuffer.GetData());

		AnalysisBuffer = ScratchBuffer;

		Audio::FBiquadFilter LPF;
		LPF.Init(SourceSampleRate, 1, Audio::EBiquadFilter::Lowpass, LowPassFilterFrequency);
		LPF.ProcessAudio(AnalysisBuffer.GetData(), AnalysisBuffer.Num(), ScratchBuffer.GetData());

		// Move from the scratch buffer
		AnalysisBuffer = MoveTemp(ScratchBuffer);
	}
	else
	{
		// Do not move from source, we want to copy
		AnalysisBuffer = SourceData;
	}
}

void UMotoSynthSource::DynamicsProcessForAnalysis()
{
	// Feed the filtered audio file through a dynamics processor to get a common audio amplitude profile
	if (bEnableDynamicsProcessorForAnalysis)
	{
		Audio::FDynamicsProcessor DynamicsProcessor;
		DynamicsProcessor.Init(SourceSampleRate, 1);

		DynamicsProcessor.SetLookaheadMsec(DynamicsProcessorLookahead);
		DynamicsProcessor.SetInputGain(DynamicsProcessorInputGainDb);
		DynamicsProcessor.SetRatio(DynamicsProcessorRatio);
		DynamicsProcessor.SetKneeBandwidth(DynamicsKneeBandwidth);
		DynamicsProcessor.SetThreshold(DynamicsProcessorThreshold);
		DynamicsProcessor.SetAttackTime(DynamicsProcessorAttackTimeMsec);
		DynamicsProcessor.SetReleaseTime(DynamicsProcessorReleaseTimeMsec);

		Audio::AlignedFloatBuffer DynamicsProcessorScratchBuffer;
		DynamicsProcessorScratchBuffer.AddUninitialized(AnalysisBuffer.Num());

		DynamicsProcessor.ProcessAudio(AnalysisBuffer.GetData(), AnalysisBuffer.Num(), DynamicsProcessorScratchBuffer.GetData());

		AnalysisBuffer = MoveTemp(DynamicsProcessorScratchBuffer);
	}
}

void UMotoSynthSource::NormalizeForAnalysis()
{
	if (bEnableNormalizationForAnalysis)
	{
		// Find max abs sample
		float MaxSample = 0.0f;
		for (float& Sample : AnalysisBuffer)
		{
			MaxSample = FMath::Max(MaxSample, FMath::Abs(Sample));
		}

		// If we found one, which we should, use to normalize the signal
		if (MaxSample > 0.0f)
		{
			Audio::MultiplyBufferByConstantInPlace(AnalysisBuffer, 1.0f / MaxSample);
		}
	}
}

void UMotoSynthSource::BuildGrainTableByFFT()
{

}

void UMotoSynthSource::BuildGrainTableByRPMEstimation()
{
	// Reset the graintable in case this is a re-generate call
	GrainTable.Reset();

	if (!AnalysisBuffer.Num())
	{
		UE_LOG(LogSynthesis, Error, TEXT("No analysis buffer to build grain table from."));
		return;
	}

	// start counting zero crossings (when the sign flips from negative to positive or positive to negative).
	float* AnalysisBufferPtr = AnalysisBuffer.GetData();

	// Prepare grain table for new entries
	GrainTable.Reset();

	// The following grain-table algorithm tracks zero crossings with positive slope (velocity) and positive peaks.
	// It adds a new entry to the grain table when it finds a positive peak w/ preceding zero crossing that closest matches the expected RPM value for that grain.
	// The expected RPM value is determined from a look up for the RPM curve.

	// Immediately make a grain table entry for the beginning of the asset
	FGrainTableEntry FirstGrain;
	FirstGrain.RPM = GetCurrentRPMForSampleIndex(0);;
	FirstGrain.SampleIndex = 0;
	GrainTable.Add(FirstGrain);

	// Determine the expected grain duration (in samples) given the RPM value of this grain
	int32 ExpectedGrainDuration = (int32)((float)SourceSampleRate / (FirstGrain.RPM / 60.0f));

	// Set the grain duration delta to max integer. 
	int32 MinGrainDurationDelta = INT_MAX;
	int32 BestGrainSampleIndex = INDEX_NONE;
	int32 PreviousGrainStartSampleIndex = 0;
	float PreviousSample = AnalysisBuffer[0];
	int32 PreviousZeroCrossingSampleIndex = 0;


	for (int32 AnalysisSampleIndex = 1; AnalysisSampleIndex < AnalysisBuffer.Num(); ++AnalysisSampleIndex)
	{
		// Check for positive-sloped zero crossings
		float CurrentSample = AnalysisBuffer[AnalysisSampleIndex];
		if (CurrentSample > PreviousSample && PreviousSample < 0.0f && CurrentSample >= 0.0f)
		{
			// Measure the delta from current grain's start
			FGrainTableEntry& CurrentGrain = GrainTable[GrainTable.Num() - 1];

			// The distance between the current sample index and our previous grain's sample index
			int32 SampleDeltaFromPreviousGrain = AnalysisSampleIndex - CurrentGrain.SampleIndex;

			// compute the absolute delta from the expected grain duration from our RPM curve
			int32 DeltaFromExpectedDuration = FMath::Abs(ExpectedGrainDuration - SampleDeltaFromPreviousGrain);

			// If this delta is less than our previous min delta, we store the current index and update our min delta
			if (DeltaFromExpectedDuration < MinGrainDurationDelta)
			{
				MinGrainDurationDelta = DeltaFromExpectedDuration;
				BestGrainSampleIndex = AnalysisSampleIndex;
			}
			// Otherwise we are now finding worse-matches so we've now found a grain we want to add to the table
			else
			{
				check(BestGrainSampleIndex != INDEX_NONE);

				// Now add a new grain starting from where the current grain left off
				FGrainTableEntry NewGrain;

				// The new grain's starting RPM is the ending BPM of the previous grain
				NewGrain.RPM = GetCurrentRPMForSampleIndex(BestGrainSampleIndex);
				NewGrain.AnalysisSampleIndex = BestGrainSampleIndex;
				GrainTable.Add(NewGrain);

				// Update the next expected grain duration based on the current grain's RPM
				ExpectedGrainDuration = (int32)((float)SourceSampleRate / (NewGrain.RPM / 60.0f));

				// Reset the MinGrainDurationDelta since we are now hunting for a new best-match
				MinGrainDurationDelta = INT_MAX;
				BestGrainSampleIndex = INDEX_NONE;

				// Now set the current analysis buffer index to be the new grain sample index
				AnalysisSampleIndex = NewGrain.AnalysisSampleIndex;
			}
		}
		PreviousSample = CurrentSample;
	}

	// To store the sample index using the source file, shift all the samples to compensate for phase shift due to analysis
	// Then do a fix up to find the nearest zero crossing in the source file to minimize clipping distortion or the need to cross-fade on new grains
	for (FGrainTableEntry& Entry : GrainTable)
	{
		int32 DesiredSourceIndex = FMath::Max(Entry.AnalysisSampleIndex - SampleShiftOffset, 0);

		if (DesiredSourceIndex == 0)
		{
			Entry.SampleIndex = DesiredSourceIndex;
		}
		else
		{
			int32 NearestZeroCrossingForward = 0;
			int32 NearestZeroCrossingBackward = 0;

			// move forward in buffer to find nearest zero crossing
			float CurrentValue = 1.0f;
			float PreviousValue = 1.0f;

			int32 CurrentSearchIndex = DesiredSourceIndex;
			while ((CurrentValue > 0.0f && PreviousValue > 0.0f) || (CurrentValue < 0.0f && PreviousValue < 0.0f))
			{
				CurrentValue = SourceData[CurrentSearchIndex];
				PreviousValue = SourceData[CurrentSearchIndex - 1];
				CurrentSearchIndex++;
			}
			NearestZeroCrossingForward = CurrentSearchIndex;

			CurrentValue = 1.0f;
			PreviousValue = 1.0f;
			CurrentSearchIndex = DesiredSourceIndex;
			while ((CurrentValue > 0.0f && PreviousValue > 0.0f) || (CurrentValue < 0.0f && PreviousValue < 0.0f))
			{
				CurrentValue = SourceData[CurrentSearchIndex];
				PreviousValue = SourceData[CurrentSearchIndex - 1];
				CurrentSearchIndex--;
			}
			NearestZeroCrossingBackward = CurrentSearchIndex;

			if (FMath::Abs(DesiredSourceIndex - NearestZeroCrossingForward) < FMath::Abs(DesiredSourceIndex - NearestZeroCrossingBackward))
			{
				DesiredSourceIndex = NearestZeroCrossingForward;
			}
			else
			{
				DesiredSourceIndex = NearestZeroCrossingBackward;
			}
		}
		
		Entry.SampleIndex = DesiredSourceIndex;
	}

	UE_LOG(LogSynthesis, Log, TEXT("Grain Table Built Using RPM Estimation: %d Grains"), GrainTable.Num());
}

void UMotoSynthSource::WriteDebugDataToWaveFiles()
{
	if (bWriteAnalysisInputToFile && !AnalysisInputFilePath.IsEmpty())
	{
		WriteAnalysisBufferToWaveFile();
		WriteGrainTableDataToWaveFile();
	}
}


void UMotoSynthSource::PerformGrainTableAnalysis()
{
	FilterSourceDataForAnalysis();
	DynamicsProcessForAnalysis();
	NormalizeForAnalysis();
	BuildGrainTableByRPMEstimation();
	WriteDebugDataToWaveFiles();

	AnalysisBuffer.Reset();
}

void UMotoSynthSource::WriteAnalysisBufferToWaveFile()
{
	if (AnalysisBuffer.Num() > 0)
	{
		// Write out the analysis buffer
		Audio::FSoundWavePCMWriter Writer;

		TArray<int16> AnalysisBufferInt16;
		AnalysisBufferInt16.AddUninitialized(AnalysisBuffer.Num());

		for (int32 i = 0; i < AnalysisBufferInt16.Num(); ++i)
		{
			AnalysisBufferInt16[i] = AnalysisBuffer[i] * 32767.0f;
		}

		Audio::TSampleBuffer<> BufferToWrite(AnalysisBufferInt16.GetData(), AnalysisBufferInt16.Num(), 1, SourceSampleRate);

		FString FileName = FString::Printf(TEXT("%s_Analysis"), *GetName());
		Writer.BeginWriteToWavFile(BufferToWrite, FileName, AnalysisInputFilePath);
	}
}

void UMotoSynthSource::WriteGrainTableDataToWaveFile()
{
	if (GrainTable.Num() > 0)
	{
		int32 BufferSize = SourceData.Num();
		check(BufferSize == AnalysisBuffer.Num());

		TArray<int16> AnalysisGrainTableBufferInt16;
		AnalysisGrainTableBufferInt16.AddZeroed(BufferSize);

		TArray<int16> SourceGrainTableBufferInt16;
		SourceGrainTableBufferInt16.AddZeroed(BufferSize);

		// Write out the grain table for the analysis file
		for (int32 GrainIndex = 0; GrainIndex < GrainTable.Num(); ++GrainIndex)
		{
			FGrainTableEntry& Entry = GrainTable[GrainIndex];

			int32 AnalysisGrainSampleDuration = 0;
			int32 SoruceGrainSampleDuration = 0;

			// Write out the audio storeed in the grain table
			if (GrainIndex == GrainTable.Num() - 1)
			{
				AnalysisGrainSampleDuration = AnalysisBuffer.Num() - Entry.AnalysisSampleIndex;
				SoruceGrainSampleDuration = SourceData.Num() - Entry.SampleIndex;
			}
			else
			{
				AnalysisGrainSampleDuration = GrainTable[GrainIndex + 1].AnalysisSampleIndex - Entry.AnalysisSampleIndex;
				SoruceGrainSampleDuration = GrainTable[GrainIndex + 1].SampleIndex - Entry.SampleIndex;
			}

			for (int32 AudioDataIndex = 0; AudioDataIndex < AnalysisGrainSampleDuration; ++AudioDataIndex)
			{
				int32 BufferIndex = Entry.AnalysisSampleIndex + AudioDataIndex;
				if (BufferIndex < AnalysisBuffer.Num())
				{
					float SampleDataData = 1.0f;

					if (AudioDataIndex != 0)
					{
						SampleDataData = AnalysisBuffer[BufferIndex];
					}

					AnalysisGrainTableBufferInt16[Entry.AnalysisSampleIndex + AudioDataIndex] = SampleDataData * 32767.0f;
				}
			}

			for (int32 AudioDataIndex = 0; AudioDataIndex < SoruceGrainSampleDuration; ++AudioDataIndex)
			{
				int32 BufferIndex = Entry.SampleIndex + AudioDataIndex;
				if (BufferIndex < SourceData.Num())
				{
					float SampleDataData = 1.0f;

					if (AudioDataIndex != 0)
					{
						SampleDataData = SourceData[BufferIndex];
					}

					SourceGrainTableBufferInt16[Entry.SampleIndex + AudioDataIndex] = SampleDataData * 32767.0f;
				}
			}
		}

		{
			Audio::FSoundWavePCMWriter Writer;
			Audio::TSampleBuffer<> BufferToWrite(AnalysisGrainTableBufferInt16.GetData(), AnalysisGrainTableBufferInt16.Num(), 1, SourceSampleRate);
			FString FileName = FString::Printf(TEXT("%s_AnalysisGrains"), *GetName());
			Writer.BeginWriteToWavFile(BufferToWrite, FileName, AnalysisInputFilePath);
		}

		{
			Audio::FSoundWavePCMWriter Writer;
			Audio::TSampleBuffer<> BufferToWrite(SourceGrainTableBufferInt16.GetData(), SourceGrainTableBufferInt16.Num(), 1, SourceSampleRate);
			FString FileName = FString::Printf(TEXT("%s_SourceGrains"), *GetName());
			Writer.BeginWriteToWavFile(BufferToWrite, FileName, AnalysisInputFilePath);
		}
	}
}

float UMotoSynthSource::GetCurrentRPMForSampleIndex(int32 CurrentSampleIndex)
{
	float CurrentTime = (float)CurrentSampleIndex / (float)SourceSampleRate;
	FRichCurve* RichRPMCurve = RPMCurve.GetRichCurve();
	float CurveValue = RichRPMCurve->Eval(CurrentTime);
	return CurveValue;
}

void UMotoSynthSource::StartEnginePreview()
{
	// Set all the state of the previewer that needs setting
	EnginePreviewer.SetSynthToneEnabled(bEnginePreviewSynthToneEnabled);
	EnginePreviewer.SetSynthToneVolume(EnginePreviewSynthToneVolume);
	EnginePreviewer.SetGranularEngineSources(AccelerationSource, DecelerationSource);
	EnginePreviewer.SetGranularEngineEnabled(bEnginePreviewGranularEngineEnabled);
	EnginePreviewer.SetGranularEngineVolume(EnginePreviewGranularEngineVolume);
	EnginePreviewer.SetGranularEngineGrainCrossfade(EnginePreviewGrainCrossfade);
	EnginePreviewer.SetGranularEngineNumLoopGrains(EnginePreviewNumLoopGrains);
	EnginePreviewer.SetGranularEngineNumLoopJitterGrains(EnginePreviewNumLoopJitterGrains);

	if (FRichCurve* RichRPMCurve = EnginePreviewRPMCurve.GetRichCurve())
	{
		EnginePreviewer.SetPreviewRPMCurve(*RichRPMCurve);
	}

	EnginePreviewer.StartPreviewing();
}

void UMotoSynthSource::StopEnginePreview()
{
	EnginePreviewer.StopPreviewing();
}

void UMotoSynthSource::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	static const FName bEnginePreviewSynthToneEnabledFName = GET_MEMBER_NAME_CHECKED(UMotoSynthSource, bEnginePreviewSynthToneEnabled);
	static const FName EnginePreviewSynthToneVolumeFName = GET_MEMBER_NAME_CHECKED(UMotoSynthSource, EnginePreviewSynthToneVolume);
	static const FName bEnginePreviewGranularEngineEnabledFName = GET_MEMBER_NAME_CHECKED(UMotoSynthSource, bEnginePreviewGranularEngineEnabled);
	static const FName EnginePreviewGranularEngineVolumeFName = GET_MEMBER_NAME_CHECKED(UMotoSynthSource, EnginePreviewGranularEngineVolume);
	static const FName EnginePreviewGrainCrossfadeFName = GET_MEMBER_NAME_CHECKED(UMotoSynthSource, EnginePreviewGrainCrossfade);
	static const FName EnginePreviewNumLoopGrainsFName = GET_MEMBER_NAME_CHECKED(UMotoSynthSource, EnginePreviewNumLoopGrains);
	static const FName EnginePreviewNumLoopJitterGrainsFName = GET_MEMBER_NAME_CHECKED(UMotoSynthSource, EnginePreviewNumLoopJitterGrains);
	static const FName AccelerationSourceFName = GET_MEMBER_NAME_CHECKED(UMotoSynthSource, AccelerationSource);
	static const FName DecelerationSourceFName = GET_MEMBER_NAME_CHECKED(UMotoSynthSource, DecelerationSource);
	static const FName EnginePreviewRPMCurveFName = GET_MEMBER_NAME_CHECKED(UMotoSynthSource, EnginePreviewRPMCurve);

	if (FProperty* PropertyThatChanged = PropertyChangedEvent.Property)
	{
		const FName& Name = PropertyThatChanged->GetFName();
		if (Name == bEnginePreviewSynthToneEnabledFName)
		{
			EnginePreviewer.SetSynthToneEnabled(bEnginePreviewSynthToneEnabled);
		}
		else if (Name == EnginePreviewSynthToneVolumeFName)
		{
			EnginePreviewer.SetSynthToneVolume(EnginePreviewSynthToneVolume);
		}
		else if (Name == bEnginePreviewGranularEngineEnabledFName)
		{
			EnginePreviewer.SetGranularEngineEnabled(bEnginePreviewGranularEngineEnabled);
		}
		else if (Name == EnginePreviewGranularEngineVolumeFName)
		{
			EnginePreviewer.SetGranularEngineVolume(EnginePreviewGranularEngineVolume);
		}
		else if (Name == EnginePreviewGrainCrossfadeFName)
		{
			EnginePreviewer.SetGranularEngineGrainCrossfade(EnginePreviewGrainCrossfade);
		}
		else if (Name == EnginePreviewNumLoopGrainsFName)
		{
			EnginePreviewer.SetGranularEngineNumLoopGrains(EnginePreviewNumLoopGrains);
		}
		else if (Name == EnginePreviewNumLoopJitterGrainsFName)
		{
			EnginePreviewer.SetGranularEngineNumLoopJitterGrains(EnginePreviewNumLoopJitterGrains);
		}
		else if (Name == AccelerationSourceFName || Name == DecelerationSourceFName)
		{
			EnginePreviewer.SetGranularEngineSources(AccelerationSource, DecelerationSource);
		}
		else if (Name == EnginePreviewRPMCurveFName)
		{
			if (FRichCurve* RichRPMCurve = EnginePreviewRPMCurve.GetRichCurve())
			{
				EnginePreviewer.SetPreviewRPMCurve(*RichRPMCurve);
			}
		}
	}
}
#endif // #if WITH_EDITOR


#if WITH_EDITOR
FMotoSynthEnginePreviewer::FMotoSynthEnginePreviewer()
{
}

FMotoSynthEnginePreviewer::~FMotoSynthEnginePreviewer()
{
	StopPreviewing();
}

void FMotoSynthEnginePreviewer::SetSynthToneEnabled(bool bInEnabled)
{
	FScopeLock Lock(&PreviewEngineCritSect);
	bSynthToneEnabled = bInEnabled;
	SynthEngine.SetSynthToneEnabled(bSynthToneEnabled);
}

void FMotoSynthEnginePreviewer::SetSynthToneVolume(float InVolume)
{
	FScopeLock Lock(&PreviewEngineCritSect);
	SynthToneVolume = InVolume;
	SynthEngine.SetSynthToneVolume(SynthToneVolume);
}

void FMotoSynthEnginePreviewer::SetGranularEngineEnabled(bool bInEnabled)
{
	FScopeLock Lock(&PreviewEngineCritSect);
	bGrainEngineEnabled = bInEnabled;
	SynthEngine.SetGranularEngineEnabled(bGrainEngineEnabled);
}

void FMotoSynthEnginePreviewer::SetGranularEngineVolume(float InVolume)
{
	FScopeLock Lock(&PreviewEngineCritSect);
	GranularEngineVolume = InVolume;
	SynthEngine.SetGranularEngineVolume(GranularEngineVolume);
}

void FMotoSynthEnginePreviewer::SetGranularEngineGrainCrossfade(int32 InSamples)
{
	FScopeLock Lock(&PreviewEngineCritSect);
	GrainCrossfadeSamples = InSamples;
	SynthEngine.SetGranularEngineGrainCrossfade(InSamples);
}

void FMotoSynthEnginePreviewer::SetGranularEngineNumLoopGrains(int32 InEnginePreviewNumLoopGrains)
{
	FScopeLock Lock(&PreviewEngineCritSect);
	EnginePreviewNumLoopGrains = InEnginePreviewNumLoopGrains;
	SynthEngine.SetGranularEngineNumLoopGrains(InEnginePreviewNumLoopGrains);
}

void FMotoSynthEnginePreviewer::SetGranularEngineNumLoopJitterGrains(int32 InEnginePreviewNumLoopJitterGrains)
{
	FScopeLock Lock(&PreviewEngineCritSect);
	EnginePreviewNumLoopJitterGrains = InEnginePreviewNumLoopJitterGrains;
	SynthEngine.SetGranularEngineNumLoopJitterGrains(InEnginePreviewNumLoopJitterGrains);
}

void FMotoSynthEnginePreviewer::SetGranularEngineSources(UMotoSynthSource* InAccelSynthSource, UMotoSynthSource* InDecelSynthSource)
{
	if (InAccelSynthSource && InDecelSynthSource)
	{
		FMotoSynthData AccelSynthData;
		InAccelSynthSource->GetData(AccelSynthData);

		FMotoSynthData DecelSynthData;
		InDecelSynthSource->GetData(DecelSynthData);

		SynthEngine.SetSourceData(AccelSynthData, DecelSynthData);

		SynthEngine.GetRPMRange(RPMRange);
	}
}

void FMotoSynthEnginePreviewer::SetPreviewRPMCurve(const FRichCurve& InRPMCurve)
{
	FScopeLock Lock(&PreviewEngineCritSect);
	PreviewRPMCurve = InRPMCurve;
}

void FMotoSynthEnginePreviewer::StartPreviewing()
{
	bPreviewFinished = false;

	Reset();

	if (!bRegistered)
	{
		bEngineInitialized = false;
		if (FAudioDeviceManager* AudioDeviceManager = FAudioDeviceManager::Get())
		{
			if (FAudioDevice* AudioDevice = AudioDeviceManager->GetMainAudioDeviceRaw())
			{
				bRegistered = true;
				AudioDevice->RegisterSubmixBufferListener(this);
			}
		}
	}
}

void FMotoSynthEnginePreviewer::StopPreviewing()
{
	bPreviewFinished = true;

	if (bRegistered)
	{
		if (FAudioDeviceManager* AudioDeviceManager = FAudioDeviceManager::Get())
		{
			if (FAudioDevice* AudioDevice = AudioDeviceManager->GetMainAudioDeviceRaw())
			{
				bRegistered = false;
				bEngineInitialized = false;
				AudioDevice->UnregisterSubmixBufferListener(this);
			}
		}
	}
}

void FMotoSynthEnginePreviewer::OnNewSubmixBuffer(const USoundSubmix* OwningSubmix, float* AudioData, int32 NumSamples, int32 NumChannels, const int32 InSampleRate, double AudioClock)
{
	FScopeLock Lock(&PreviewEngineCritSect);

	if (bPreviewFinished)
	{
		return;
	}

	if (!bEngineInitialized)
	{
		bEngineInitialized = true;
		SynthEngine.Init(InSampleRate);
	}

	// Update the clock of the previewer. Used to look up curves. 
	if (CurrentPreviewCurveStartTime == 0.0f)
	{
		CurrentPreviewCurveStartTime = (float)AudioClock;
		CurrentPreviewCurveTime = 0.0f;
	}
	else
	{
		CurrentPreviewCurveTime = (float)AudioClock - CurrentPreviewCurveStartTime;
	}

	// Update params to the synth engine from the preview controls

	float RPMCurveTime = CurrentPreviewCurveTime;
	float MinTime;
	float MaxTime;
	PreviewRPMCurve.GetTimeRange(MinTime, MaxTime);

	// Wrap to the beginning of the curve time if our current time is less
	if (CurrentPreviewCurveTime < MinTime)
	{
		// Offset the startime
		CurrentPreviewCurveStartTime += MinTime;
		CurrentPreviewCurveTime = MinTime;
	}
	else if (CurrentPreviewCurveTime >= MaxTime)
	{
		CurrentPreviewCurveStartTime += MaxTime;
		CurrentPreviewCurveTime = MinTime;
	}

	// This should be a value between 0.0 and 1.0
	float CurrentRPMCurveValue = PreviewRPMCurve.Eval(CurrentPreviewCurveTime);

	// Normalize the value in the curve's range
	float ValueRangeMin;
	float ValueRangeMax;

	PreviewRPMCurve.GetValueRange(ValueRangeMin, ValueRangeMax);

	float FractionalValue = 0.0f;
	if (!FMath::IsNearlyEqual(ValueRangeMin, ValueRangeMax))
	{
		FractionalValue = (CurrentRPMCurveValue - ValueRangeMin) / (ValueRangeMax - ValueRangeMin);
	}

	float NextRPM = Audio::GetLogFrequencyClamped(FMath::Clamp(FractionalValue, 0.0f, 1.0f), { 0.0f, 1.0f }, RPMRange);
	SynthEngine.SetRPM(NextRPM);

	int32 NumFrames = NumSamples / NumChannels;

	// Generate the engine audio
	OutputBuffer.Reset();
	OutputBuffer.AddZeroed(NumFrames);

	SynthEngine.GetNextBuffer(OutputBuffer.GetData(), NumFrames, true);

	for (int32 FrameIndex = 0, SampleIndex = 0; FrameIndex < NumFrames; ++FrameIndex, SampleIndex += NumChannels)
	{
		float SampleOutput = 0.5f * OutputBuffer[FrameIndex];

		// Left channel
		for (int32 Channel = 0; Channel < 2; ++Channel)
		{
			AudioData[SampleIndex + Channel] += SampleOutput;
		}
	}
}

void FMotoSynthEnginePreviewer::Reset()
{
	CurrentPreviewCurveStartTime = 0.0f;

	SynthEngine.Reset();
}
#endif // WITH_EDITOR


bool FMotoSynthEngine::IsMotoSynthEngineEnabled()
{
	return MotosynthDisabledCVar == 0;
}


FMotoSynthEngine::FMotoSynthEngine()
{
}

FMotoSynthEngine::~FMotoSynthEngine()
{
}

void FMotoSynthEngine::Init(int32 InSampleRate)
{
	if (!IsMotoSynthEngineEnabled())
	{
		return;
	}

	RendererSampleRate = InSampleRate;
	CurrentRPM = 0.0f;

	SynthOsc.Init((float)InSampleRate);
	SynthOsc.SetType(Audio::EOsc::Saw);

	SynthOsc.SetGain(0.5f);
	SynthOsc.SetFrequency(100.0f);
	SynthOsc.Update();

	SynthOsc.Start();

	SynthFilter.Init((float)InSampleRate, 1);
	SynthFilter.SetFrequency(500.0f);
	SynthFilter.Update();

	constexpr int32 GrainPoolSize = 10;
	GrainPool.Init(FMotoSynthGrainRuntime(), GrainPoolSize);
	for (int32 i = 0; i < GrainPoolSize; ++i)
	{
		FreeGrains.Add(i);
	}

	ActiveGrains.Reset();
	FadingGrains.Reset();
}

void FMotoSynthEngine::Reset()
{
	SynthCommand([this]()
	{
		Init(RendererSampleRate);
	});
}

FVector2D FMotoSynthEngine::GetRPMRange(FMotoSynthData& InAccelerationData, FMotoSynthData& InDecelerationData)
{
	FVector2D AccelRPMRange;
	InAccelerationData.RPMCurve.GetValueRange(AccelRPMRange.X, AccelRPMRange.Y);

	FVector2D DecelRPMRange;
	InDecelerationData.RPMCurve.GetValueRange(DecelRPMRange.X, DecelRPMRange.Y);

	return { FMath::Max(AccelRPMRange.X, DecelRPMRange.X), FMath::Min(AccelRPMRange.Y, DecelRPMRange.Y) };
}

void FMotoSynthEngine::SetSourceData(FMotoSynthData& InAccelerationData, FMotoSynthData& InDecelerationData)
{
	FVector2D NewRPMRange = GetRPMRange(InAccelerationData, InDecelerationData);
	RPMRange = NewRPMRange;

	SynthCommand([this, InAccelerationData, InDecelerationData, NewRPMRange]() mutable
	{
		AccelerationSourceData = MoveTemp(InAccelerationData);
		DecelerationSourceData = MoveTemp(InDecelerationData);
		CurrentAccelerationSourceDataIndex = 0;
		CurrentDecelerationSourceDataIndex = 0;
		RPMRange_RendererCallback = NewRPMRange;
	});
}

void FMotoSynthEngine::GetRPMRange(FVector2D& OutRPMRange)
{
	OutRPMRange = RPMRange;
}

void FMotoSynthEngine::SetRPM(float InRPM, float InTimeSec)
{
	SynthCommand([this, InRPM, InTimeSec]()
	{
		TargetRPM = InRPM;
		CurrentRPMTime = 0.0f;
		RPMFadeTime = InTimeSec;
		StartingRPM = CurrentRPM;

		if (CurrentRPM == 0.0f)
		{
			StartingRPM = TargetRPM;
			CurrentRPM = TargetRPM;
			PreviousRPM = TargetRPM - 1;
			CurrentRPMSlope = 0.0f;
			PreviousRPMSlope = 0.0f;
			bWasAccelerating = true;
		}

	});
}

void FMotoSynthEngine::SetSynthToneEnabled(bool bInEnabled)
{
	SynthCommand([this, bInEnabled]()
	{
		bSynthToneEnabled = bInEnabled;
	});
}

void FMotoSynthEngine::SetSynthToneVolume(float InVolume)
{
	SynthCommand([this, InVolume]()
	{
		SynthToneVolume = InVolume;
	});
}

void FMotoSynthEngine::SetGranularEngineEnabled(bool bInEnabled)
{
	SynthCommand([this, bInEnabled]()
	{
		bGranularEngineEnabled = bInEnabled;
	});
}

void FMotoSynthEngine::SetGranularEngineVolume(float InVolume)
{
	SynthCommand([this, InVolume]()
	{
		GranularEngineVolume = InVolume;
	});
}

void FMotoSynthEngine::SetGranularEngineGrainCrossfade(int32 NumSamples)
{
	SynthCommand([this, NumSamples]() mutable
	{
		GrainCrossfadeSamples = NumSamples;
	});
}

void FMotoSynthEngine::SetGranularEngineNumLoopGrains(int32 InNumLoopGrains)
{
	SynthCommand([this, InNumLoopGrains]() mutable
	{
		// store this as half this (we do a spread around the grain index that needs to loop)
		NumGrainsToLoopOnSameRPM = FMath::Max(InNumLoopGrains / 2, 0);
	});
}

void FMotoSynthEngine::SetGranularEngineNumLoopJitterGrains(int32 InEnginePreviewNumLoopJitterGrains)
{
	SynthCommand([this, InEnginePreviewNumLoopJitterGrains]() mutable
	{
		// store this as half this (we do a spread around the grain index that needs to loop)
		NumGrainLoopJitterDelta = FMath::Max(InEnginePreviewNumLoopJitterGrains, 0);
	});
}

bool FMotoSynthEngine::NeedsSpawnGrain()
{
	if (ActiveGrains.Num() > 0)
	{
		bool bGrainStartedFading = false;
		for (int32 i = ActiveGrains.Num() - 1; i >= 0; --i)
		{
			int32 GrainIndex = ActiveGrains[i];
			if (GrainPool[GrainIndex].IsFadingOut())
			{
				ActiveGrains.RemoveAtSwap(i, 1, false);
				FadingGrains.Add(GrainIndex);
				bGrainStartedFading = true;
			}
		}
		// Only spawn one when we start to fade
		return bGrainStartedFading;
	}
	// No active grains, so spawn one
	return true;
}

void FMotoSynthEngine::SpawnGrain(int32& StartingIndex, const FMotoSynthData& SynthData)
{
	if (FreeGrains.Num() && CurrentRPM > 0.0f)
	{
		int32 GrainIndex = FreeGrains.Pop(false);
		ActiveGrains.Push(GrainIndex);

		FMotoSynthGrainRuntime& NewGrain = GrainPool[GrainIndex];
		
		// Start a bit to the left
		int32 GrainTableStart = FMath::Max(StartingIndex - 1, 0);
		for (int32 GrainTableIndex = GrainTableStart; GrainTableIndex < SynthData.GrainTable.Num(); ++GrainTableIndex)
		{
			const FGrainTableEntry* Entry = &SynthData.GrainTable[GrainTableIndex];
			if ((CurrentRPMSlope >= 0.0f && Entry->RPM >= CurrentRPM) || (CurrentRPMSlope < 0.0f && Entry->RPM < CurrentRPM))
			{
				// If the grain we're picking is the exact same one, lets randomly pick a grain around here
				int32 NumGrainEntries = NumGrainTableEntriesPerGrain;
				int32 CrossFadeGrains = GrainCrossfadeSamples;
				if (StartingIndex == GrainTableIndex)
				{
					//NumGrainEntries *= 4;
					CrossFadeGrains *= 2;

					GrainTableIndex += FMath::RandRange(-20, 20);
					GrainTableIndex = FMath::Clamp(GrainTableIndex, 0, SynthData.GrainTable.Num());
				}
				else 
				{
					// Soon as we are not playing the same grain, reset the looping state
					MaxLoopingGrainIndex = 0;
					MinLoopingGrainIndex = 0;
					CurrentLoopingSourceDataIndex = 0;

					// Update the starting index that was passed in to optimize grain-table look up for future spawns
					StartingIndex = GrainTableIndex;
				}

				// compute the grain duration based on the NumGrainEntriesPerGrain
				// we walk the grain table and add grain table durations together to reach a final duration		
				int32 NextGrainTableIndex = FMath::Min(GrainTableIndex + NumGrainEntries + 1, SynthData.GrainTable.Num() - 1);
				int32 GrainDuration = SynthData.GrainTable[NextGrainTableIndex].SampleIndex - SynthData.GrainTable[GrainTableIndex].SampleIndex;

				// Get the RPM value fo the very next grain after this grain duration to be the "ending rpm"
				// This allows us to pitch-scale the grain more closely to the grain's RPM contour through it's lifetime
				int32 EndingRPM = SynthData.GrainTable[NextGrainTableIndex].RPM;

				int32 EndIndex = FMath::Min(Entry->SampleIndex + GrainDuration, SynthData.AudioSource.Num());
				NewGrain.Init(SynthData.AudioSource.GetData(), Entry->SampleIndex, EndIndex, CrossFadeGrains, Entry->RPM, EndingRPM);
				NewGrain.SetRPM(CurrentRPM);

				break;
			}
		}
	}
}

void FMotoSynthEngine::GenerateGranularEngine(float* OutAudio, int32 NumSamples)
{
	// If we're generating a synth tone prepare the scratch buffer
	if (bSynthToneEnabled)
	{
		SynthBuffer.Reset();
		SynthBuffer.AddUninitialized(NumSamples);
	}

	// we lerp through the frame lerp to accurately account for RPM changes and accel or decel
	float RPMDelta = 0.0f;
	if (!FMath::IsNearlyEqual(CurrentRPM, TargetRPM))
	{
		// In this callback, we will lerp to a target RPM. We will always lerp to the target, even if it's in one callback.
		float ThisCallbackTargetRPM = TargetRPM;

		// If we've been given a non-zero fade time, then we will likely lerp through multiple callbacks to get to our target
		// So we need to figure out what percentage of our target RPM value we need to lerp to
		if (RPMFadeTime > 0.0f)
		{
			// Update the RPM time at the callback block rate. Next callback we'll progress further through the lerp.
			CurrentRPMTime += (float)NumSamples / RendererSampleRate;

			// Track how far we've progressed through the fade time
			float FadeFraction = FMath::Clamp(CurrentRPMTime / RPMFadeTime, 0.0f, 1.0f);

			// Compute the fraction of how far we are with the overrall target RPM and which RPM we started at.
			ThisCallbackTargetRPM = StartingRPM + FadeFraction * (TargetRPM - StartingRPM);
		}

		// This is the amount of RPMs to increment per sample to get accurate grain management
		RPMDelta = (ThisCallbackTargetRPM - CurrentRPM) / NumSamples;
	}

	for (int32 SampleIndex = 0; SampleIndex < NumSamples; ++SampleIndex)
	{
		if (bGranularEngineEnabled)
		{
			if (NeedsSpawnGrain())
			{
				CurrentRPMSlope = CurrentRPM - PreviousRPM;
				bool bUnchanged = FMath::IsNearlyZero(CurrentRPMSlope, 0.001f);
				if (bUnchanged)
				{
					// If we haven't changed our RPM much, lets 
					if (bWasAccelerating)
					{
						CurrentDecelerationSourceDataIndex = 0;
						SpawnGrain(CurrentAccelerationSourceDataIndex, AccelerationSourceData);
					}
					else
					{
						CurrentAccelerationSourceDataIndex = 0;
						SpawnGrain(CurrentDecelerationSourceDataIndex, DecelerationSourceData);
					}
				}
				else if (CurrentRPMSlope > 0.0f)
				{
					bWasAccelerating = true;
					CurrentDecelerationSourceDataIndex = 0;
					SpawnGrain(CurrentAccelerationSourceDataIndex, AccelerationSourceData);
				}
				else
				{
					bWasAccelerating = false;
					CurrentAccelerationSourceDataIndex = 0;
					SpawnGrain(CurrentDecelerationSourceDataIndex, DecelerationSourceData);
				}
			}

			PreviousRPMSlope = CurrentRPMSlope;

			// Now render the grain sample data
			for (int32 GrainIndex : ActiveGrains)
			{
				FMotoSynthGrainRuntime& Grain = GrainPool[GrainIndex];
				Grain.SetRPM(CurrentRPM);

				OutAudio[SampleIndex] += Grain.GenerateSample();
			}

			for (int32 FadingGrainIndex = FadingGrains.Num() - 1; FadingGrainIndex >= 0; --FadingGrainIndex)
			{
				int32 GrainIndex = FadingGrains[FadingGrainIndex];

				FMotoSynthGrainRuntime& Grain = GrainPool[GrainIndex];
				Grain.SetRPM(CurrentRPM);

				OutAudio[SampleIndex] += Grain.GenerateSample();

				if (Grain.IsDone())
				{
					FadingGrains.RemoveAtSwap(FadingGrainIndex, 1, false);
					FreeGrains.Push(GrainIndex);
				}
			}
		}

		// We need to generate the synth tone with the exact sample RPM frequencies that the grains are
		if (bSynthToneEnabled)
		{
			if ((SynthPitchUpdateSampleIndex & SynthPitchUpdateDeltaSamples) == 0)
			{
				float CurrentFrequency = CurrentRPM / 60.0f;
				SynthOsc.SetFrequency(CurrentFrequency);
				SynthOsc.Update();
			}

			SynthBuffer[SampleIndex] = SynthOsc.Generate();
		}

		PreviousRPM = CurrentRPM;
		CurrentRPM += RPMDelta;
	}

	// Apply the filter andmix the audio intothe output buffer
	if (bSynthToneEnabled)
	{
		float* SynthBufferPtr = SynthBuffer.GetData();
		SynthFilter.ProcessAudio(SynthBufferPtr, NumSamples, SynthBufferPtr);

		for (int32 FrameIndex = 0; FrameIndex < NumSamples; ++FrameIndex)
		{
			OutAudio[FrameIndex] += SynthToneVolume * SynthBufferPtr[FrameIndex];
		}
	}

	// Make sure we reach our target RPM exactly if we have no fade time
	if (CurrentRPMTime >= RPMFadeTime)
	{
		CurrentRPM = TargetRPM;
	}
}

int32 FMotoSynthEngine::OnGenerateAudio(float* OutAudio, int32 NumSamples)
{
	// Don't do anything if we're not enabled
	if (!IsMotoSynthEngineEnabled())
	{
		return NumSamples;
	}

	GenerateGranularEngine(OutAudio, NumSamples);

	return NumSamples;
}

void FMotoSynthGrainRuntime::Init(const float* InAudioBuffer, int32 InStartIndex, int32 InEndIndex, int32 InNumSamplesCrossfade, float InGrainStartRPM, float InGrainEndRPM)
{
	AudioBuffer = InAudioBuffer;
	StartSampleIndex = (float)InStartIndex;
	FadeInEndIndex = StartSampleIndex + (float)InNumSamplesCrossfade;
	CurrentSampleIndex = StartSampleIndex;
	EndSampleIndex = InEndIndex;

	GrainDurationSamples = FMath::Max((float)(InEndIndex - InStartIndex), 1.0f);
	FadeSamples = FMath::Clamp((float)InNumSamplesCrossfade, 0.0f, 0.5f * ((float)EndSampleIndex - CurrentSampleIndex));
	FadeOutStartIndex = FMath::Max(EndSampleIndex - FadeSamples, 0.0f);
	GrainPitchScale = 1.0f;
	GrainRPMStart = InGrainStartRPM;
	GrainRPMDelta = InGrainEndRPM - InGrainStartRPM;
}

float FMotoSynthGrainRuntime::GenerateSample()
{
	if (!AudioBuffer)
	{
		return 0.0f;
	}

	// compute the location of the grain playback in float-sample indices
	int32 PreviousIndex = (int32)CurrentSampleIndex;
	int32 NextIndex = PreviousIndex + 1;

	if (NextIndex < EndSampleIndex)
	{
		float PreviousSampleValue = AudioBuffer[PreviousIndex];
		float NextSampleValue = AudioBuffer[NextIndex];
		float SampleAlpha = CurrentSampleIndex - (float)PreviousIndex;
		float SampleValueInterpolated = FMath::Lerp(PreviousSampleValue, NextSampleValue, SampleAlpha);

		// apply fade in or fade outs
		if (CurrentSampleIndex < FadeInEndIndex && FadeSamples > 0.0f)
		{
			SampleValueInterpolated *= ((CurrentSampleIndex - StartSampleIndex)/ FadeSamples);
		}
		else if (FadeSamples >= 0.0f && CurrentSampleIndex >= FadeOutStartIndex)
		{
			float FadeOutScale = FMath::Clamp(1.0f - ((CurrentSampleIndex - FadeOutStartIndex) / FadeSamples), 0.0f, 1.0f);
			SampleValueInterpolated *= FadeOutScale;
		}

		// Update the pitch scale based on the progress through the grain and the starting and ending grain RPMs and the current runtime RPM
		float GrainFraction = (CurrentSampleIndex - StartSampleIndex) / GrainDurationSamples;
		// Expected RPM given our playback progress, linearly interpolating the start and end RPMs
		float ExpectedRPM = GrainRPMStart + GrainFraction * GrainRPMDelta;
		GrainPitchScale = CurrentRuntimeRPM / ExpectedRPM;

		CurrentSampleIndex += GrainPitchScale;
		return SampleValueInterpolated;
	}
	else
	{
		CurrentSampleIndex = (float)EndSampleIndex + 1.0f;
	}

	return 0.0f;
}

bool FMotoSynthGrainRuntime::IsFadingOut() const
{
	return CurrentSampleIndex >= FadeOutStartIndex;
}

bool FMotoSynthGrainRuntime::IsDone() const
{
	return CurrentSampleIndex >= (float)EndSampleIndex;
}

void FMotoSynthGrainRuntime::SetRPM(int32 InRPM)
{
	CurrentRuntimeRPM = InRPM;
}
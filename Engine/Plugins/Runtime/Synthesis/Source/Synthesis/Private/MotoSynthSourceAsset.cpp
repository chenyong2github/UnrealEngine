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

#if WITH_EDITOR
void UMotoSynthPreset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (EnginePreviewer)
	{
		EnginePreviewer->SetSettings(Settings);
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

void UMotoSynthSource::UpdateSourceData()
{
	if (!SoundWaveSource)
	{
		return;
	}

	TArray<uint8> ImportedSoundWaveData;
	uint32 ImportedSampleRate;
	uint16 ImportedChannelCount;
	SoundWaveSource->GetImportedSoundWaveData(ImportedSoundWaveData, ImportedSampleRate, ImportedChannelCount);

	SourceSampleRate = ImportedSampleRate;

	const int32 NumFrames = (ImportedSoundWaveData.Num() / sizeof(int16)) / ImportedChannelCount;

	SourceData.Reset();
	SourceData.AddUninitialized(NumFrames);

	int16* ImportedDataPtr = (int16*)ImportedSoundWaveData.GetData();
	float* RawSourceDataPtr = SourceData.GetData();

	// Convert to float and only use the left-channel
	for (int32 FrameIndex = 0; FrameIndex < NumFrames; ++FrameIndex)
	{
		const int32 SampleIndex = FrameIndex * ImportedChannelCount;
		float CurrSample = static_cast<float>(ImportedDataPtr[SampleIndex]) / 32768.0f;

		RawSourceDataPtr[FrameIndex] = CurrSample;
	}
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

static void GetBufferViewFromAnalysisBuffer(const Audio::AlignedFloatBuffer& InAnalysisBuffer, int32 StartingBufferIndex, int32 BufferSize, TArrayView<const float>& OutBufferView)
{
	if (BufferSize > 0)
	{
		BufferSize = FMath::Min(BufferSize, InAnalysisBuffer.Num() - StartingBufferIndex);
		OutBufferView = MakeArrayView(&InAnalysisBuffer[StartingBufferIndex], BufferSize);
	}
}

static float ComputeCrossCorrelation(const TArrayView<const float>& InBufferA, const TArrayView<const float>& InBufferB)
{
	float SumA = 0.0f;
	float SumB = 0.0f;
	float SumAB = 0.0f;
	float SquareSumA = 0.0f;
	float SquareSumB = 0.0f;
	int32 Num = InBufferA.Num();

	for (int32 Index = 0; Index < Num; ++Index)
	{
		// scale the InBufferB to match InBufferA via linear sample rate conversion
		float FractionalIndex = ((float)Index / Num) * InBufferB.Num();
		int32 BIndexPrev = (int32)FractionalIndex;
		int32 BIndexNext = FMath::Min(BIndexPrev + 1, InBufferB.Num() - 1);

		float BufferBSample = FMath::Lerp(InBufferB[BIndexPrev], InBufferB[BIndexNext], FractionalIndex - (float)BIndexPrev);

		SumA += InBufferA[Index];
		SumB += BufferBSample;
		SumAB += InBufferA[Index] * BufferBSample;

		SquareSumA += (InBufferA[Index] * InBufferA[Index]);
		SquareSumB += (BufferBSample * BufferBSample);
	}

	float CorrelationNumerator = (Num * SumAB - SumA * SumB);
	float CorrelationDenomenator = FMath::Sqrt((Num * SquareSumA - SumA * SumA) * (Num * SquareSumB - SumB * SumB));
	check(CorrelationDenomenator > 0.0f);
	float Correlation = CorrelationNumerator / CorrelationDenomenator;
	return Correlation;
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

	// Prepare grain table for new entries
	GrainTable.Reset();

	if (SourceSampleRate <= 0)
	{
		UE_LOG(LogSynthesis, Error, TEXT("Unable to build grain table for moto synth soruce, source sample rate is invalid (%d)"), SourceSampleRate);
		return;
	}

	float DeltaTime = 1.0f / SourceSampleRate;
	float CurrentPhase = 1.0f;
	int32 CurrentSampleIndex = RPMCycleCalibrationSample;
		
	// Cycle back to the beginning of the file with this sample as the calibration sample
	while (CurrentSampleIndex >= 1)
	{
		CurrentPhase = PI;
		while (CurrentPhase >= 0.0f && CurrentSampleIndex >= 0)
		{
			float w0 = GetCurrentRPMForSampleIndex(CurrentSampleIndex) / 60.0f;
			float w1 = GetCurrentRPMForSampleIndex(CurrentSampleIndex--) / 60.0f;
			float Alpha = (w1 - w0) / DeltaTime;
			float DeltaPhase = w0 * DeltaTime + 0.5f * Alpha * DeltaTime * DeltaTime;
			CurrentPhase -= DeltaPhase;
		}
	}

	// Now, where the 'current phase' is, we can read forward from current sample index.
	float* AnalysisBufferPtr = AnalysisBuffer.GetData();
	while (CurrentSampleIndex < AnalysisBuffer.Num() - 1)
	{
		// We read through samples accumulating phase until the phase is greater than 1.0
		// That will indicate the need make a grain entry
		while (CurrentPhase < PI && CurrentSampleIndex < AnalysisBuffer.Num() - 1)
		{
			float w0 = GetCurrentRPMForSampleIndex(CurrentSampleIndex) / 60.0f;
			float w1 = GetCurrentRPMForSampleIndex(CurrentSampleIndex++) / 60.0f;
			float Alpha = (w1 - w0) / DeltaTime;
			float DeltaPhase = w0 * DeltaTime + 0.5f * Alpha * DeltaTime * DeltaTime;
			CurrentPhase += DeltaPhase;
		}

		CurrentPhase = 0.0f;

		// Immediately make a grain table entry for the beginning of the asset
		FGrainTableEntry NewGrain;
		NewGrain.RPM = GetCurrentRPMForSampleIndex(CurrentSampleIndex);;
		NewGrain.AnalysisSampleIndex = CurrentSampleIndex;
		NewGrain.SampleIndex = FMath::Max(NewGrain.AnalysisSampleIndex - SampleShiftOffset, 0);
		GrainTable.Add(NewGrain);
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
	UpdateSourceData();
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
	if (MotoSynthPreset)
	{
		FMotoSynthRuntimeSettings& Settings = MotoSynthPreset->Settings;

		// Set all the state of the previewer that needs setting
		EnginePreviewer.SetSettings(Settings);

		if (FRichCurve* RichRPMCurve = EnginePreviewRPMCurve.GetRichCurve())
		{
			EnginePreviewer.SetPreviewRPMCurve(*RichRPMCurve);
		}

		MotoSynthPreset->EnginePreviewer = &EnginePreviewer;
		EnginePreviewer.StartPreviewing();
	}
}

void UMotoSynthSource::StopEnginePreview()
{
	if (MotoSynthPreset)
	{
		MotoSynthPreset->EnginePreviewer = nullptr;
	}
	EnginePreviewer.StopPreviewing();
}

void UMotoSynthSource::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

 	static const FName EnginePreviewRPMCurveFName = GET_MEMBER_NAME_CHECKED(UMotoSynthSource, EnginePreviewRPMCurve);
 
 	if (FProperty* PropertyThatChanged = PropertyChangedEvent.Property)
 	{
 		const FName& Name = PropertyThatChanged->GetFName();
 		if (Name == EnginePreviewRPMCurveFName)
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

void FMotoSynthEnginePreviewer::SetSettings(const FMotoSynthRuntimeSettings& InSettings)
{
	FScopeLock Lock(&PreviewEngineCritSect);

	// Set the accel and decel data separately
	if (Settings.AccelerationSource != InSettings.AccelerationSource || Settings.DecelerationSource != InSettings.DecelerationSource)
	{
		FMotoSynthData AccelSynthData;
		InSettings.AccelerationSource->GetData(AccelSynthData);

		FMotoSynthData DecelSynthData;
		InSettings.DecelerationSource->GetData(DecelSynthData);
		SynthEngine.SetSourceData(AccelSynthData, DecelSynthData);

		SynthEngine.GetRPMRange(RPMRange);
	}

	Settings = InSettings;
	SynthEngine.SetSettings(InSettings);
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

	float RPMCurveTime = CurrentPreviewCurveTime;
	float MinTime = 0.0f;
	float MaxTime = 0.0f;
	PreviewRPMCurve.GetTimeRange(MinTime, MaxTime);

	// No way to preview RPMs if curve does not have a time range
	if (FMath::IsNearlyEqual(MinTime, MaxTime))
	{
		return;
	}

	// Update the clock of the previewer. Used to look up curves. 
	if (CurrentPreviewCurveStartTime == 0.0f || CurrentPreviewCurveTime >= MaxTime)
	{
		CurrentPreviewCurveStartTime = (float)AudioClock + MinTime;
		CurrentPreviewCurveTime = MinTime;
	}
	else
	{
		CurrentPreviewCurveTime = (float)AudioClock - CurrentPreviewCurveStartTime;
	}

	//UE_LOG(LogTemp, Log, TEXT("RPM CURVE TIME: %.2f"), CurrentPreviewCurveTime);

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
	OutputBuffer.AddZeroed(NumFrames * 2);

	SynthEngine.GetNextBuffer(OutputBuffer.GetData(), NumFrames * 2, true);

	for (int32 FrameIndex = 0, SampleIndex = 0; FrameIndex < NumFrames; ++FrameIndex, SampleIndex += NumChannels)
	{
		for (int32 Channel = 0; Channel < 2; ++Channel)
		{
			AudioData[SampleIndex + Channel] += OutputBuffer[2 * FrameIndex + Channel];
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

	GrainCrossfadeSamples = 10;
	NumGrainTableEntriesPerGrain = 3;
	GrainTableRandomOffsetForConstantRPMs = 20;
	GrainCrossfadeSamplesForConstantRPMs = 20;

	SynthOctaveShift = 0;
	SynthToneVolume = 1.0f;
	SynthFilterFrequency = 500.0f;
	SynthFilter.Init((float)InSampleRate, 1);
	SynthFilter.SetFrequency(SynthFilterFrequency);
	SynthFilter.Update();

	DelayStereo.Init((float)InSampleRate, 2);
	DelayStereo.SetDelayTimeMsec(25);
	DelayStereo.SetFeedback(0.37f);
	DelayStereo.SetWetLevel(0.68f);
	DelayStereo.SetDryLevel(0.8f);
	DelayStereo.SetDelayRatio(0.43f);
	DelayStereo.SetMode(Audio::EStereoDelayMode::PingPong);
	DelayStereo.SetFilterEnabled(true);
	DelayStereo.SetFilterSettings(Audio::EBiquadFilter::Lowpass, 4000.0f, 0.5f);

	constexpr int32 GrainPoolSize = 10;
	GrainPool.Init(FMotoSynthGrainRuntime(), GrainPoolSize);
	for (int32 i = 0; i < GrainPoolSize; ++i)
	{
		FreeGrains.Add(i);
	}

	ActiveGrains.Reset();
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

void FMotoSynthEngine::SetSettings(const FMotoSynthRuntimeSettings& InSettings)
{
	SynthCommand([this, InSettings]()
	{
		bSynthToneEnabled = InSettings.bSynthToneEnabled;
		SynthToneVolume = InSettings.SynthToneVolume;
		SynthOctaveShift = InSettings.SynthOctaveShift;
		SynthFilterFrequency = InSettings.SynthToneFilterFrequency;
		bGranularEngineEnabled = InSettings.bGranularEngineEnabled;
		TargetGranularEngineVolume = InSettings.GranularEngineVolume;
		GrainCrossfadeSamples = InSettings.NumSamplesToCrossfadeBetweenGrains;
		NumGrainTableEntriesPerGrain = InSettings.NumGrainTableEntriesPerGrain;
		GrainTableRandomOffsetForConstantRPMs = InSettings.GrainTableRandomOffsetForConstantRPMs;
		GrainCrossfadeSamplesForConstantRPMs = InSettings.GrainCrossfadeSamplesForConstantRPMs;
		bStereoWidenerEnabled = InSettings.bStereoWidenerEnabled;

		SynthFilter.SetFrequency(SynthFilterFrequency);
		SynthFilter.Update();

		DelayStereo.SetDelayTimeMsec(InSettings.StereoDelayMsec);
		DelayStereo.SetFeedback(InSettings.StereoFeedback);
		DelayStereo.SetWetLevel(InSettings.StereoWidenerWetlevel);
		DelayStereo.SetDryLevel(InSettings.StereoWidenerDryLevel);
		DelayStereo.SetDelayRatio(InSettings.StereoWidenerDelayRatio);
		DelayStereo.SetFilterEnabled(InSettings.bStereoWidenerFilterEnabled);
		DelayStereo.SetFilterSettings(Audio::EBiquadFilter::Lowpass, InSettings.StereoWidenerFilterFrequency, InSettings.StereoWidenerFilterQ);
	});
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

bool FMotoSynthEngine::NeedsSpawnGrain()
{
	if (ActiveGrains.Num() > 0)
	{
		if (ActiveGrains.Num() == 1)
		{
			int32 GrainIndex = ActiveGrains[0];
			if (GrainPool[GrainIndex].IsNearingEnd())
			{
				return true;
			}
		}
		// No grains needed spawning
		return false;
	}
	// No active grains so return true
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
				int32 NewGrainCrossfadeSamples = GrainCrossfadeSamples;
				if (StartingIndex == GrainTableIndex)
				{
					NewGrainCrossfadeSamples = GrainCrossfadeSamplesForConstantRPMs;

					GrainTableIndex += FMath::RandRange(-GrainTableRandomOffsetForConstantRPMs, GrainTableRandomOffsetForConstantRPMs);
					GrainTableIndex = FMath::Clamp(GrainTableIndex, 0, SynthData.GrainTable.Num());
				}
				else 
				{
					// Update the starting index that was passed in to optimize grain-table look up for future spawns
					StartingIndex = GrainTableIndex;
				}

				// compute the grain duration based on the NumGrainEntriesPerGrain
				// we walk the grain table and add grain table durations together to reach a final duration		
				int32 NextGrainTableIndex = GrainTableIndex + NumGrainEntries + 1;
				int32 NextGrainTableIndexClamped = FMath::Clamp(NextGrainTableIndex, 0, SynthData.GrainTable.Num() - 1);
				int32 GrainTableIndexClamped = FMath::Clamp(GrainTableIndex, 0, SynthData.GrainTable.Num() - 1);
				int32 GrainDuration = SynthData.GrainTable[NextGrainTableIndexClamped].SampleIndex - SynthData.GrainTable[GrainTableIndexClamped].SampleIndex;

				// Get the RPM value fo the very next grain after this grain duration to be the "ending rpm"
				// This allows us to pitch-scale the grain more closely to the grain's RPM contour through it's lifetime
				int32 EndingRPM = SynthData.GrainTable[NextGrainTableIndexClamped].RPM;

				int32 StartIndex = FMath::Max(0, Entry->SampleIndex - NewGrainCrossfadeSamples);
				int32 EndIndex = FMath::Min(Entry->SampleIndex + GrainDuration + NewGrainCrossfadeSamples, SynthData.AudioSource.Num());

				TArrayView<const float> GrainArrayView = MakeArrayView(&SynthData.AudioSource[StartIndex], EndIndex - StartIndex);
				NewGrain.Init(GrainArrayView, NewGrainCrossfadeSamples, Entry->RPM, EndingRPM, CurrentRPM);
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
			for (int32 ActiveGrainIndex = ActiveGrains.Num() - 1; ActiveGrainIndex >= 0; --ActiveGrainIndex)
			{
				int32 GrainIndex = ActiveGrains[ActiveGrainIndex];
				FMotoSynthGrainRuntime& Grain = GrainPool[GrainIndex];
				Grain.SetRPM(CurrentRPM);

				OutAudio[SampleIndex] += Grain.GenerateSample();

				if (Grain.IsDone())
				{
					ActiveGrains.RemoveAtSwap(ActiveGrainIndex, 1, false);
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
				CurrentFrequency *= Audio::GetFrequencyMultiplier(12.0f * SynthOctaveShift);
				SynthOsc.SetFrequency(CurrentFrequency);
				SynthOsc.Update();
			}

			SynthBuffer[SampleIndex] = SynthOsc.Generate();
		}

		PreviousRPM = CurrentRPM;
		CurrentRPM += RPMDelta;
	}

	if (!FMath::IsNearlyEqual(TargetGranularEngineVolume, GranularEngineVolume))
	{
		Audio::FadeBufferFast(OutAudio, NumSamples, GranularEngineVolume, TargetGranularEngineVolume);
		GranularEngineVolume = TargetGranularEngineVolume;
	}
	else if (!FMath::IsNearlyEqual(GranularEngineVolume, 1.0f))
	{
		Audio::MultiplyBufferByConstantInPlace(OutAudio, NumSamples, GranularEngineVolume);
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

	const int32 NumFrames = NumSamples / 2;

	// Generate granular audio w/ our mono buffer
	GrainEngineBuffer.Reset();
	GrainEngineBuffer.AddZeroed(NumFrames);

	GenerateGranularEngine(GrainEngineBuffer.GetData(), NumFrames);

	float* GrainEngineBufferPtr = GrainEngineBuffer.GetData();

	// Up-mix to dual-mono stereo
	for (int32 Frame = 0; Frame < NumFrames; ++Frame)
	{
		float GrainEngineSample = GrainEngineBufferPtr[Frame];
		for (int32 Channel = 0; Channel < 2; ++Channel)
		{
			OutAudio[Frame * 2 + Channel] = GrainEngineSample;
		}
	}

	if (bStereoWidenerEnabled)
	{
		// Feed through the stereo delay as "stereo widener"
		DelayStereo.ProcessAudio(OutAudio, NumSamples, OutAudio);
	}

	return NumSamples;
}

void FMotoSynthGrainRuntime::Init(const TArrayView<const float>& InGrainArrayView, int32 InNumSamplesCrossfade, float InGrainStartRPM, float InGrainEndRPM, float InStartingRPM)
{
	GrainArrayView = InGrainArrayView;

	CurrentSampleIndex = 0.0f;
	FadeSamples = (float)InNumSamplesCrossfade;
	FadeOutStartIndex = (float)InGrainArrayView.Num() - (float)FadeSamples;
	GrainPitchScale = 1.0f;
	GrainRPMStart = InGrainStartRPM;
	GrainRPMDelta = InGrainEndRPM - InGrainStartRPM;
	StartingRPM = InStartingRPM;
}

float FMotoSynthGrainRuntime::GenerateSample()
{
	if (CurrentSampleIndex >= (float)GrainArrayView.Num())
	{
		return 0.0f;
	}

	// compute the location of the grain playback in float-sample indices
	int32 PreviousIndex = (int32)CurrentSampleIndex;
	int32 NextIndex = PreviousIndex + 1;

	if (NextIndex < GrainArrayView.Num())
	{
		float PreviousSampleValue = GrainArrayView[PreviousIndex];
		float NextSampleValue = GrainArrayView[NextIndex];
		float SampleAlpha = CurrentSampleIndex - (float)PreviousIndex;
		float SampleValueInterpolated = FMath::Lerp(PreviousSampleValue, NextSampleValue, SampleAlpha);

		// apply fade in or fade outs
		if (FadeSamples > 0)
		{
			if (CurrentSampleIndex < FadeSamples)
			{
				SampleValueInterpolated *= (CurrentSampleIndex / FadeSamples);
			}
			else if (CurrentSampleIndex >= FadeOutStartIndex)
			{
				float FadeOutScale = FMath::Clamp(1.0f - ((CurrentSampleIndex - FadeOutStartIndex) / FadeSamples), 0.0f, 1.0f);
				SampleValueInterpolated *= FadeOutScale;
			}
		}


		// Update the pitch scale based on the progress through the grain and the starting and ending grain RPMs and the current runtime RPM
		float GrainFraction = CurrentSampleIndex / GrainArrayView.Num();
		// Expected RPM given our playback progress, linearly interpolating the start and end RPMs
		float ExpectedRPM = GrainRPMStart + GrainFraction * GrainRPMDelta;
		GrainPitchScale = CurrentRuntimeRPM / ExpectedRPM;

		CurrentSampleIndex += GrainPitchScale;
		return SampleValueInterpolated;
	}
	else
	{
		CurrentSampleIndex = (float)GrainArrayView.Num() + 1.0f;
	}

	return 0.0f;
}
bool FMotoSynthGrainRuntime::IsNearingEnd() const
{
	return (int32)CurrentSampleIndex >= (GrainArrayView.Num() - FadeSamples);
}

bool FMotoSynthGrainRuntime::IsDone() const
{
	return (int32)CurrentSampleIndex >= GrainArrayView.Num();
}

void FMotoSynthGrainRuntime::SetRPM(int32 InRPM)
{
	CurrentRuntimeRPM = InRPM;
}
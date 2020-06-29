// Copyright Epic Games, Inc. All Rights Reserved.

#include "MotoSynthEngine.h"
#include "SynthesisModule.h"
#include "MotoSynthDataManager.h"

static int32 MotosynthDisabledCVar = 0;
FAutoConsoleVariableRef CVarDisableMotoSynth(
	TEXT("au.DisableMotoSynth"),
	MotosynthDisabledCVar,
	TEXT("Disables the moto synth.\n")
	TEXT("0: Not Disabled, 1: Disabled"),
	ECVF_Default);

// Retrieves the moto synth asset manager
FMotoSynthAssetManager& FMotoSynthAssetManager::Get()
{
	static TUniquePtr<FMotoSynthAssetManager> MotoSynthAssetManager;

	if (!MotoSynthAssetManager.IsValid())
	{
		MotoSynthAssetManager = TUniquePtr<FMotoSynthAssetManager>(new FMotoSynthAssetManager);
	}
	return *MotoSynthAssetManager.Get();
}

FMotoSynthAssetManager::FMotoSynthAssetManager()
{
}

FMotoSynthAssetManager::~FMotoSynthAssetManager()
{
}

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

void FMotoSynthEngine::SetSourceData(uint32 InAccelerationSourceID, uint32 InDecelerationSourceID)
{
	FMotoSynthSourceDataManager& DataManager = FMotoSynthSourceDataManager::Get();
	MotoSynthDataPtr InAccelerationSourceData = DataManager.GetMotoSynthData(InAccelerationSourceID);
	MotoSynthDataPtr InDecelerationSourceData = DataManager.GetMotoSynthData(InDecelerationSourceID);

	if (InAccelerationSourceData.IsValid() && InDecelerationSourceData.IsValid())
	{
		FVector2D AccelRPMRange;
		InAccelerationSourceData->RPMCurve.GetValueRange(AccelRPMRange.X, AccelRPMRange.Y);

		FVector2D DecelRPMRange;
		InDecelerationSourceData->RPMCurve.GetValueRange(DecelRPMRange.X, DecelRPMRange.Y);

		FVector2D NewRPMRange = { FMath::Max(AccelRPMRange.X, DecelRPMRange.X), FMath::Min(AccelRPMRange.Y, DecelRPMRange.Y) };
		RPMRange = NewRPMRange;

		SynthCommand([this, InAccelerationSourceData, InDecelerationSourceData, NewRPMRange]() mutable
		{
			CurrentAccelerationSourceDataIndex = 0;
			CurrentDecelerationSourceDataIndex = 0;
			AccelerationSourceData = InAccelerationSourceData;
			DecelerationSourceData = InDecelerationSourceData;
			RPMRange_RendererCallback = NewRPMRange;
		});
	}
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

void FMotoSynthEngine::SpawnGrain(int32& StartingIndex, const MotoSynthDataPtr& SynthData)
{
	if (FreeGrains.Num() && CurrentRPM > 0.0f)
	{
		int32 GrainIndex = FreeGrains.Pop(false);
		ActiveGrains.Push(GrainIndex);

		FMotoSynthGrainRuntime& NewGrain = GrainPool[GrainIndex];
		
		// Start a bit to the left
		int32 GrainTableStart = FMath::Max(StartingIndex - 1, 0);
		for (int32 GrainTableIndex = GrainTableStart; GrainTableIndex < SynthData->GrainTable.Num(); ++GrainTableIndex)
		{
			const FGrainTableEntry* Entry = &SynthData->GrainTable[GrainTableIndex];
			if ((CurrentRPMSlope >= 0.0f && Entry->RPM >= CurrentRPM) || (CurrentRPMSlope < 0.0f && Entry->RPM < CurrentRPM))
			{
				// If the grain we're picking is the exact same one, lets randomly pick a grain around here
				int32 NumGrainEntries = NumGrainTableEntriesPerGrain;
				int32 NewGrainCrossfadeSamples = GrainCrossfadeSamples;
				if (StartingIndex == GrainTableIndex)
				{
					NewGrainCrossfadeSamples = GrainCrossfadeSamplesForConstantRPMs;

					GrainTableIndex += FMath::RandRange(-GrainTableRandomOffsetForConstantRPMs, GrainTableRandomOffsetForConstantRPMs);
					GrainTableIndex = FMath::Clamp(GrainTableIndex, 0, SynthData->GrainTable.Num());
				}
				else 
				{
					// Update the starting index that was passed in to optimize grain-table look up for future spawns
					StartingIndex = GrainTableIndex;
				}

				// compute the grain duration based on the NumGrainEntriesPerGrain
				// we walk the grain table and add grain table durations together to reach a final duration		
				int32 NextGrainTableIndex = GrainTableIndex + NumGrainEntries + 1;
				int32 NextGrainTableIndexClamped = FMath::Clamp(NextGrainTableIndex, 0, SynthData->GrainTable.Num() - 1);
				int32 GrainTableIndexClamped = FMath::Clamp(GrainTableIndex, 0, SynthData->GrainTable.Num() - 1);
				int32 GrainDuration = SynthData->GrainTable[NextGrainTableIndexClamped].SampleIndex - SynthData->GrainTable[GrainTableIndexClamped].SampleIndex;

				// Get the RPM value for the very next grain after this grain duration to be the "ending rpm"
				// This allows us to pitch-scale the grain more closely to the grain's RPM contour through it's lifetime
				int32 EndingRPM = SynthData->GrainTable[NextGrainTableIndexClamped].RPM;

				int32 StartIndex = FMath::Max(0, Entry->SampleIndex - NewGrainCrossfadeSamples);
				int32 EndIndex = FMath::Min(Entry->SampleIndex + GrainDuration + NewGrainCrossfadeSamples, SynthData->AudioSource.Num());

				TArrayView<const int16> GrainArrayView = MakeArrayView(&SynthData->AudioSource[StartIndex], EndIndex - StartIndex);
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

void FMotoSynthGrainRuntime::Init(const TArrayView<const int16>& InGrainArrayView, int32 InNumSamplesCrossfade, float InGrainStartRPM, float InGrainEndRPM, float InStartingRPM)
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
		float PreviousSampleValue = (float)GrainArrayView[PreviousIndex] / 32767.0f;
		float NextSampleValue = (float)GrainArrayView[NextIndex] / 32767.0f;
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
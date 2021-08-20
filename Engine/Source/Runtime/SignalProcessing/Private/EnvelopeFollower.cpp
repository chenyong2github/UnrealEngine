// Copyright Epic Games, Inc. All Rights Reserved.

#include "DSP/EnvelopeFollower.h"
#include "DSP/Dsp.h"
#include "DSP/BufferVectorOperations.h"

namespace Audio
{
	// see https://en.wikipedia.org/wiki/RC_time_constant
	// Time constants indicate how quickly the envelope follower responds to changes in input
	static const float AnalogTImeConstant = 1.00239343f;
	static const float DigitalTimeConstant = 4.60517019f;

	FEnvelopeFollower::FEnvelopeFollower()
		: EnvMode(EPeakMode::Peak)
		, MeanWindowSize(DefaultWindowSize)
		, MeanHopSize(DefaultHopSize)
		, SampleRate(44100.0f)
		, AttackTimeMsec(0.0f)
		, AttackTimeSamples(0.0f)
		, ReleaseTimeMsec(0.0f)
		, ReleaseTimeSamples(0.0f)
		, CurrentEnvelopeValue(0.0f)
		, bIsAnalog(true)
	{
	}

	FEnvelopeFollower::FEnvelopeFollower(const float InSampleRate,
		const float InAttackTimeMsec,
		const float InReleaseTimeMSec,
		const EPeakMode::Type InMode,
		const bool bInIsAnalog,
		const int32 InWindowSizeForMean,
		const int32 InHopSizeForMean)		
	{
		Init(InSampleRate, InAttackTimeMsec, InReleaseTimeMSec, InMode, bInIsAnalog, InWindowSizeForMean, InHopSizeForMean);
	}

	FEnvelopeFollower::~FEnvelopeFollower()
	{
	}

	void FEnvelopeFollower::Init(const float InSampleRate, 
		const float InAttackTimeMsec, 
		const float InReleaseTimeMSec, 
		const EPeakMode::Type InMode, 
		const bool bInIsAnalog,
		const int32 InWindowSizeForMean,
		const int32 InHopSizeForMean)
	{
		check(InWindowSizeForMean > 0);
		check(InHopSizeForMean > 0);
		check(InWindowSizeForMean > InHopSizeForMean);

		SampleRate = InSampleRate;

		bIsAnalog = bInIsAnalog;
		EnvMode = InMode;

		MeanWindowSize = InWindowSizeForMean;
		MeanHopSize = InHopSizeForMean;

		// Set the attack and release times using the default values
		SetAttackTime(InAttackTimeMsec);
		SetReleaseTime(InReleaseTimeMSec);

		ScratchBuffer.SetNum(InWindowSizeForMean);
	}

	void FEnvelopeFollower::Reset()
	{
		CurrentEnvelopeValue = 0.0f;
	}

	void FEnvelopeFollower::SetAnalog(const bool bInIsAnalog)
	{
		bIsAnalog = bInIsAnalog;
		SetAttackTime(AttackTimeMsec);
		SetReleaseTime(ReleaseTimeMsec);
	}

	void FEnvelopeFollower::SetAttackTime(const float InAttackTimeMsec)
	{
		AttackTimeMsec = InAttackTimeMsec;
		const float TimeConstant = bIsAnalog ? AnalogTImeConstant : DigitalTimeConstant;
		AttackTimeSamples = FMath::Exp(-1000.0f * TimeConstant / (AttackTimeMsec * SampleRate));
	}

	void FEnvelopeFollower::SetReleaseTime(const float InReleaseTimeMsec)
	{
		ReleaseTimeMsec = InReleaseTimeMsec;
		const float TimeConstant = bIsAnalog ? AnalogTImeConstant : DigitalTimeConstant;
		ReleaseTimeSamples = FMath::Exp(-1000.0f * TimeConstant / (InReleaseTimeMsec * SampleRate));
	}

	void FEnvelopeFollower::SetMode(const EPeakMode::Type InMode)
	{
		EnvMode = InMode;
	}

	float FEnvelopeFollower::ProcessAudio(const float InAudioSample)
	{
		ProcessAudio(&InAudioSample, 1);

		// Update and return the envelope value
		return CurrentEnvelopeValue;
	}

	float FEnvelopeFollower::ProcessAudio(const float* InAudioBuffer, int32 InNumSamples)
	{
		// MS/RMS
		if (EnvMode == EPeakMode::MeanSquared || EnvMode == EPeakMode::RootMeanSquared)
		{
			TCircularAudioBuffer<float> SumBuffer(MeanWindowSize);
			int32 SampleIndex = 0;
			// This loop iterates per-window, not per-sample
			while (SampleIndex < InNumSamples)
			{
				// Get a new Window
				const int32 SamplesToCopy = MeanHopSize < (InNumSamples - SampleIndex) ? MeanHopSize : (InNumSamples - SampleIndex);
				SumBuffer.Push(InAudioBuffer + SampleIndex, SamplesToCopy);
				float* CurrentWindow = ScratchBuffer.GetData();
				SumBuffer.Pop(CurrentWindow, SamplesToCopy);
				
				// Calculate MS/RMS
				float CurrentMean;
				ArrayMeanSquared(TArrayView<float>(CurrentWindow, SamplesToCopy), CurrentMean);
				if (EnvMode == EPeakMode::RootMeanSquared)
				{
					CurrentMean = FMath::Sqrt(CurrentMean);
				}

				// Use MS/RMS to drive envelope following
				for (int i = 0; i < SamplesToCopy; ++i)
				{
					ProcessAudioNonClamped(CurrentMean);
				}
				SampleIndex += SamplesToCopy;
			}
		}
		// Peak mode
		else
		{
			for (int32 SampleIndex = 0; SampleIndex < InNumSamples; ++SampleIndex)
			{
				ProcessAudioNonClamped(FMath::Abs(InAudioBuffer[SampleIndex]));
			}
		}
		return CurrentEnvelopeValue = FMath::Clamp(CurrentEnvelopeValue, 0.0f, 1.0f);
	}

	float FEnvelopeFollower::ProcessAudio(const float* InAudioBuffer, float* OutAudioBuffer, int32 InNumSamples)
	{
		// MS/RMS
		if (EnvMode == EPeakMode::MeanSquared || EnvMode == EPeakMode::RootMeanSquared)
		{
			TCircularAudioBuffer<float> SumBuffer(MeanWindowSize);
			int32 SampleIndex = 0;
			// This loop iterates per-window, not per-sample
			while (SampleIndex < InNumSamples)
			{
				// Get a new Window
				const int32 SamplesToCopy = MeanHopSize < (InNumSamples - SampleIndex) ? MeanHopSize : (InNumSamples - SampleIndex);
				SumBuffer.Push(InAudioBuffer + SampleIndex, SamplesToCopy);
				float* CurrentWindow = ScratchBuffer.GetData();
				SumBuffer.Pop(CurrentWindow, MeanWindowSize);

				// Calculate MS/RMS
				float CurrentMean;
				ArrayMeanSquared(TArrayView<float>(CurrentWindow, SamplesToCopy), CurrentMean);
				if (EnvMode == EPeakMode::RootMeanSquared)
				{
					CurrentMean = FMath::Sqrt(CurrentMean);
				}

				// Use MS/RMS to drive envelope following
				for (int i = 0; i < SamplesToCopy; ++i)
				{
					OutAudioBuffer[SampleIndex + i] = ProcessAudioNonClamped(CurrentMean);
				}
				SampleIndex += SamplesToCopy;
			}
		}
		// Peak
		else
		{
			for (int32 SampleIndex = 0; SampleIndex < InNumSamples; ++SampleIndex)
			{
				OutAudioBuffer[SampleIndex] = ProcessAudioNonClamped(FMath::Abs(InAudioBuffer[SampleIndex]));
			}
		}

		Audio::BufferRangeClampFast(OutAudioBuffer, InNumSamples, 0.0f, 1.0f);
		return CurrentEnvelopeValue;
	}

	float FEnvelopeFollower::ProcessAudioNonClamped(const float InAudioSample)
	{
		float TimeSamples = (InAudioSample > CurrentEnvelopeValue) ? AttackTimeSamples : ReleaseTimeSamples;
		float NewEnvelopeValue = TimeSamples * (CurrentEnvelopeValue - InAudioSample) + InAudioSample;
		NewEnvelopeValue = Audio::UnderflowClamp(NewEnvelopeValue);

		// Update and return the envelope value
		return CurrentEnvelopeValue = NewEnvelopeValue;
	}

	int16 FEnvelopeFollower::ProcessAudio(const int16 InAudioSample)
	{
		// Convert to float
		float SampleValueFloat = (float)InAudioSample / 32767.0f;

		// Process it
		float Result = ProcessAudio(SampleValueFloat);

		// Convert back to int16
		return (int16)(Result * 32767.0f);
	}

	float FEnvelopeFollower::GetCurrentValue() const
	{
		return CurrentEnvelopeValue;
	}

}

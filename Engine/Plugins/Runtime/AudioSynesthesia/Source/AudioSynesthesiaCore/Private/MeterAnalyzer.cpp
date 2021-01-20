// Copyright Epic Games, Inc. All Rights Reserved.


#include "MeterAnalyzer.h"
#include "AudioMixer.h"
#include "DSP/BufferVectorOperations.h"
#include "DSP/DeinterleaveView.h"


namespace Audio
{
	FMeterAnalyzer::FMeterAnalyzer(float InSampleRate, int32 InNumChannels, const FMeterAnalyzerSettings& InSettings)
	{
		NumChannels = InNumChannels;
		SampleRate = InSampleRate;
		Settings = InSettings;
		check(NumChannels > 0);
		check(SampleRate > 0.0f);

		PeakEnvDataPerChannel.AddDefaulted(NumChannels);
		PeakDataPerChannel.AddDefaulted(NumChannels);
		ClippingDataPerChannel.AddDefaulted(NumChannels);

		for (int32 ChannelIndex = 0; ChannelIndex < NumChannels; ++ChannelIndex)
		{
			MeterEnvelopeFollowers.Add(MakeUnique<FEnvelopeFollower>(InSampleRate, (float)InSettings.MeterAttackTime, (float)InSettings.MeterReleaseTime, InSettings.MeterPeakMode, true));
		}
	}

	FMeterAnalyzerResults FMeterAnalyzer::ProcessAudio(TArrayView<const float> InSampleView)
	{
		// Feed audio through the envelope followers
		for (int32 SampleIndex = 0; SampleIndex < InSampleView.Num(); SampleIndex += NumChannels)
		{
			for (int32 ChannelIndex = 0; ChannelIndex < NumChannels; ++ChannelIndex)
			{
				float Sample = InSampleView[SampleIndex + ChannelIndex];

				// Check for clipping in this block of audio
				FClippingData& ClippingData = ClippingDataPerChannel[ChannelIndex];
				if (Sample > Settings.ClippingThreshold)
				{
					// Track how many samples clipped in this block
					ClippingData.NumSamplesClipping++;

					// Track the max clipping value in this block of audio
					ClippingData.ClippingValue = FMath::Max(ClippingData.ClippingValue, Sample);
				}

				TUniquePtr<FEnvelopeFollower>& MeterEnvelopeFollower = MeterEnvelopeFollowers[ChannelIndex];
				float NewPeakEnvValue = MeterEnvelopeFollower->ProcessAudio(Sample);

				FPeakEnvelopeData& PeakEnvData = PeakEnvDataPerChannel[ChannelIndex];
				FPeakData& PeakData = PeakDataPerChannel[ChannelIndex];

				// Positive if we're rising
				bool bNewSlopeIsPositive = (NewPeakEnvValue - PeakEnvData.Value) > 0.0f;

				// Detect if the new peak envelope value is less than the previous peak envelope value (i.e. local maximum)
				if (!bNewSlopeIsPositive && PeakEnvData.bEnvelopeSlopeIsPositive)
				{
					// Get the current time
					float CurrentTimeSec = (PeakEnvData.SampleCount + SampleIndex) / SampleRate;

					// Check if the peak value is larger than the current peak value or if enough time has elapsed to store a new peak
					if (PeakData.Value < PeakEnvData.Value || (CurrentTimeSec - PeakData.StartTime) >= (0.001f * Settings.PeakHoldTime))
					{
						PeakData.Value = PeakEnvData.Value;
						PeakData.StartTime = CurrentTimeSec;
					}
				}

				// Update the current peak envelope data to the new value
				PeakEnvData.Value = NewPeakEnvValue;
				PeakEnvData.bEnvelopeSlopeIsPositive = bNewSlopeIsPositive;
			}
		}

		// Build the results data
		FMeterAnalyzerResults Results;

		for (int32 ChannelIndex = 0; ChannelIndex < NumChannels; ++ChannelIndex)
		{
			// Add clipping data
			FClippingData& ClippingData = ClippingDataPerChannel[ChannelIndex];

			Results.ClippingValues.Add(ClippingData.ClippingValue);
			Results.NumSamplesClipping.Add(ClippingData.NumSamplesClipping);

			// Reset the clipping data for next block of audio
			ClippingData.ClippingValue = 0.0f;
			ClippingData.NumSamplesClipping = 0;

			// update sample count of the env data
			FPeakEnvelopeData& PeakEnvData = PeakEnvDataPerChannel[ChannelIndex];
			PeakEnvData.SampleCount += InSampleView.Num();

			TUniquePtr<FEnvelopeFollower>& MeterEnvelopeFollower = MeterEnvelopeFollowers[ChannelIndex];
			Results.MeterValues.Add(MeterEnvelopeFollower->GetCurrentValue());

			FPeakData& PeakData = PeakDataPerChannel[ChannelIndex];
			Results.PeakValues.Add(PeakData.Value);

			Results.TimeSec = PeakEnvData.SampleCount / SampleRate;
		}

		return Results;
	}

	const FMeterAnalyzerSettings& FMeterAnalyzer::GetSettings() const
	{
		return Settings;
	}
}

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DSP/EnvelopeFollower.h"

namespace Audio
{
	struct AUDIOSYNESTHESIACORE_API FMeterAnalyzerSettings
	{	
		// Envelope follower mode
		EPeakMode::Type MeterPeakMode = EPeakMode::RootMeanSquared;

		// Envelope follower attack time in milliseconds
		int32 MeterAttackTime = 300;

		// Envelope follower release time in milliseconds
		int32 MeterReleaseTime = 300;
		
		// Peak detector hold time in milliseconds
		int32 PeakHoldTime = 100;

		// The volume threshold to detect clipping
		float ClippingThreshold = 1.0f;
	};

	// Per-channel analyzer results
	struct FMeterAnalyzerResults
	{
		float TimeSec = 0.0f;
		TArray<float> MeterValues;
		TArray<float> PeakValues;
		TArray<float> ClippingValues;
		TArray<int32> NumSamplesClipping;
	};

	class FMeterAnalyzer
	{
	public:

		/** Construct analyzer */
		FMeterAnalyzer(float InSampleRate, int32 NumChannels, const FMeterAnalyzerSettings& InSettings);

		/**
		 * Calculate the meter results for the input samples.  Will return the current meter and the current peak value of the analyzer.
		 */
		FMeterAnalyzerResults ProcessAudio(TArrayView<const float> InSampleView);

		/**
		 * Return const reference to settings used inside this analyzer.
		 */
		const FMeterAnalyzerSettings& GetSettings() const;

	protected:
		// Envelope follower per channel
		TArray<TUniquePtr<FEnvelopeFollower>> MeterEnvelopeFollowers;

		// Per-channel clipping data
		struct FClippingData
		{
			// How many samples we've been clipping
			int32 NumSamplesClipping = 0;
			float ClippingValue = 0.0f;
		};
		TArray<FClippingData> ClippingDataPerChannel;

		// State to track the peak data
		struct FPeakData
		{
			float Value = 0.0f;
			float StartTime = 0.0f;
		};
		// Per-channel peak data
		TArray<FPeakData> PeakDataPerChannel;

		struct FPeakEnvelopeData
		{
			// Current envelope data
			float Value = 0.0f;

			// Running sample count of the envelope data
			int32 SampleCount = 0;

			// Current slope of the envelope
			bool bEnvelopeSlopeIsPositive = false;
		};
		// Per-channel envelope data
		TArray<FPeakEnvelopeData> PeakEnvDataPerChannel;

		FMeterAnalyzerSettings Settings;
		float SampleRate = 0.0f;
		int32 NumChannels = 0;
	};
}


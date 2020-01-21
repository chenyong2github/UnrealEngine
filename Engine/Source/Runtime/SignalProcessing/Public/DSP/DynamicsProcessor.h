// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DSP/EnvelopeFollower.h"
#include "DSP/Delay.h"
#include "Filter.h"

namespace Audio
{
	// What mode the compressor is in
	namespace EDynamicsProcessingMode
	{
		enum Type
		{
			Compressor,
			Limiter,
			Expander,
			Gate,
			Count
		};
	}

	enum class EDynamicsProcessorChannelLinkMode : uint8
	{
		Disabled,
		Average,
		Peak,
		Count
	};

	// Dynamic range compressor
	// https://en.wikipedia.org/wiki/Dynamic_range_compression
	class SIGNALPROCESSING_API FDynamicsProcessor
	{
	public:
		FDynamicsProcessor();
		~FDynamicsProcessor();

		void Init(const float SampleRate, const int32 NumChannels = 2);

		void SetLookaheadMsec(const float InLookAheadMsec);
		void SetAttackTime(const float InAttackTimeMsec);
		void SetReleaseTime(const float InReleaseTimeMsec);
		void SetThreshold(const float InThresholdDb);
		void SetRatio(const float InCompressionRatio);
		void SetKneeBandwidth(const float InKneeBandwidthDb);
		void SetInputGain(const float InInputGainDb);
		void SetKeyAudition(const bool InAuditionEnabled);
		void SetKeyGain(const float InKeyGain);
		void SetKeyHighshelfCutoffFrequency(const float InCutoffFreq);
		void SetKeyHighshelfEnabled(const bool bInEnabled);
		void SetKeyHighshelfGain(const float InGainDb);
		void SetKeyLowshelfCutoffFrequency(const float InCutoffFreq);
		void SetKeyLowshelfEnabled(const bool bInEnabled);
		void SetKeyLowshelfGain(const float InGainDb);
		void SetOutputGain(const float InOutputGainDb);
		void SetChannelLinkMode(const EDynamicsProcessorChannelLinkMode InLinkMode);
		void SetAnalogMode(const bool bInIsAnalogMode);
		void SetPeakMode(const EPeakMode::Type InEnvelopeFollowerModeType);
		void SetProcessingMode(const EDynamicsProcessingMode::Type ProcessingMode);

		void ProcessAudioFrame(const float* InFrame, float* OutFrame);
		void ProcessAudio(const float* InBuffer, const int32 InNumSamples, float* OutBuffer);


	protected:

		float ComputeGain(const float InEnvFollowerDb);

		// (Optional) Low-pass filter for input signal
		FBiquadFilter InputLowshelfFilter;

		// (Optional) High-pass filter for input signal
		FBiquadFilter InputHighshelfFilter;

		EDynamicsProcessingMode::Type ProcessingMode;

		// Lookahead delay lines
		TArray<FDelay> LookaheadDelay;

		// Envelope followers
		TArray<FEnvelopeFollower> EnvFollower;

		// Points in the knee used for lagrangian interpolation
		TArray<FVector2D> KneePoints;

		// Channel values of cached detector sample
		TArray<float> DetectorOuts;

		// Channel values of cached gain sample
		TArray<float> Gain;

		// How far ahead to look in the audio
		float LookaheedDelayMsec;

		// The period of which the compressor decreases gain to the level determined by the compression ratio
		float AttackTimeMsec;

		// The period of which the compressor increases gain to 0 dB once level has fallen below the threshold
		float ReleaseTimeMsec;

		// Amplitude threshold above which gain will be reduced
		float ThresholdDb;

		// Amount of gain reduction
		float Ratio;

		// Defines how hard or soft the gain reduction blends from no gain reduction to gain reduction (determined by the ratio)
		float HalfKneeBandwidthDb;

		// Amount of input gain
		float InputGain;

		// Amount of output gain
		float OutputGain;

		// Gain of key detector signal in dB
		float KeyGain;

		// Number of channels to use for the dynamics processor
		int32 NumChannels;

		// Whether or not input channels are linked, and if so, how to calculate gain
		EDynamicsProcessorChannelLinkMode LinkMode;

		// Whether or not we're in analog mode
		bool bIsAnalogMode;

		// Whether or not to bypass processor and only output key modulator
		bool bKeyAuditionEnabled;

		// Whether or not key high-pass filter is enabled
		bool bKeyHighshelfEnabled;

		// Whether or not key low-pass filter is enabled
		bool bKeyLowshelfEnabled;
	};
}

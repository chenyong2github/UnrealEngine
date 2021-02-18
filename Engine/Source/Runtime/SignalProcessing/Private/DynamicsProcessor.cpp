// Copyright Epic Games, Inc. All Rights Reserved.

#include "DSP/DynamicsProcessor.h"

namespace Audio
{
	FDynamicsProcessor::FDynamicsProcessor()
		: ProcessingMode(EDynamicsProcessingMode::Compressor)
		, EnvelopeFollowerPeakMode(EPeakMode::RootMeanSquared)
		, LookaheedDelayMsec(10.0f)
		, AttackTimeMsec(20.0f)
		, ReleaseTimeMsec(1000.0f)
		, ThresholdDb(-6.0f)
		, Ratio(1.0f)
		, HalfKneeBandwidthDb(5.0f)
		, InputGain(1.0f)
		, OutputGain(1.0f)
		, KeyGain(1.0f)
		, LinkMode(EDynamicsProcessorChannelLinkMode::Disabled)
		, bIsAnalogMode(true)
		, bKeyAuditionEnabled(false)
		, bKeyHighshelfEnabled(false)
		, bKeyLowshelfEnabled(false)
	{
		// The knee will have 2 points
		KneePoints.Init(FVector2D(), 2);
	}

	FDynamicsProcessor::~FDynamicsProcessor()
	{
	}

	void FDynamicsProcessor::Init(const float InSampleRate, const int32 InNumChannels)
	{
		SampleRate = InSampleRate;

		SetNumChannels(InNumChannels);
		SetKeyNumChannels(InNumChannels);
	
		LookaheadDelay.Reset();
		LookaheadDelay.AddDefaulted(InNumChannels);

		for (int32 Channel = 0; Channel < InNumChannels; ++Channel)
		{
			LookaheadDelay[Channel].Init(SampleRate, 0.1f);
			LookaheadDelay[Channel].SetDelayMsec(LookaheedDelayMsec);

			EnvFollower[Channel].Init(SampleRate, AttackTimeMsec, ReleaseTimeMsec, EPeakMode::RootMeanSquared, bIsAnalogMode);
		}

		InputLowshelfFilter.Init(SampleRate, InNumChannels, EBiquadFilter::LowShelf);
		InputHighshelfFilter.Init(SampleRate, InNumChannels, EBiquadFilter::HighShelf);

		DetectorOuts.Reset();
		DetectorOuts.AddZeroed(InNumChannels);

		Gain.Reset();
		Gain.AddZeroed(InNumChannels);
	}

	int32 FDynamicsProcessor::GetNumChannels() const
	{
		return Gain.Num();
	}

	int32 FDynamicsProcessor::GetKeyNumChannels() const
	{
		return EnvFollower.Num();
	}
	
	void FDynamicsProcessor::SetLookaheadMsec(const float InLookAheadMsec)
	{
		LookaheedDelayMsec = InLookAheadMsec;
		for (int32 Channel = 0; Channel < LookaheadDelay.Num(); ++Channel)
		{
			LookaheadDelay[Channel].SetDelayMsec(LookaheedDelayMsec);
		}
	}

	void FDynamicsProcessor::SetAttackTime(const float InAttackTimeMsec)
	{
		AttackTimeMsec = InAttackTimeMsec;
		for (int32 Channel = 0; Channel < EnvFollower.Num(); ++Channel)
		{
			EnvFollower[Channel].SetAttackTime(InAttackTimeMsec);
		}
	}

	void FDynamicsProcessor::SetReleaseTime(const float InReleaseTimeMsec)
	{
		ReleaseTimeMsec = InReleaseTimeMsec;
		for (int32 Channel = 0; Channel < EnvFollower.Num(); ++Channel)
		{
			EnvFollower[Channel].SetReleaseTime(InReleaseTimeMsec);
		}
	}

	void FDynamicsProcessor::SetThreshold(const float InThresholdDb)
	{
		ThresholdDb = InThresholdDb;
	}

	void FDynamicsProcessor::SetRatio(const float InCompressionRatio)
	{
		// Don't let the compression ratio be 0.0!
		Ratio = FMath::Max(InCompressionRatio, SMALL_NUMBER);
	}

	void FDynamicsProcessor::SetKneeBandwidth(const float InKneeBandwidthDb)
	{
		HalfKneeBandwidthDb = 0.5f * InKneeBandwidthDb;
	}

	void FDynamicsProcessor::SetInputGain(const float InInputGainDb)
	{
		InputGain = ConvertToLinear(InInputGainDb);
	}

	void FDynamicsProcessor::SetKeyAudition(const bool InAuditionEnabled)
	{
		bKeyAuditionEnabled = InAuditionEnabled;
	}

	void FDynamicsProcessor::SetKeyGain(const float InKeyGain)
	{
		KeyGain = ConvertToLinear(InKeyGain);
	}

	void FDynamicsProcessor::SetKeyHighshelfCutoffFrequency(const float InCutoffFreq)
	{
		InputHighshelfFilter.SetFrequency(InCutoffFreq);
	}

	void FDynamicsProcessor::SetKeyHighshelfEnabled(const bool bInEnabled)
	{
		bKeyHighshelfEnabled = bInEnabled;
	}

	void FDynamicsProcessor::SetKeyHighshelfGain(const float InGainDb)
	{
		InputHighshelfFilter.SetGainDB(InGainDb);
	}

	void FDynamicsProcessor::SetKeyLowshelfCutoffFrequency(const float InCutoffFreq)
	{
		InputLowshelfFilter.SetFrequency(InCutoffFreq);
	}

	void FDynamicsProcessor::SetKeyLowshelfEnabled(const bool bInEnabled)
	{
		bKeyLowshelfEnabled = bInEnabled;
	}

	void FDynamicsProcessor::SetKeyLowshelfGain(const float InGainDb)
	{
		InputLowshelfFilter.SetGainDB(InGainDb);
	}

	void FDynamicsProcessor::SetKeyNumChannels(const int32 InNumChannels)
	{
		if (InNumChannels != EnvFollower.Num())
		{
			EnvFollower.Reset();
			EnvFollower.AddDefaulted(InNumChannels);

			for (int32 Channel = 0; Channel < InNumChannels; ++Channel)
			{
				EnvFollower[Channel].Init(SampleRate, AttackTimeMsec, ReleaseTimeMsec, EnvelopeFollowerPeakMode, bIsAnalogMode);
			}
		}

		if (InNumChannels != InputLowshelfFilter.GetNumChannels())
		{
			InputLowshelfFilter.Init(SampleRate, InNumChannels, EBiquadFilter::LowShelf);
		}

		if (InNumChannels != InputHighshelfFilter.GetNumChannels())
		{
			InputHighshelfFilter.Init(SampleRate, InNumChannels, EBiquadFilter::HighShelf);
		}

		if (InNumChannels != DetectorOuts.Num())
		{
			DetectorOuts.Reset();
			DetectorOuts.AddZeroed(InNumChannels);
		}
	}

	void FDynamicsProcessor::SetOutputGain(const float InOutputGainDb)
	{
		OutputGain = ConvertToLinear(InOutputGainDb);
	}

	void FDynamicsProcessor::SetChannelLinkMode(const EDynamicsProcessorChannelLinkMode InLinkMode)
	{
		LinkMode = InLinkMode;
	}

	void FDynamicsProcessor::SetAnalogMode(const bool bInIsAnalogMode)
	{
		bIsAnalogMode = bInIsAnalogMode;
		for (int32 Channel = 0; Channel < EnvFollower.Num(); ++Channel)
		{
			EnvFollower[Channel].SetAnalog(bInIsAnalogMode);
		}
	}

	void FDynamicsProcessor::SetNumChannels(const int32 InNumChannels)
	{
		if (InNumChannels != Gain.Num())
		{
			Gain.Reset();
			Gain.AddZeroed(InNumChannels);
		}

		if (InNumChannels != LookaheadDelay.Num())
		{
			LookaheadDelay.Reset();
			LookaheadDelay.AddDefaulted(InNumChannels);

			for (int32 Channel = 0; Channel < InNumChannels; ++Channel)
			{
				LookaheadDelay[Channel].Init(SampleRate, 0.1f);
				LookaheadDelay[Channel].SetDelayMsec(LookaheedDelayMsec);
			}
		}
	}

	void FDynamicsProcessor::SetPeakMode(const EPeakMode::Type InEnvelopeFollowerModeType)
	{
		EnvelopeFollowerPeakMode = InEnvelopeFollowerModeType;
		for (int32 Channel = 0; Channel < EnvFollower.Num(); ++Channel)
		{
			EnvFollower[Channel].SetMode(InEnvelopeFollowerModeType);
		}
	}

	void FDynamicsProcessor::SetProcessingMode(const EDynamicsProcessingMode::Type InProcessingMode)
	{
		ProcessingMode = InProcessingMode;
	}

	void FDynamicsProcessor::ProcessAudioFrame(const float* InFrame, float* OutFrame, const float* InKeyFrame)
	{
		const bool bKeyIsInput = InFrame == InKeyFrame;
		if (ProcessKeyFrame(InKeyFrame, OutFrame, bKeyIsInput))
		{
			const int32 NumChannels = GetNumChannels();
			for (int32 Channel = 0; Channel < NumChannels; ++Channel)
			{
				// Write and read into the look ahead delay line.
				// We apply the compression output of the direct input to the output of this delay line
				// This way sharp transients can be "caught" with the gain.
				float LookaheadOutput = LookaheadDelay[Channel].ProcessAudioSample(InFrame[Channel]);

				// Write into the output with the computed gain value
				OutFrame[Channel] = Gain[Channel] * LookaheadOutput * OutputGain * InputGain;
			}
		}
	}

	void FDynamicsProcessor::ProcessAudio(const float* InBuffer, const int32 InNumSamples, float* OutBuffer, const float* InKeyBuffer)
	{
		const int32 NumChannels = GetNumChannels();
		const int32 KeyNumChannels = GetKeyNumChannels();

		if (InKeyBuffer)
		{
			int32 KeySampleIndex = 0;
			for (int32 SampleIndex = 0; SampleIndex < InNumSamples; SampleIndex += NumChannels)
			{
				const float* KeyFrame = &InKeyBuffer[KeySampleIndex];
				ProcessAudioFrame(&InBuffer[SampleIndex], &OutBuffer[SampleIndex], KeyFrame);
				KeySampleIndex += KeyNumChannels;
			}
		}
		else
		{
			for (int32 SampleIndex = 0; SampleIndex < InNumSamples; SampleIndex += NumChannels)
			{
				const float* KeyFrame = &InBuffer[SampleIndex];
				ProcessAudioFrame(&InBuffer[SampleIndex], &OutBuffer[SampleIndex], KeyFrame);
			}
		}
	}

	bool FDynamicsProcessor::ProcessKeyFrame(const float* InKeyFrame, float* OutFrame, bool bKeyIsInput)
	{
		// Get detector outputs
		const float* KeyIn = InKeyFrame;

		const int32 KeyNumChannels = GetKeyNumChannels();
		const int32 NumChannels = GetNumChannels();
		if (KeyNumChannels > 0)
		{
			if (bKeyLowshelfEnabled)
			{
				InputLowshelfFilter.ProcessAudioFrame(KeyIn, DetectorOuts.GetData());
				KeyIn = DetectorOuts.GetData();
			}

			if (bKeyHighshelfEnabled)
			{
				InputHighshelfFilter.ProcessAudioFrame(KeyIn, DetectorOuts.GetData());
				KeyIn = DetectorOuts.GetData();
			}
		}

		float DetectorGain = InputGain;

		// Apply key gain only if detector is key (not input)
		if (!bKeyIsInput)
		{
			DetectorGain *= KeyGain;
		}

		if (bKeyAuditionEnabled)
		{
			for (int32 Channel = 0; Channel < NumChannels; ++Channel)
			{
				const int32 KeyIndex = Channel % KeyNumChannels;
				OutFrame[Channel] = DetectorGain * KeyIn[KeyIndex];
			}

			return false;
		}

		for (int32 Channel = 0; Channel < KeyNumChannels; ++Channel)
		{
			DetectorOuts[Channel] = EnvFollower[Channel].ProcessAudio(DetectorGain * KeyIn[Channel]);
		}

		switch (LinkMode)
		{
			case EDynamicsProcessorChannelLinkMode::Average:
			{
				float KeyOutLinked = 0.0f;
				for (int32 Channel = 0; Channel < KeyNumChannels; ++Channel)
				{
					KeyOutLinked += DetectorOuts[Channel];
				}
				KeyOutLinked /= static_cast<float>(KeyNumChannels);
				const float DetectorOutLinkedDb = ConvertToDecibels(KeyOutLinked);
				const float ComputedGain = ComputeGain(DetectorOutLinkedDb);
				for (int32 Channel = 0; Channel < NumChannels; ++Channel)
				{
					Gain[Channel] = ComputedGain;
				}
			}
			break;

			case EDynamicsProcessorChannelLinkMode::Peak:
			{
				float KeyOutLinked = FMath::Max<float>(DetectorOuts);
				const float KeyOutLinkedDb = ConvertToDecibels(KeyOutLinked);
				const float ComputedGain = ComputeGain(KeyOutLinkedDb);
				for (int32 Channel = 0; Channel < NumChannels; ++Channel)
				{
					Gain[Channel] = ComputedGain;
				}
			}
			break;

			case EDynamicsProcessorChannelLinkMode::Disabled:
			default:
			{
				// Compute gain individually per channel and wrap if
				// channel count is greater than key channel count.
				for (int32 Channel = 0; Channel < NumChannels; ++Channel)
				{
					const int32 KeyIndex = Channel % KeyNumChannels;
					float ChannelGain = DetectorOuts[KeyIndex];

					const float KeyOutDb = ConvertToDecibels(ChannelGain);
					const float ComputedGain = ComputeGain(KeyOutDb);
					Gain[KeyIndex] = ComputedGain;
				}
			}
			break;
		}

		return true;
	}

	float FDynamicsProcessor::ComputeGain(const float InEnvFollowerDb)
	{
		float SlopeFactor = 0.0f;

		// Depending on the mode, we define the "slope". 
		switch (ProcessingMode)
		{
			default:

			// Compressors smoothly reduce the gain as the gain gets louder
			// CompressionRatio -> Inifinity is a limiter
			case EDynamicsProcessingMode::Compressor:
				SlopeFactor = 1.0f - 1.0f / Ratio;
				break;

			// Limiters do nothing until it hits the threshold then clamps the output hard
			case EDynamicsProcessingMode::Limiter:
				SlopeFactor = 1.0f;
				break;

			// Expanders smoothly increase the gain as the gain gets louder
			// CompressionRatio -> Infinity is a gate
			case EDynamicsProcessingMode::Expander:
				SlopeFactor = 1.0f / Ratio - 1.0f;
				break;

			// Gates are opposite of limiter. They stop sound (stop gain) until the threshold is hit
			case EDynamicsProcessingMode::Gate:
				SlopeFactor = -1.0f;
				break;
		}

		// If we are in the range of compression
		if (HalfKneeBandwidthDb > 0.0f && InEnvFollowerDb > (ThresholdDb - HalfKneeBandwidthDb) && InEnvFollowerDb < ThresholdDb + HalfKneeBandwidthDb)
		{
			// Setup the knee for interpolation. Don't allow the top knee point to exceed 0.0
			KneePoints[0].X = ThresholdDb - HalfKneeBandwidthDb;
			KneePoints[1].X = FMath::Min(ThresholdDb + HalfKneeBandwidthDb, 0.0f);

			KneePoints[0].Y = 0.0f;
			KneePoints[1].Y = SlopeFactor;

			// The knee calculation adjusts the slope to use via lagrangian interpolation through the slope
			SlopeFactor = LagrangianInterpolation(KneePoints, InEnvFollowerDb);
		}

		float OutputGainDb = SlopeFactor * (ThresholdDb - InEnvFollowerDb);
		OutputGainDb = FMath::Min(0.0f, OutputGainDb);
		return ConvertToLinear(OutputGainDb);
	}
}

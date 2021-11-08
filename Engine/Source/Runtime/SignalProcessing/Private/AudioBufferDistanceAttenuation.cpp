// Copyright Epic Games, Inc. All Rights Reserved.

#include "DSP/AudioBufferDistanceAttenuation.h"
#include "DSP/Dsp.h"
#include "DSP/BufferVectorOperations.h"

namespace Audio
{
	void FAudioBufferDistanceAttenuation::SetSettings(const FAudioBufferDistanceAttenuation::FSettings& InSettings)
	{
		FScopeLock Lock(&DistAttenCritSect);

		Settings = InSettings;

		// Always sure we have a valid curve if none is supplied.
		if (Settings.AttenuationCurve.Num() <= 1)
		{
			// Make it a default linear attenuation curve
			TArray<FVector2D> Points;
			Points.Add({ 0.0f, 1.0 });
			Points.Add({ 1.0f, 0.0 });
			Settings.AttenuationCurve.AddPoints(Points);
		}
	}

	float FAudioBufferDistanceAttenuation::ComputeNextLinearAttenuation(float InCurrentDistance)
	{
		float NextAttenuationDb = 0.0f;
		const float Denom = FMath::Max(Settings.DistanceRange.Y - Settings.DistanceRange.X, SMALL_NUMBER);
		const float Alpha = FMath::Clamp((InCurrentDistance - Settings.DistanceRange.X) / Denom, 0.0f, 1.0f);

		float CurveValue = 0.0f;

		bool bSuccess = Settings.AttenuationCurve.Eval(Alpha, CurveValue);

		// This should succeed since we ensure we always have at least two points in the curve when it's set
		check(bSuccess);

		// Note the curve is expected to map to the attenuation amount (i.e. at right-most value, it'll be 0.0, which corresponds to max dB attenuation)
		// This then needs to be used to interpolate the dB range (0.0 is no attenuation, -60 for example, is a lot of attenuation)
		NextAttenuationDb = FMath::Lerp(Settings.AttenuationDbAtMaxRange, 0.0f, CurveValue);

		float TargetAttenuationLinear = 0.0f;
		if (NextAttenuationDb > Settings.AttenuationDbAtMaxRange)
		{
			TargetAttenuationLinear = Audio::ConvertToLinear(NextAttenuationDb);
		}
		return TargetAttenuationLinear;
	}

	void FAudioBufferDistanceAttenuation::ProcessAudio(int16* InOutAudioFrames, uint32 InFrameCount, uint32 InNumChannels, float InCurrentDistance)
	{
		check(InOutAudioFrames != nullptr);
		check(InFrameCount > 0);
		check(InNumChannels > 0);
		check(InCurrentDistance >= 0.0f);

		FScopeLock Lock(&DistAttenCritSect);

		float TargetAttenuationLinear = ComputeNextLinearAttenuation(InCurrentDistance);

		// TODO: investigate adding int16 flavors of utilities in BufferVectorOperations.h to avoid format conversions
		uint32 CurrentSampleIndex = 0;
		const float DeltaValue = (TargetAttenuationLinear - CurrentAttenuationLinear) / InFrameCount;
		float Gain = CurrentAttenuationLinear;
		for (uint32 FrameIndex = 0; FrameIndex < InFrameCount; ++FrameIndex)
		{
			for (uint32 ChannelIndex = 0; ChannelIndex < InNumChannels; ++ChannelIndex)
			{
				InOutAudioFrames[CurrentSampleIndex + ChannelIndex] = InOutAudioFrames[CurrentSampleIndex + ChannelIndex] * Gain;
			}

			CurrentSampleIndex += InNumChannels;
			Gain += DeltaValue;
		}

		// Update the current attenuation linear for the next render block
		CurrentAttenuationLinear = TargetAttenuationLinear;
	}

	void FAudioBufferDistanceAttenuation::ProcessAudio(float* RESTRICT InOutAudioFrames, uint32 InFrameCount, uint32 InNumChannels, float InCurrentDistance)
	{
		check(InOutAudioFrames != nullptr);
		check(InFrameCount > 0);
		check(InNumChannels > 0);
		check(InCurrentDistance >= 0.0f);

		FScopeLock Lock(&DistAttenCritSect);

		float TargetAttenuationLinear = ComputeNextLinearAttenuation(InCurrentDistance);

		int32 NumSamples = (int32)(InFrameCount * InNumChannels);
		Audio::FadeBufferFast(InOutAudioFrames, NumSamples, CurrentAttenuationLinear, TargetAttenuationLinear);

		CurrentAttenuationLinear = TargetAttenuationLinear;
	}
}
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/CriticalSection.h"
#include "Math/Vector2D.h"
#include "DSP/AudioLinearCurve.h"

namespace Audio
{
	class SIGNALPROCESSING_API FAudioBufferDistanceAttenuation
	{
	public:
		// Settings for audio buffer distance attenuation
		struct FSettings
		{
			// The min and max distance range to apply attenuation to the voip stream based on distance from listener.
			FVector2D DistanceRange = { 400.0f, 4000.0f };

			// The attenuation (in Decibels) at max range. The attenuation will be performed in dB and clamped to zero (linear) greater than max attenuation past the max range.
			float AttenuationDbAtMaxRange = -60.0f;

			// A curve (values are expected to be normalized between 0.0 and 1.0) to use to control attenuation vs distance from listener.
			Audio::FLinearCurve AttenuationCurve;
		};

		FAudioBufferDistanceAttenuation() = default;
		virtual ~FAudioBufferDistanceAttenuation() = default;

		// Sets custom voice chat attenuation settings
		void SetSettings(const FAudioBufferDistanceAttenuation::FSettings& InSettings);

		// Processes the audio buffer with the given distance attenuation
		void ProcessAudio(int16* InOutAudioFrames, uint32 InFrameCount, uint32 InNumChannels, float InCurrentDistance);
		void ProcessAudio(float* RESTRICT InOutAudioFrames, uint32 InFrameCount, uint32 InNumChannels, float InCurrentDistance);

	private:
		float ComputeNextLinearAttenuation(float InCurrentDistance);
		float GetValueFromFloatCurve(float InAlpha);

		FCriticalSection DistAttenCritSect;
		FAudioBufferDistanceAttenuation::FSettings Settings;
		float CurrentAttenuationLinear = 1.0f;
	};

}
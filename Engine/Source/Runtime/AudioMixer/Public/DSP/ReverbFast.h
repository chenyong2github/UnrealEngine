// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DSP/BufferVectorOperations.h"
#include "DSP/IntegerDelay.h"
#include "DSP/LongDelayAPF.h"
#include "DSP/BufferOnePoleLPF.h"
#include "DSP/DynamicDelayAPF.h"
#include "DSP/LateReflectionsFast.h"
#include "DSP/EarlyReflectionsFast.h"

namespace Audio
{
	// Settings for plate reverb
	struct AUDIOMIXER_API FPlateReverbFastSettings 
	{
		// EQuadBehavior describes how reverb is generated when there are 5 or more output channels.
		enum class EQuadBehavior : uint8
		{
			// Only produce reverb in front left and front right.
			StereoOnly,
			// Produce reverb in front left and front right. Copy front left to rear left and front right to rear right.
			QuadMatched,
			// Produce reverb in front left and front right. Copy front left to rear right and front right to rear left.
			QuadFlipped
		};

		FPlateReverbFastSettings();

		bool operator==(const FPlateReverbFastSettings& Other) const;

		bool operator!=(const FPlateReverbFastSettings& Other) const;

		// EarlyReflectionSettings controls the initial perceived echoes from a sound, modeling the first few
		// orders of reflections from a sound source to the listener's ears. 
		FEarlyReflectionsFastSettings EarlyReflections;
		// LateReflectionSettings controls the long tail diffused echo modeling the higher order reflections
		// from a sound source to the listener's ears. 
		FLateReflectionsFastSettings LateReflections;

		// Mix amount between dry and wet signals.
		float Wetness;

		// Set how reverb module generates reverb when there are 5 or more output channels. 
		EQuadBehavior QuadBehavior;
	};

	// The Plate Reverb emulates the interactions between a sound, the listener and the space they share. Early reflections
	// are modeled using a feedback delay network while late reflections are modeled using a plate reverb. This 
	// class aims to support a flexible and pleasant sounding reverb balanced with computational efficiency. 
	class AUDIOMIXER_API FPlateReverbFast {
		public:
			static const float MaxWetness;
			static const float MinWetness;
			static const FPlateReverbFastSettings DefaultSettings;
			
			// InMaxInternalBufferSamples sets the maximum number of samples used in internal buffers.
			FPlateReverbFast(float InSampleRate, int32 InMaxInternalBufferSamples = 512, const FPlateReverbFastSettings& InSettings=DefaultSettings);

			~FPlateReverbFast();

			// Copies, clamps and applies settings.
			void SetSettings(const FPlateReverbFastSettings& InSettings);

			const FPlateReverbFastSettings& GetSettings() const;

			// Whether or not to enable late reflections
			void EnableLateReflections(const bool bInEnableLateReflections);

			// Whether or not to enable late reflections
			void EnableEarlyReflections(const bool bInEnableEarlyReflections);

			// Creates reverberated audio in OutSamples based upon InSamples
			// InNumChannels can be 1 or 2 channels.
			// OutSamples must be greater or equal to 2.
			void ProcessAudio(const AlignedFloatBuffer& InSamples, const int32 InNumChannels, AlignedFloatBuffer& OutSamples, const int32 OutNumChannels);

			// Clamp individual settings to values supported by this class.
			static void ClampSettings(FPlateReverbFastSettings& InOutSettings);

		private:
			// Copy input samples to output samples. Remap channels if necessary.
			void PassThroughAudio(const AlignedFloatBuffer& InSamples, const int32 InNumChannels, AlignedFloatBuffer& OutSamples, const int32 OutNumChannels);

			
			// Copy reverberated samples to interleaved output samples. Map channels according to internal settings.
			void InterleaveAndMixOutput(const AlignedFloatBuffer& InFrontLeftSamples, const AlignedFloatBuffer& InFrontRightSamples, AlignedFloatBuffer& OutSamples, const int32 OutNumChannels);

			void ApplySettings();




			float SampleRate;
			float LastWetness;
			bool bProcessCallSinceWetnessChanged;

			FPlateReverbFastSettings Settings;
			FEarlyReflectionsFast EarlyReflections;
			FLateReflectionsFast LateReflections;

			AlignedFloatBuffer FrontLeftLateReflectionsSamples;
			AlignedFloatBuffer FrontRightLateReflectionsSamples;
			AlignedFloatBuffer FrontLeftEarlyReflectionsSamples;
			AlignedFloatBuffer FrontRightEarlyReflectionsSamples;
			AlignedFloatBuffer FrontLeftReverbSamples;
			AlignedFloatBuffer FrontRightReverbSamples;
			AlignedFloatBuffer LeftAttenuatedSamples;
			AlignedFloatBuffer RightAttenuatedSamples;
			AlignedFloatBuffer ScaledInputBuffer;

			bool bEnableEarlyReflections;
			bool bEnableLateReflections;
	};
}

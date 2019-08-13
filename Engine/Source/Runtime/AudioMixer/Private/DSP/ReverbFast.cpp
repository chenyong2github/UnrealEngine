// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DSP/ReverbFast.h"
#include "DSP/BufferVectorOperations.h"
#include "Audio.h"

namespace Audio {
	FPlateReverbFastSettings::FPlateReverbFastSettings()
		: Wetness(0.5f)
		, QuadBehavior(EQuadBehavior::StereoOnly)
	{}

	bool FPlateReverbFastSettings::operator==(const FPlateReverbFastSettings& Other) const
	{
		bool bIsEqual = (
			(Other.EarlyReflections == EarlyReflections) &&
			(Other.LateReflections == LateReflections) &&
			(Other.Wetness == Wetness) &&
			(Other.QuadBehavior == QuadBehavior));
		
		return bIsEqual;
	}

	bool FPlateReverbFastSettings::operator!=(const FPlateReverbFastSettings& Other) const
	{
		return !(*this == Other);
	}


	const float FPlateReverbFast::MaxWetness = 10.f;
	const float FPlateReverbFast::MinWetness = 0.0f;

	const FPlateReverbFastSettings FPlateReverbFast::DefaultSettings;

	FPlateReverbFast::FPlateReverbFast(float InSampleRate, int32 InMaxInternalBufferSamples, const FPlateReverbFastSettings& InSettings)
		: SampleRate(InSampleRate)
		, LastWetness(0.0f)
		, bProcessCallSinceWetnessChanged(false)
		, EarlyReflections(InSampleRate, InMaxInternalBufferSamples)
		, LateReflections(InSampleRate, InMaxInternalBufferSamples, InSettings.LateReflections)
		, bEnableEarlyReflections(true)
		, bEnableLateReflections(true)
	{
		SetSettings(InSettings);
	}

	FPlateReverbFast::~FPlateReverbFast()
	{}

	void FPlateReverbFast::SetSettings(const FPlateReverbFastSettings& InSettings)
	{
		// Copy, clamp and apply settings
		if (bProcessCallSinceWetnessChanged)
		{
			LastWetness = Settings.Wetness;
			bProcessCallSinceWetnessChanged = false;
		}

		Settings = InSettings;
		ClampSettings(Settings);
		ApplySettings();
	}

	const FPlateReverbFastSettings& FPlateReverbFast::GetSettings() const
	{
		return Settings;
	}

	// Whether or not to enable late reflections
	void FPlateReverbFast::EnableLateReflections(const bool bInEnableLateReflections)
	{
		bEnableLateReflections = bInEnableLateReflections;
	}

	// Whether or not to enable late reflections
	void FPlateReverbFast::EnableEarlyReflections(const bool bInEnableEarlyReflections)
	{
		bEnableEarlyReflections = bInEnableEarlyReflections;
	}

	// Process a buffer of input audio samples.
	void FPlateReverbFast::ProcessAudio(const AlignedFloatBuffer& InSamples, const int32 InNumChannels, AlignedFloatBuffer& OutSamples, const int32 OutNumChannels)
	{
		ScaledInputBuffer.Reset(InSamples.Num());
		ScaledInputBuffer.AddUninitialized(InSamples.Num());
		check(ScaledInputBuffer.Num() == InSamples.Num());

		FMemory::Memcpy(ScaledInputBuffer.GetData(), InSamples.GetData(), InSamples.Num() * sizeof(float));

		// Scale input by wetness (or fade to new wetness)
		if (FMath::IsNearlyEqual(LastWetness, Settings.Wetness))
		{
			MultiplyBufferByConstantInPlace(ScaledInputBuffer.GetData(), InSamples.Num(), Settings.Wetness);
		}
		else
		{
			FadeBufferFast(ScaledInputBuffer, LastWetness, Settings.Wetness);
			LastWetness = Settings.Wetness;
		}


		checkf((1 == InNumChannels) || (2 == InNumChannels), TEXT("FPlateReverbFast only supports 1 or 2 channel inputs."))
		checkf(OutNumChannels >= 2, TEXT("FPlateReverbFast requires at least 2 output channels."))

		// Determine number of frames and output size.
		const int32 InNum = InSamples.Num();
		const int32 InNumFrames = InNum / InNumChannels;

		if (!bEnableEarlyReflections && !bEnableLateReflections)
		{
			// Zero output buffers if all reverb is disabled. 
			const int32 OutNum = InNumFrames * OutNumChannels;

			OutSamples.Reset(OutNum);
			OutSamples.AddUninitialized(OutNum);

			FMemory::Memset(OutSamples.GetData(), 0, sizeof(float) * OutNum);
			return;
		}

		// Resize internal buffers	
		FrontLeftReverbSamples.Reset(InNumFrames);
		FrontRightReverbSamples.Reset(InNumFrames);

		FrontLeftReverbSamples.AddUninitialized(InNumFrames);
		FrontRightReverbSamples.AddUninitialized(InNumFrames);

		if (bEnableEarlyReflections && !bEnableLateReflections)
		{
			// Only generate early reflections.
			EarlyReflections.ProcessAudio(ScaledInputBuffer, InNumChannels, FrontLeftReverbSamples, FrontRightReverbSamples);
		}
		else if (!bEnableEarlyReflections && bEnableLateReflections)
		{
			// Only generate late reflections.
			LateReflections.ProcessAudio(ScaledInputBuffer, InNumChannels, FrontLeftReverbSamples, FrontRightReverbSamples);
		}
		else if (bEnableEarlyReflections && bEnableLateReflections)
		{
			// Resize internal buffers
			FrontLeftLateReflectionsSamples.Reset(InNumFrames);
			FrontRightLateReflectionsSamples.Reset(InNumFrames);
			FrontLeftEarlyReflectionsSamples.Reset(InNumFrames);
			FrontRightEarlyReflectionsSamples.Reset(InNumFrames);

			FrontLeftLateReflectionsSamples.AddUninitialized(InNumFrames);
			FrontRightLateReflectionsSamples.AddUninitialized(InNumFrames);
			FrontLeftEarlyReflectionsSamples.AddUninitialized(InNumFrames);
			FrontRightEarlyReflectionsSamples.AddUninitialized(InNumFrames);

			// Generate both early reflections and late reflections 
			EarlyReflections.ProcessAudio(ScaledInputBuffer, InNumChannels, FrontLeftEarlyReflectionsSamples, FrontRightEarlyReflectionsSamples);
			LateReflections.ProcessAudio(ScaledInputBuffer, InNumChannels, FrontLeftLateReflectionsSamples, FrontRightLateReflectionsSamples);
			// Add early and late reflections together.
			SumBuffers(FrontLeftEarlyReflectionsSamples, FrontLeftLateReflectionsSamples, FrontLeftReverbSamples);
			SumBuffers(FrontRightEarlyReflectionsSamples, FrontRightLateReflectionsSamples, FrontRightReverbSamples);
		}


		// Interleave and upmix
		InterleaveAndMixOutput(FrontLeftReverbSamples, FrontRightReverbSamples, OutSamples, OutNumChannels);
		bProcessCallSinceWetnessChanged = true;
	}

	void FPlateReverbFast::ClampSettings(FPlateReverbFastSettings& InOutSettings)
	{
		// Clamp settings for this object and member objects.
		InOutSettings.Wetness = FMath::Clamp(InOutSettings.Wetness, MinWetness, MaxWetness);
		FLateReflectionsFast::ClampSettings(InOutSettings.LateReflections);
		FEarlyReflectionsFast::ClampSettings(InOutSettings.EarlyReflections);
	}

	// Copy input samples to output samples. Remap channels if necessary.
	void FPlateReverbFast::PassThroughAudio(const AlignedFloatBuffer& InSamples, const int32 InNumChannels, AlignedFloatBuffer& OutSamples, const int32 OutNumChannels)
	{
		const int32 InNum = InSamples.Num();
		const int32 InNumFrames = InNum / InNumChannels;
		const int32 OutNum = OutNumChannels * InNumFrames;

		// Resize output buffer
		OutSamples.Reset(OutNum);
		OutSamples.AddUninitialized(OutNum);
		FMemory::Memset(OutSamples.GetData(), 0, sizeof(float) * OutNum);

		if (InNum > 0)
		{
			if (InNumChannels == OutNumChannels)
			{
				FMemory::Memcpy(OutSamples.GetData(), InSamples.GetData(), sizeof(float) * InNum);
			}
			else
			{
				// InNumChannels can only be 1 or 2 channels so we have a limited number of 
				// upmix situations.
				FMemory::Memset(OutSamples.GetData(), 0, sizeof(float) * OutNum);
				if (1 == InNumChannels)
				{
					// Upmix a mono signal
					float* OutSampleData = OutSamples.GetData();
					const float* InSampleData = InSamples.GetData();
					int32 OutPos = 0;
					for (int32 i = 0; i < InNumFrames; i++, OutPos += OutNumChannels)
					{
						// Scale to keep loudnes consistent.
						float value = InSampleData[i] * 0.5f;	
						OutSampleData[OutPos] = value;
						OutSampleData[OutPos + 1] = value;
					}
				}
				else if (2 == InNumChannels)
				{
					// Upmix a stereo signal by copying:
					// 		FrontLeft  -> FrontLeft
					//		FrontRight -> FrontRight
					float* OutSampleData = OutSamples.GetData();
					const float* InSampleData = InSamples.GetData();
					int32 OutPos = 0;
					for (int32 i = 0; i < InNum; i += InNumChannels, OutPos += OutNumChannels)
					{
						OutSampleData[OutPos] = InSampleData[i];
						OutSampleData[OutPos + 1] = InSampleData[i + 1];
					}
				}
			}
		}
	}

	// Copy reverberated samples to interleaved output samples. Map channels according to internal settings.
	// InFrontLeftSamples and InFrontRightSamples may be modified in-place.
	void FPlateReverbFast::InterleaveAndMixOutput(const AlignedFloatBuffer& InFrontLeftSamples, const AlignedFloatBuffer& InFrontRightSamples, AlignedFloatBuffer& OutSamples, const int32 OutNumChannels)
	{
		check(InFrontLeftSamples.Num() == InFrontRightSamples.Num())

		const int32 InNumFrames = InFrontLeftSamples.Num();
		const int32 OutNum = OutNumChannels * InNumFrames;

		// Resize output buffer
		OutSamples.Reset(OutNum);
		OutSamples.AddUninitialized(OutNum);
		FMemory::Memset(OutSamples.GetData(), 0, sizeof(float) * OutNum);

		// Interleave / mix reverb audio into output buffer
		if (2 == OutNumChannels)
		{
			// Stereo interleaved output
			BufferInterleave2ChannelFast(FrontLeftReverbSamples, FrontRightReverbSamples, OutSamples);
		}
		else
		{
			if ((OutNumChannels < 5) || (FPlateReverbFastSettings::EQuadBehavior::StereoOnly == Settings.QuadBehavior))
			{
				// We do not handle any quad reverb mapping when OutNumChannels is less than 5

				float* LeftSampleData = FrontLeftReverbSamples.GetData();
				float* RightSampleData = FrontRightReverbSamples.GetData();
				float* OutSampleData = OutSamples.GetData();
				
				int32 OutPos = 0;
				for (int32 i = 0; i < InNumFrames; i++, OutPos += OutNumChannels)
				{
					OutSampleData[OutPos + EAudioMixerChannel::FrontLeft] = LeftSampleData[i];
					OutSampleData[OutPos + EAudioMixerChannel::FrontRight] = RightSampleData[i];
				}
			}
			else
			{
				// There are 5 or more output channels and quad mapping is enabled.

				LeftAttenuatedSamples.Reset(InNumFrames);
				RightAttenuatedSamples.Reset(InNumFrames);

				LeftAttenuatedSamples.AddUninitialized(InNumFrames);
				RightAttenuatedSamples.AddUninitialized(InNumFrames);

				// Reduce volume of output reverbs.
				BufferMultiplyByConstant(FrontLeftReverbSamples, 0.5f, LeftAttenuatedSamples);
				BufferMultiplyByConstant(FrontRightReverbSamples, 0.5f, RightAttenuatedSamples);

				const float* FrontLeftSampleData = LeftAttenuatedSamples.GetData();
				const float* FrontRightSampleData = RightAttenuatedSamples.GetData();
				// WARNING: this pointer will alias other pointers in this scope. Be conscious of RESTRICT keyword in any called functions.
				const float* BackLeftSampleData = nullptr;
				// WARNING: this pointer will alias other pointers in this scope. Be conscious of RESTRICT keyword in any called functions.
				const float* BackRightSampleData = nullptr;

				// Map quads by asigning pointers.
				switch (Settings.QuadBehavior)
				{
					case FPlateReverbFastSettings::EQuadBehavior::QuadFlipped:
						// Left and right are flipped.
						BackLeftSampleData = FrontRightSampleData;
						BackRightSampleData = FrontLeftSampleData;

					case FPlateReverbFastSettings::EQuadBehavior::QuadMatched:
					default:
						// Left and right are matched.
						BackLeftSampleData = FrontLeftSampleData;
						BackRightSampleData = FrontRightSampleData;
				}

				// Interleave to output
				float* OutSampleData = OutSamples.GetData();
				int32 OutPos = 0;
				for (int32 i = 0; i < InNumFrames; i++, OutPos += OutNumChannels)
				{
					OutSampleData[OutPos + EAudioMixerChannel::FrontLeft] = FrontLeftSampleData[i];
					OutSampleData[OutPos + EAudioMixerChannel::FrontRight] = FrontRightSampleData[i];
					OutSampleData[OutPos + EAudioMixerChannel::BackLeft] = BackLeftSampleData[i];
					OutSampleData[OutPos + EAudioMixerChannel::BackRight] = BackRightSampleData[i];
				}
			}
		}
	}

	void FPlateReverbFast::ApplySettings()
	{
		EarlyReflections.SetSettings(Settings.EarlyReflections);
		LateReflections.SetSettings(Settings.LateReflections);
	}
}

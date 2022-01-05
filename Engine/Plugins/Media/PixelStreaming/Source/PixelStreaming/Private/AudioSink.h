// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WebRTCIncludes.h"

#include "Templates/Function.h"
#include "Containers/Array.h"
#include "IMediaAudioSample.h"
#include "MediaObjectPool.h"
#include "Misc/AssertionMacros.h"
#include "HAL/PlatformTime.h"

using FAudioSampleRef = TSharedRef<IMediaAudioSample, ESPMode::ThreadSafe>;

class FAudioSample :
	public IMediaAudioSample,
	public IMediaPoolable
{
public:
	void Init(const void* AudioData, uint32 BitsPerSample, uint32 InSampleRate, uint32 InNumChannels, uint32 InNumFrames)
	{
		check(BitsPerSample == 16); // `EMediaAudioSampleFormat::Int16` is hardcoded

		NumChannels = InNumChannels;
		SampleRate = InSampleRate;
		NumFrames = InNumFrames;
		Timestamp = FTimespan::FromSeconds(FPlatformTime::Seconds());
		Buffer = TArray<uint8>(reinterpret_cast<const uint8*>(AudioData), NumChannels * sizeof(int16) * InNumFrames);

		UE_LOG(PixelPlayer, VeryVerbose, TEXT("Audio: %dc, %dHz, %d samples"), NumChannels, SampleRate, NumFrames);
	}

	const void* GetBuffer() override
	{
		return Buffer.GetData();
	}

	uint32 GetChannels() const override
	{
		return NumChannels;
	}

	FTimespan GetDuration() const override
	{
		return FTimespan::FromSeconds(static_cast<double>(NumFrames) / SampleRate);
	}

	EMediaAudioSampleFormat GetFormat() const override
	{
		return EMediaAudioSampleFormat::Int16;
	}

	uint32 GetFrames() const override
	{
		return NumFrames;
	}

	uint32 GetSampleRate() const override
	{
		return SampleRate;
	}

	FMediaTimeStamp GetTime() const override
	{
		return Timestamp;
	}

private:
	uint32 NumChannels;
	uint32 SampleRate;
	uint32 NumFrames;
	FMediaTimeStamp Timestamp;
	TArray<uint8> Buffer;
};

using FAudioSamplePool = TMediaObjectPool<FAudioSample>;

class FAudioSink : public webrtc::AudioTrackSinkInterface
{
public:
	using FDelegate = TUniqueFunction<void(const FAudioSampleRef&)>;

	explicit FAudioSink(FDelegate&& InDelegate): Delegate(MoveTemp(InDelegate))
	{}

private:
	void OnData(const void* AudioData, int32 BitsPerSample, int32 SampleRate, SIZE_T NumberOfChannels, SIZE_T NumberOfFrames) override
	{
		TSharedRef<FAudioSample, ESPMode::ThreadSafe> Sample = SamplePool.AcquireShared();
		Sample->Init(AudioData, BitsPerSample, SampleRate, NumberOfChannels, NumberOfFrames);
		Delegate(Sample);
	}

private:
	FDelegate Delegate;
	FAudioSamplePool SamplePool;
};

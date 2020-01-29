// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IMediaSamples.h"
#include "IMediaTextureSample.h"
#include "Misc/ScopeLock.h"
#include "Templates/SharedPointer.h"

class IMediaAudioSample;
class IMediaBinarySample;
class IMediaOverlaySample;
class IMediaTextureSample;

/**
 * General purpose media sample queue.
 */
class MEDIAIOCORE_API FMediaIOCoreSamples
	: public IMediaSamples
{
public:
	FMediaIOCoreSamples() = default;
	FMediaIOCoreSamples(const FMediaIOCoreSamples&) = delete;
	FMediaIOCoreSamples& operator=(const FMediaIOCoreSamples&) = delete;

public:

	/**
	 * Add the given audio sample to the cache.
	 *
	 * @param Sample The sample to add.
	 * @return True if the operation succeeds.
	 * @see AddCaption, AddMetadata, AddSubtitle, AddVideo, PopAudio, NumAudioSamples
	 */
	bool AddAudio(const TSharedRef<IMediaAudioSample, ESPMode::ThreadSafe>& Sample)
	{
		FScopeLock Lock(&AudioCriticalSection);
		AudioSamples.EmplaceAt(0, Sample);
		return true;
	}

	/**
	 * Add the given caption sample to the cache.
	 *
	 * @param Sample The sample to add.
	 * @return True if the operation succeeds.
	 * @see AddAudio, AddMetadata, AddSubtitle, AddVideo, PopCaption, NumCaptionSamples
	 */
	bool AddCaption(const TSharedRef<IMediaOverlaySample, ESPMode::ThreadSafe>& Sample)
	{
		FScopeLock Lock(&CaptionCriticalSection);
		CaptionSamples.EmplaceAt(0, Sample);
		return true;
	}

	/**
	 * Add the given metadata sample to the cache.
	 *
	 * @param Sample The sample to add.
	 * @return True if the operation succeeds.
	 * @see AddAudio, AddCaption, AddSubtitle, AddVideo, PopMetadata, NumMetadataSamples
	 */
	bool AddMetadata(const TSharedRef<IMediaBinarySample, ESPMode::ThreadSafe>& Sample)
	{
		FScopeLock Lock(&MetadataCriticalSection);
		MetadataSamples.EmplaceAt(0, Sample);
		return true;
	}

	/**
	 * Add the given subtitle sample to the cache.
	 *
	 * @param Sample The sample to add.
	 * @return True if the operation succeeds.
	 * @see AddAudio, AddCaption, AddMetadata, AddVideo, PopSubtitle, NumSubtitleSamples
	 */
	bool AddSubtitle(const TSharedRef<IMediaOverlaySample, ESPMode::ThreadSafe>& Sample)
	{
		FScopeLock Lock(&SubtitleCriticalSection);
		SubtitleSamples.EmplaceAt(0, Sample);
		return true;
	}

	/**
	 * Add the given video sample to the cache.
	 *
	 * @param Sample The sample to add.
	 * @return True if the operation succeeds.
	 * @see AddAudio, AddCaption, AddMetadata, AddSubtitle, PopVideo, NumVideoSamples
	 */
	bool AddVideo(const TSharedRef<IMediaTextureSample, ESPMode::ThreadSafe>& Sample)
	{
		FScopeLock Lock(&VideoCriticalSection);
		VideoSamples.EmplaceAt(0, Sample);
		return true;
	}

	/**
	 * Pop a Audio sample from the cache.
	 *
	 * @return True if the operation succeeds.
	 * @see AddAudio, NumAudioSamples
	 */
	bool PopAudio()
	{
		FScopeLock Lock(&AudioCriticalSection);
		const int32 SampleCount = AudioSamples.Num();
		if (SampleCount > 0)
		{
			AudioSamples.RemoveAt(SampleCount - 1);
			return true;
		}
		return false;
	}

	/**
	 * Pop a Caption sample from the cache.
	 *
	 * @return True if the operation succeeds.
	 * @see AddCaption, NumCaption
	 */
	bool PopCaption()
	{
		FScopeLock Lock(&CaptionCriticalSection);
		const int32 SampleCount = CaptionSamples.Num();
		if (SampleCount > 0)
		{
			CaptionSamples.RemoveAt(SampleCount - 1);
			return true;
		}
		return false;
	}

	/**
	 * Pop a Metadata sample from the cache.
	 *
	 * @return True if the operation succeeds.
	 * @see AddMetadata, NumMetadata
	 */
	bool PopMetadata()
	{
		FScopeLock Lock(&MetadataCriticalSection);
		const int32 SampleCount = MetadataSamples.Num();
		if (SampleCount > 0)
		{
			MetadataSamples.RemoveAt(SampleCount - 1);
			return true;
		}
		return false;
	}

	/**
	 * Pop a Subtitle sample from the cache.
	 *
	 * @return True if the operation succeeds.
	 * @see AddSubtitle, NumSubtitle
	 */
	bool PopSubtitle()
	{
		FScopeLock Lock(&SubtitleCriticalSection);
		const int32 SampleCount = SubtitleSamples.Num();
		if (SampleCount > 0)
		{
			SubtitleSamples.RemoveAt(SampleCount - 1);
			return true;
		}
		return false;
	}

	/**
	 * Pop a video sample from the cache.
	 *
	 * @return True if the operation succeeds.
	 * @see AddVideo, NumVideo
	 */
	bool PopVideo()
	{
		FScopeLock Lock(&VideoCriticalSection);
		const int32 SampleCount = VideoSamples.Num();
		if (SampleCount > 0)
		{
			VideoSamples.RemoveAt(SampleCount - 1);
			return true;
		}
		return false;
	}

	/**
	 * Get the number of queued audio samples.
	 *
	 * @return Number of samples.
	 * @see AddAudio, PopAudio
	 */
	int32 NumAudioSamples() const
	{
		FScopeLock Lock(&AudioCriticalSection);
		return AudioSamples.Num();
	}

	/**
	 * Get the number of queued caption samples.
	 *
	 * @return Number of samples.
	 * @see AddCaption, PopCaption
	 */
	int32 NumCaptionSamples() const
	{
		FScopeLock Lock(&CaptionCriticalSection);
		return CaptionSamples.Num();
	}

	/**
	 * Get the number of queued metadata samples.
	 *
	 * @return Number of samples.
	 * @see AddMetadata, PopMetada
	 */
	int32 NumMetadataSamples() const
	{
		FScopeLock Lock(&MetadataCriticalSection);
		return MetadataSamples.Num();
	}

	/**
	 * Get the number of queued subtitle samples.
	 *
	 * @return Number of samples.
	 * @see AddSubtitle, PopSubtitle
	 */
	int32 NumSubtitleSamples() const
	{
		FScopeLock Lock(&SubtitleCriticalSection);
		return SubtitleSamples.Num();
	}

	/**
	 * Get the number of queued video samples.
	 *
	 * @return Number of samples.
	 * @see AddVideo, PopVideo
	 */
	int32 NumVideoSamples() const
	{
		FScopeLock Lock(&VideoCriticalSection);
		return VideoSamples.Num();
	}

	/**
	 * Get next sample time from the VideoSampleQueue.
	 *
	 * @return Time of the next sample from the VideoSampleQueue
	 * @see AddVideo, NumVideoSamples
	 */
	FTimespan GetNextVideoSampleTime()
	{
		FScopeLock Lock(&VideoCriticalSection);

		const int32 SampleCount = VideoSamples.Num();
		if (SampleCount > 0)
		{
			TSharedPtr<IMediaTextureSample, ESPMode::ThreadSafe> Sample = VideoSamples[SampleCount - 1];
			if (Sample.IsValid())
			{
				return Sample->GetTime();
			}
		}

		return FTimespan();
	}

public:

	//~ IMediaSamples interface

	virtual bool FetchAudio(TRange<FTimespan> TimeRange, TSharedPtr<IMediaAudioSample, ESPMode::ThreadSafe>& OutSample) override;
	virtual bool FetchCaption(TRange<FTimespan> TimeRange, TSharedPtr<IMediaOverlaySample, ESPMode::ThreadSafe>& OutSample) override;
	virtual bool FetchMetadata(TRange<FTimespan> TimeRange, TSharedPtr<IMediaBinarySample, ESPMode::ThreadSafe>& OutSample) override;
	virtual bool FetchSubtitle(TRange<FTimespan> TimeRange, TSharedPtr<IMediaOverlaySample, ESPMode::ThreadSafe>& OutSample) override;
	virtual bool FetchVideo(TRange<FTimespan> TimeRange, TSharedPtr<IMediaTextureSample, ESPMode::ThreadSafe>& OutSample) override;
	virtual void FlushSamples() override;

protected:

	/** Audio sample queue. */
	mutable FCriticalSection AudioCriticalSection;
	TArray<TSharedPtr<IMediaAudioSample, ESPMode::ThreadSafe>> AudioSamples;

	/** Caption sample queue. */
	mutable FCriticalSection CaptionCriticalSection;
	TArray<TSharedPtr<IMediaOverlaySample, ESPMode::ThreadSafe>> CaptionSamples;

	/** Metadata sample queue. */
	mutable FCriticalSection MetadataCriticalSection;
	TArray<TSharedPtr<IMediaBinarySample, ESPMode::ThreadSafe>> MetadataSamples;

	/** Subtitle sample queue. */
	mutable FCriticalSection SubtitleCriticalSection;
	TArray<TSharedPtr<IMediaOverlaySample, ESPMode::ThreadSafe>> SubtitleSamples;

	/** Video sample queue. */
	mutable FCriticalSection VideoCriticalSection;
	TArray<TSharedPtr<IMediaTextureSample, ESPMode::ThreadSafe>> VideoSamples;
};

// Copyright Epic Games, Inc. All Rights Reserved.

#include "MediaIOCoreSamples.h"

#include "IMediaAudioSample.h"
#include "IMediaBinarySample.h"
#include "IMediaOverlaySample.h"
#include "IMediaTextureSample.h"


/* IMediaSamples interface
*****************************************************************************/

bool FMediaIOCoreSamples::FetchAudio(TRange<FTimespan> TimeRange, TSharedPtr<IMediaAudioSample, ESPMode::ThreadSafe>& OutSample)
{
	FScopeLock Lock(&AudioCriticalSection);

	const int32 SampleCount = AudioSamples.Num();
	if (SampleCount > 0)
	{
		TSharedPtr<IMediaAudioSample, ESPMode::ThreadSafe> Sample = AudioSamples[SampleCount - 1];
		if (Sample.IsValid())
		{
			const FTimespan SampleTime = Sample->GetTime();

			if (TimeRange.Overlaps(TRange<FTimespan>(SampleTime, SampleTime + Sample->GetDuration())))
			{
				AudioSamples.RemoveAt(SampleCount - 1);
				OutSample = Sample;
				return true;
			}
		}
	}

	return false;
}


bool FMediaIOCoreSamples::FetchCaption(TRange<FTimespan> TimeRange, TSharedPtr<IMediaOverlaySample, ESPMode::ThreadSafe>& OutSample)
{
	FScopeLock Lock(&CaptionCriticalSection);

	const int32 SampleCount = CaptionSamples.Num();
	if (SampleCount > 0)
	{
		TSharedPtr<IMediaOverlaySample, ESPMode::ThreadSafe> Sample = CaptionSamples[SampleCount - 1];
		if (Sample.IsValid())
		{
			const FTimespan SampleTime = Sample->GetTime();

			if (TimeRange.Overlaps(TRange<FTimespan>(SampleTime, SampleTime + Sample->GetDuration())))
			{
				CaptionSamples.RemoveAt(SampleCount - 1);
				OutSample = Sample;
				return true;
			}
		}
	}

	return false;
}


bool FMediaIOCoreSamples::FetchMetadata(TRange<FTimespan> TimeRange, TSharedPtr<IMediaBinarySample, ESPMode::ThreadSafe>& OutSample)
{
	FScopeLock Lock(&MetadataCriticalSection);

	const int32 SampleCount = MetadataSamples.Num();
	if (SampleCount > 0)
	{
		TSharedPtr<IMediaBinarySample, ESPMode::ThreadSafe> Sample = MetadataSamples[SampleCount - 1];
		if (Sample.IsValid())
		{
			const FTimespan SampleTime = Sample->GetTime();

			if (TimeRange.Overlaps(TRange<FTimespan>(SampleTime, SampleTime + Sample->GetDuration())))
			{
				MetadataSamples.RemoveAt(SampleCount - 1);
				OutSample = Sample;
				return true;
			}
		}
	}

	return false;
}


bool FMediaIOCoreSamples::FetchSubtitle(TRange<FTimespan> TimeRange, TSharedPtr<IMediaOverlaySample, ESPMode::ThreadSafe>& OutSample)
{
	FScopeLock Lock(&SubtitleCriticalSection);

	const int32 SampleCount = SubtitleSamples.Num();
	if (SampleCount > 0)
	{
		TSharedPtr<IMediaOverlaySample, ESPMode::ThreadSafe> Sample = SubtitleSamples[SampleCount - 1];
		if (Sample.IsValid())
		{
			const FTimespan SampleTime = Sample->GetTime();

			if (TimeRange.Overlaps(TRange<FTimespan>(SampleTime, SampleTime + Sample->GetDuration())))
			{
				SubtitleSamples.RemoveAt(SampleCount - 1);
				OutSample = Sample;
				return true;
			}
		}
	}

	return false;
}


bool FMediaIOCoreSamples::FetchVideo(TRange<FTimespan> TimeRange, TSharedPtr<IMediaTextureSample, ESPMode::ThreadSafe>& OutSample)
{
	FScopeLock Lock(&VideoCriticalSection);

	const int32 SampleCount = VideoSamples.Num();
	if (SampleCount > 0)
	{
		TSharedPtr<IMediaTextureSample, ESPMode::ThreadSafe> Sample = VideoSamples[SampleCount - 1];
		if (Sample.IsValid())
		{
			const FTimespan SampleTime = Sample->GetTime();

			if (TimeRange.Overlaps(TRange<FTimespan>(SampleTime, SampleTime + Sample->GetDuration())))
			{
				VideoSamples.RemoveAt(SampleCount - 1);
				OutSample = Sample;
				return true;
			}
		}
	}

	return false;
}


void FMediaIOCoreSamples::FlushSamples()
{
	{
		FScopeLock Lock(&AudioCriticalSection); 
		AudioSamples.Empty();
	}

	{
		FScopeLock Lock(&CaptionCriticalSection);
		CaptionSamples.Empty();
	}

	{
		FScopeLock Lock(&MetadataCriticalSection);
		MetadataSamples.Empty();
	}

	{
		FScopeLock Lock(&SubtitleCriticalSection);
		SubtitleSamples.Empty();
	}

	{
		FScopeLock Lock(&VideoCriticalSection);
		VideoSamples.Empty();
	}
}

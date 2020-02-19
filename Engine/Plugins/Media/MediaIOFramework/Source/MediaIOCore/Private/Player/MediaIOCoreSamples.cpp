// Copyright Epic Games, Inc. All Rights Reserved.

#include "MediaIOCoreSamples.h"

#include "IMediaAudioSample.h"
#include "IMediaBinarySample.h"
#include "IMediaOverlaySample.h"
#include "IMediaTextureSample.h"


FMediaIOCoreSamples::FMediaIOCoreSamples(ITimedDataInput* ChannelsOwner)
	: VideoSamples(ChannelsOwner, "Video")
	, AudioSamples(ChannelsOwner, "Audio")
	, MetadataSamples(ChannelsOwner, "Metadata")
	, SubtitleSamples(ChannelsOwner, "Subtitles")
	, CaptionSamples(ChannelsOwner, "Caption")
{

}

void FMediaIOCoreSamples::CacheSamplesState(FTimespan PlayerTime)
{
	VideoSamples.CacheState(PlayerTime);
	AudioSamples.CacheState(PlayerTime);
	MetadataSamples.CacheState(PlayerTime);
	SubtitleSamples.CacheState(PlayerTime);
	CaptionSamples.CacheState(PlayerTime);
}

void FMediaIOCoreSamples::EnableTimedDataChannels(EMediaIOSampleType SampleTypes)
{
	VideoSamples.EnableChannel(static_cast<uint8>(SampleTypes) & static_cast<uint8>(EMediaIOSampleType::Video));
	AudioSamples.EnableChannel(static_cast<uint8>(SampleTypes)& static_cast<uint8>(EMediaIOSampleType::Audio));
	MetadataSamples.EnableChannel(static_cast<uint8>(SampleTypes)& static_cast<uint8>(EMediaIOSampleType::Metadata));
	SubtitleSamples.EnableChannel(static_cast<uint8>(SampleTypes)& static_cast<uint8>(EMediaIOSampleType::Subtitles));
	CaptionSamples.EnableChannel(static_cast<uint8>(SampleTypes)& static_cast<uint8>(EMediaIOSampleType::Caption));
}

void FMediaIOCoreSamples::InitializeVideoBuffer(const FMediaIOSamplingSettings& InSettings)
{
	VideoSamples.UpdateSettings(InSettings);
}

void FMediaIOCoreSamples::InitializeAudioBuffer(const FMediaIOSamplingSettings& InSettings)
{
	AudioSamples.UpdateSettings(InSettings);
}

void FMediaIOCoreSamples::InitializeMetadataBuffer(const FMediaIOSamplingSettings& InSettings)
{
	MetadataSamples.UpdateSettings(InSettings);
}

void FMediaIOCoreSamples::InitializeSubtitlesBuffer(const FMediaIOSamplingSettings& InSettings)
{
	SubtitleSamples.UpdateSettings(InSettings);
}

void FMediaIOCoreSamples::InitializeCaptionBuffer(const FMediaIOSamplingSettings& InSettings)
{
	CaptionSamples.UpdateSettings(InSettings);
}


/* IMediaSamples interface
*****************************************************************************/

bool FMediaIOCoreSamples::FetchAudio(TRange<FTimespan> TimeRange, TSharedPtr<IMediaAudioSample, ESPMode::ThreadSafe>& OutSample)
{
	return AudioSamples.FetchSample(TimeRange, OutSample);
}


bool FMediaIOCoreSamples::FetchCaption(TRange<FTimespan> TimeRange, TSharedPtr<IMediaOverlaySample, ESPMode::ThreadSafe>& OutSample)
{
	return CaptionSamples.FetchSample(TimeRange, OutSample);
}


bool FMediaIOCoreSamples::FetchMetadata(TRange<FTimespan> TimeRange, TSharedPtr<IMediaBinarySample, ESPMode::ThreadSafe>& OutSample)
{
	return MetadataSamples.FetchSample(TimeRange, OutSample);
}


bool FMediaIOCoreSamples::FetchSubtitle(TRange<FTimespan> TimeRange, TSharedPtr<IMediaOverlaySample, ESPMode::ThreadSafe>& OutSample)
{
	return SubtitleSamples.FetchSample(TimeRange, OutSample);
}


bool FMediaIOCoreSamples::FetchVideo(TRange<FTimespan> TimeRange, TSharedPtr<IMediaTextureSample, ESPMode::ThreadSafe>& OutSample)
{
	return VideoSamples.FetchSample(TimeRange, OutSample);
}


void FMediaIOCoreSamples::FlushSamples()
{
	AudioSamples.FlushSamples();
	CaptionSamples.FlushSamples();
	MetadataSamples.FlushSamples();
	SubtitleSamples.FlushSamples();
	VideoSamples.FlushSamples();
}

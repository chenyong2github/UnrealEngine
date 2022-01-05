// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreamingAudioSink.h"

FPixelStreamingAudioSink::~FPixelStreamingAudioSink()
{
    for (auto Iter = this->AudioConsumers.CreateIterator(); Iter; ++Iter)
    {
        IPixelStreamingAudioConsumer* AudioConsumer = Iter.ElementIt->Value;
        Iter.RemoveCurrent();
        if(AudioConsumer != nullptr)
        {
            AudioConsumer->OnConsumerRemoved();
        }
    }
}

void FPixelStreamingAudioSink::OnData(const void* audio_data, int bits_per_sample, int sample_rate, size_t number_of_channels, size_t number_of_frames, absl::optional<int64_t> absolute_capture_timestamp_ms)
{

    // This data is populated from the internals of WebRTC, basically each audio track sent from the browser has its RTP audio source received and decoded.
    // The sample rate and number of channels here has absolutely no relationship with PixelStreamingAudioDeviceModule.
    // The sample rate and number of channels here is determined adaptively by WebRTC's NetEQ class that selects sample rate/number of channels
    // based on network conditions and other factors.

    if(!this->HasAudioConsumers())
    {
        return;
    }

    // Iterate audio consumers and pass this data to their buffers
    for(IPixelStreamingAudioConsumer* AudioConsumer : this->AudioConsumers)
    {
        AudioConsumer->ConsumeRawPCM(static_cast<const int16_t*>(audio_data), sample_rate, number_of_channels, number_of_frames);
    }

    return;
}

void FPixelStreamingAudioSink::AddAudioConsumer(IPixelStreamingAudioConsumer* AudioConsumer)
{
    bool bAlreadyInSet = false;
    this->AudioConsumers.Add(AudioConsumer, &bAlreadyInSet);
    if(!bAlreadyInSet)
    {
        AudioConsumer->OnConsumerAdded();
    }
}

void FPixelStreamingAudioSink::RemoveAudioConsumer(IPixelStreamingAudioConsumer* AudioConsumer)
{
    if(this->AudioConsumers.Contains(AudioConsumer))
    {
        this->AudioConsumers.Remove(AudioConsumer);
        AudioConsumer->OnConsumerRemoved();
    }
}

bool FPixelStreamingAudioSink::HasAudioConsumers()
{
    return this->AudioConsumers.Num() > 0;
}
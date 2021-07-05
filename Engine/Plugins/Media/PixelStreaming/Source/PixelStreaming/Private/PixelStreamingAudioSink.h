// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "WebRTCIncludes.h"
#include "IPixelStreamingAudioConsumer.h"
#include "Containers/Set.h"


// This sink collects audio coming in from the browser and passes into into UE's audio system.
class FPixelStreamingAudioSink : public webrtc::AudioTrackSinkInterface
{
public:

    FPixelStreamingAudioSink() 
    :   AudioConsumers(){};

    ~FPixelStreamingAudioSink();

	// Begin AudioTrackSinkInterface 
    void OnData(const void* audio_data, int bits_per_sample, int sample_rate, size_t number_of_channels, size_t number_of_frames, absl::optional<int64_t> absolute_capture_timestamp_ms) override;
    // End AudioTrackSinkInterface

    // Audio consumers are added to consume the audio buffers from this sink.
    void AddAudioConsumer(IPixelStreamingAudioConsumer* AudioConsumer);

    // Audio consumers can be removed usubg this method. This sink will not remove them itself until destructor is called.
    void RemoveAudioConsumer(IPixelStreamingAudioConsumer* AudioConsumer);

    bool HasAudioConsumers();

private:
    void UpdateAudioSettings(int InNumChannels, int InSampleRate);

private:
	TSet<IPixelStreamingAudioConsumer*> AudioConsumers;
    
};
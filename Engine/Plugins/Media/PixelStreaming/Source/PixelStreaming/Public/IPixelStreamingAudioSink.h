// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

class IPixelStreamingAudioConsumer;

// Interface for a sink that collects audio coming in from the browser and passes into into UE's audio system.
class IPixelStreamingAudioSink
{
    public:
        IPixelStreamingAudioSink(){}
        virtual ~IPixelStreamingAudioSink(){}
        virtual void AddAudioConsumer(IPixelStreamingAudioConsumer* AudioConsumer) = 0;
        virtual void RemoveAudioConsumer(IPixelStreamingAudioConsumer* AudioConsumer) = 0;
        virtual bool HasAudioConsumers() = 0;
};
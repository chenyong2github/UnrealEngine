// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PixelStreamingWebRTCIncludes.h"

class IPixelStreamingAudioPlayoutRequester
{
public:
	virtual ~IPixelStreamingAudioPlayoutRequester() = default;

	virtual void InitPlayout() = 0;
	virtual void StartPlayout() = 0;
	virtual void StopPlayout() = 0;
	virtual bool Playing() const = 0;
	virtual bool PlayoutIsInitialized() const = 0;
	virtual void Uninitialise() = 0;
	virtual void RegisterAudioCallback(webrtc::AudioTransport* AudioCallback) = 0;
};

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PixelStreamingWebRTCIncludes.h"

class IPixelStreamingAudioSubmixCapturer
{
public:
	virtual ~IPixelStreamingAudioSubmixCapturer() = default;

	virtual bool Init() = 0;
	virtual bool IsInitialised() const = 0;
	virtual bool IsCapturing() const = 0;
	virtual void Uninitialise() = 0;
	virtual bool StartCapturing() = 0;
	virtual bool EndCapturing() = 0;
	virtual uint32_t GetVolume() const = 0;
	virtual void SetVolume(uint32_t NewVolume) = 0;
	virtual void RegisterAudioCallback(webrtc::AudioTransport* AudioCb) = 0;
};

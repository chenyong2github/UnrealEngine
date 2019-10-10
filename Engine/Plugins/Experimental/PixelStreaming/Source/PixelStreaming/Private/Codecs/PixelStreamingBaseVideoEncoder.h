// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PixelStreamingPrivate.h"

class FVideoEncoder;
using FBufferId = uint32;

DECLARE_LOG_CATEGORY_EXTERN(LogVideoEncoder, Log, VeryVerbose);

// #AMF : Put the other common cvars here
extern TAutoConsoleVariable<float> CVarEncoderMaxBitrate;
extern TAutoConsoleVariable<int32> CVarEncoderUseBackBufferSize;
extern TAutoConsoleVariable<FString> CVarEncoderTargetSize;
extern TAutoConsoleVariable<int> CVarEncoderMinQP;

class FPixelStreamingBaseVideoEncoder
{
public:

	explicit FPixelStreamingBaseVideoEncoder();
	virtual ~FPixelStreamingBaseVideoEncoder();

	// #AMF : This should be implemented in the base class
	virtual bool CopyBackBuffer(const FTexture2DRHIRef& BackBuffer, FTimespan Timestamp, FBufferId& BufferId) = 0;

	virtual void EncodeFrame(FBufferId BufferId, const webrtc::EncodedImage& EncodedImage, uint32 Bitrate) = 0;
	virtual void OnFrameDropped(FBufferId BufferId) = 0;
	virtual void SubscribeToFrameEncodedEvent(FVideoEncoder& Subscriber) = 0;
	virtual void UnsubscribeFromFrameEncodedEvent(FVideoEncoder& Subscriber) = 0;

private:
};



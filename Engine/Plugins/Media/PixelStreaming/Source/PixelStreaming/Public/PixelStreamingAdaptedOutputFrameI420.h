// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IPixelStreamingAdaptedOutputFrame.h"
#include "PixelStreamingWebRTCIncludes.h"

class FPixelStreamingAdaptedOutputFrameI420 : public IPixelStreamingAdaptedOutputFrame
{
public:
	FPixelStreamingAdaptedOutputFrameI420(rtc::scoped_refptr<webrtc::I420Buffer> InI420Buffer)
		: I420Buffer(InI420Buffer)
	{
	}
	virtual ~FPixelStreamingAdaptedOutputFrameI420() = default;

	virtual int32 GetWidth() const override { return I420Buffer->width(); }
	virtual int32 GetHeight() const override { return I420Buffer->height(); }

	rtc::scoped_refptr<webrtc::I420Buffer> GetI420Buffer() const { return I420Buffer; }

private:
	rtc::scoped_refptr<webrtc::I420Buffer> I420Buffer;
};

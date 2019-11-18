// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PixelStreamingPrivate.h"
#include "RHIResources.h"

class FPixelStreamingBaseVideoEncoder;

class FVideoCapturer : public cricket::VideoCapturer
{
public:
	explicit FVideoCapturer(FPixelStreamingBaseVideoEncoder& InHWEncoder);

	void OnFrameReady(const FTexture2DRHIRef& FrameBuffer);

private:
	//////////////////////////////////////////////////////////////////////////
	// cricket::VideoCapturer interface
	cricket::CaptureState Start(const cricket::VideoFormat& Format) override;

	void Stop() override
	{}

	bool IsRunning() override
	{ return true; }

	bool IsScreencast() const override
	{ return false; }

	bool GetPreferredFourccs(std::vector<unsigned int>* fourccs) override
	{
		fourccs->push_back(cricket::FOURCC_H264);
		return true;
	}
	//////////////////////////////////////////////////////////////////////////

	FPixelStreamingBaseVideoEncoder& HWEncoder;

	int32 Width = 1920;
	int32 Height = 1080;
	int32 Framerate = 60;
};

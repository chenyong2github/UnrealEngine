// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayMediaEncoderCommon.h"

#include "AmdAmfPrivate.h"
#include "BaseVideoEncoder.h"

DECLARE_STATS_GROUP(TEXT("AmdAmfVideoEncoder"), STATGROUP_AmdAmfVideoEncoder, STATCAT_Advanced);

// H.264 Encoder based on AMF SDK for AMD GPUs
class FAmdAmfVideoEncoder: public FBaseVideoEncoder
{
public:
	explicit FAmdAmfVideoEncoder(const FOutputSampleCallback& OutputCallback);
	~FAmdAmfVideoEncoder() override;

	bool Initialize(const FVideoEncoderConfig& Config) override;
	bool Start() override;
	void Stop() override;
	bool Process(const FTexture2DRHIRef& Texture, FTimespan Timestamp, FTimespan Duration) override;

	bool SetBitrate(uint32 Bitrate) override;
	bool SetFramerate(uint32 Framerate) override;

private:
	struct FFrame
	{
		FTexture2DRHIRef ResolvedBackBuffer;
		amf::AMFDataPtr EncodedData;
		uint64 FrameIdx = 0;
		FThreadSafeBool bEncoding = false;
		FTimespan Timestamp;
		FTimespan Duration;
	};

	bool ProcessInput(const FTexture2DRHIRef& Texture, FTimespan Timestamp, FTimespan Duration);
	bool SubmitFrameToEncoder(FFrame& Frame);
	bool ProcessOutput();
	bool HandleEncodedFrame(FFrame& Frame);
	void ResetResolvedBackBuffer(FFrame& Frame);
	void ResolveBackBuffer(const FTexture2DRHIRef& BackBuffer, const FTexture2DRHIRef& ResolvedBackBuffer);

	bool bInitialized = false;
	amf_handle DllHandle = nullptr;
	amf::AMFFactory* AmfFactory = nullptr;
	amf::AMFContextPtr AmfContext;
	amf::AMFComponentPtr AmfEncoder;
	uint64 InputFrameCount = 0;
	uint64 OutputFrameCount = 0;
	static const uint32 NumBufferedFrames = 3;
	FFrame BufferedFrames[NumBufferedFrames];
};



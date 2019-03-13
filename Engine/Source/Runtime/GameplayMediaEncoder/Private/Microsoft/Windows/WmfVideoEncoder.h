// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BaseVideoEncoder.h"
#include "Containers/Queue.h"

DECLARE_STATS_GROUP(TEXT("WmfVideoEncoder"), STATGROUP_WmfVideoEncoder, STATCAT_Advanced);

class FWmfVideoEncoder : public FBaseVideoEncoder
{
public:
	explicit FWmfVideoEncoder(const FOutputSampleCallback& OutputCallback);

	bool Initialize(const FVideoEncoderConfig& Config) override;
	bool Start() override;
	void Stop() override;
	bool Process(const FTexture2DRHIRef& Texture, FTimespan Timestamp, FTimespan Duration) override;
	bool Flush();

	bool SetBitrate(uint32 Bitrate) override;
	bool SetFramerate(uint32 Framerate) override;

private:
	bool InitializeVideoProcessor();
	bool SetVideoProcessorInputMediaType();
	bool SetVideoProcessorOutputMediaType();
	bool ProcessVideoProcessorInputFrame();
	bool ProcessVideoProcessorOutputFrame();
	bool CreateInputSample(FGameplayMediaEncoderSample& OutSample);

	bool InitializeEncoder();
	bool SetEncoderInputMediaType();
	bool SetEncoderOutputMediaType();
	bool RetrieveStreamInfo();
	bool CheckEncoderStatus();
	bool StartStreaming();
	bool EnqueueInputFrame(const FTexture2DRHIRef& Texture, FTimespan Timestamp, FTimespan Duration);
	bool CreateOutputSample(FGameplayMediaEncoderSample& OutSample);
	bool ProcessEncoderInputFrame();
	bool ProcessEncoderOutputFrame();
	void ResolveBackBuffer(const FTexture2DRHIRef& BackBuffer, const FTexture2DRHIRef& ResolvedBackBuffer);

	TRefCountPtr<IMFTransform> VideoProcessor;
	TRefCountPtr<IMFTransform> H264Encoder;
	TQueue<FGameplayMediaEncoderSample, EQueueMode::Spsc> InputQueue;
	FThreadSafeCounter InputQueueSize = 0;
	int32 InputFrameProcessedCount = 0;
	TQueue<FGameplayMediaEncoderSample, EQueueMode::Spsc> EncoderInputQueue;
	FThreadSafeCounter EncoderInputQueueSize = 0;
	int32 EncoderInputProcessedCount = 0;
	TArray<uint8> EncodedFrame;
	FThreadSafeCounter EncodedFrameCount = 0;
	MFT_OUTPUT_STREAM_INFO OutputStreamInfo = {};
};


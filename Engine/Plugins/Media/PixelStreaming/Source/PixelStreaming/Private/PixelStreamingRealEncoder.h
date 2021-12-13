// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "VideoEncoder.h"
#include "PixelStreamingEncoderFrameFactory.h"

class FPixelStreamingVideoEncoderFactory;

class FPixelStreamingRealEncoder
{
public:
	FPixelStreamingRealEncoder(uint64 EncoderId, int Width, int Height, int MaxBitrate, int TargetBitrate, int MaxFramerate, FPixelStreamingVideoEncoderFactory* InFactory);
	~FPixelStreamingRealEncoder();

	uint64 GetId() const { return Id; }

	void SetForceNextKeyframe() { ForceNextKeyframe = true; }

	void Encode(const webrtc::VideoFrame& WebRTCFrame, bool Keyframe);

	AVEncoder::FVideoEncoder::FLayerConfig& GetCurrentConfig() { return EncoderConfig; }
	void SetConfig(const AVEncoder::FVideoEncoder::FLayerConfig& NewConfig);

private:
	uint64 Id;
	FPixelStreamingVideoEncoderFactory* Factory;
	FPixelStreamingEncoderFrameFactory FrameFactory;
	TUniquePtr<AVEncoder::FVideoEncoder> Encoder;
	AVEncoder::FVideoEncoder::FLayerConfig EncoderConfig;
	bool ForceNextKeyframe = false;
};

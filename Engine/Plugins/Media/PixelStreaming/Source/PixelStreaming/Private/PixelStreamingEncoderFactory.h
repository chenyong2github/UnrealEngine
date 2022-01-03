// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "WebRTCIncludes.h"
#include "VideoEncoder.h"
#include "Containers/Queue.h"
#include "Utils.h"
#include "PlayerId.h"
#include "IPixelStreamingSessions.h"
#include "HAL/CriticalSection.h"

class FPixelStreamingVideoEncoderFactory;
class FPixelStreamingVideoEncoder;
class FPixelStreamingRealEncoder;

class FPixelStreamingSimulcastEncoderFactory : public webrtc::VideoEncoderFactory
{
public:
	FPixelStreamingSimulcastEncoderFactory(IPixelStreamingSessions* InPixelStreamingSessions);
	virtual ~FPixelStreamingSimulcastEncoderFactory();
	virtual std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const;
	virtual CodecInfo QueryVideoEncoder(const webrtc::SdpVideoFormat& format) const override;
	virtual std::unique_ptr<webrtc::VideoEncoder> CreateVideoEncoder(const webrtc::SdpVideoFormat& format);

	FPixelStreamingVideoEncoderFactory* GetRealFactory() const;

private:
	// making this a unique ptr is a little cheeky since we pass around raw pointers to it
	// but the sole ownership lies in this class
	TUniquePtr<FPixelStreamingVideoEncoderFactory> RealFactory;
};

class FPixelStreamingVideoEncoderFactory : public webrtc::VideoEncoderFactory
{
public:
	FPixelStreamingVideoEncoderFactory(IPixelStreamingSessions* InPixelStreamingSessions);
	virtual ~FPixelStreamingVideoEncoderFactory() override;

	virtual std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override;

	// Always returns our H264 hardware encoders codec_info for now/
	virtual CodecInfo QueryVideoEncoder(const webrtc::SdpVideoFormat& format) const override;

	// Always returns our H264 hardware encoder for now.
	virtual std::unique_ptr<webrtc::VideoEncoder> CreateVideoEncoder(const webrtc::SdpVideoFormat& format) override;

	void ReleaseVideoEncoder(FPixelStreamingVideoEncoder* encoder);
	void ForceKeyFrame();

	using FHardwareEncoderId = uint64;
	FHardwareEncoderId GetOrCreateHardwareEncoder(int Width, int Height, int MaxBitrate, int TargetBitrate, int MaxFramerate);
	FPixelStreamingRealEncoder* GetHardwareEncoder(FHardwareEncoderId Id);

	void OnEncodedImage(FHardwareEncoderId SourceEncoderId, const webrtc::EncodedImage& encoded_image, const webrtc::CodecSpecificInfo* codec_specific_info, const webrtc::RTPFragmentationHeader* fragmentation);

private:
	// Used for checks such as whether a given player id is associated with the quality controlling player.
	IPixelStreamingSessions* PixelStreamingSessions;

	// These are the actual hardware encoders serving multiple pixelstreaming encoders
	TMap<FHardwareEncoderId, TUniquePtr<FPixelStreamingRealEncoder>> HardwareEncoders;
	FCriticalSection HardwareEncodersGuard;

	// Encoders assigned to each peer. Each one of these will be assigned to one of the hardware encoders
	TArray<FPixelStreamingVideoEncoder*> ActiveEncoders;
	FCriticalSection ActiveEncodersGuard;
};

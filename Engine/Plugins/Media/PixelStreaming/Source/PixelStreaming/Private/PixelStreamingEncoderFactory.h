// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "WebRTCIncludes.h"
#include "VideoEncoder.h"
#include "Containers/Queue.h"
#include "Utils.h"
#include "PlayerId.h"
#include "IPixelStreamingSessions.h"
#include "HAL/CriticalSection.h"
#include "PixelStreamingEncoderFrameFactory.h"

class FPixelStreamingVideoEncoderFactory;
class FPixelStreamingVideoEncoder;

// FEncoderContext is the wrapper around the actual video encoder - FVideoEncoder.
// You may have noticed PixelStreamingVideoEncoder - this is simply a thin bridging class to fulfil WebRTC's interface contract.
// FPixelStreamingVideoEncoder is not actually an encoder, rather it simply makes calls to an actual encoder. 
// Q. Why can't PixelStreamingVideoEncoder be an actual encoder? 
// A. WebRTC wants an encoder per peer, so it can change the bitrate for each underlying encoder based on that peer's network
// conditions. However, in Pixel Streaming we have one "controlling peer" and the other peers simply get the video
// quality of the controlling peer. For this reason we need the video encoders that WebRTC creates to all point
// to the same underlying encoder. 
// Note: We could give an encoder per peer; however, this uses up our limited hardware encoder sessions (e.g. typically you can run only 3 encoding sessions on NVIDIA Geforce cards).
struct FEncoderContext
{
	FPixelStreamingEncoderFrameFactory FrameFactory;
	FPixelStreamingVideoEncoderFactory* Factory;
	TUniquePtr<AVEncoder::FVideoEncoder> Encoder; // The real underlying encoder.
	FSmoothedValue<60> SmoothedAvgQP;
	FCriticalSection EncodingCriticalSection; // Ensures only one encoder is encoding at a time during quality controller swap.
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

	void OnPostEncode();
	void OnEncodedImage(const webrtc::EncodedImage& encoded_image, const webrtc::CodecSpecificInfo* codec_specific_info, const webrtc::RTPFragmentationHeader* fragmentation);
	void RegisterVideoEncoder(FPlayerId PlayerId, FPixelStreamingVideoEncoder* Encoder);
	void UnregisterVideoEncoder(FPlayerId PlayerId);
	void RemoveStaleEncoders();
	void ForceKeyFrame();
	double GetLatestQP();

private:
	FEncoderContext EncoderContext;

	// Each encoder is associated with a particular player (peer).
	TMap<FPlayerId, FPixelStreamingVideoEncoder*> ActiveEncoders;
	
	// Used for checks such as whether a given player id is associated with the quality controlling player.
	IPixelStreamingSessions* PixelStreamingSessions;

	// This encoder factory can be access on multiple WebRTC threads (1 for each peer) so it needs resource locking.
	FCriticalSection FactoryCS;
};
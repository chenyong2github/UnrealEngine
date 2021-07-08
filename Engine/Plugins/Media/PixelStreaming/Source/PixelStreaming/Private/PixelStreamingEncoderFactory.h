// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "WebRTCIncludes.h"
#include "VideoEncoder.h"
#include "Containers/Queue.h"
#include "Utils.h"

class FPlayerSession;
class FPixelStreamingVideoEncoderFactory;
class FPixelStreamingVideoEncoder;

// FEncoderContext is the wrapper around the actual video encoder - FVideoEncoder.
// You may have noticed PixelStreamingVideoEncoder - this is simply a thin bridging class to fulfil WebRTC's interface contract.
// FPixelStreamingVideoEncoder is not actually an encoder, rather it simply makes calls to an actual encoder. 
// Q. Why can't PixelStreamingVideoEncoder be an actual encoder? 
// A. WebRTC wants an encoder per peer, so it can change the bitrate for each underlying encoder based on that peer's network
// conditions. However, in Pixel Streaming we have one "controlling peer" and the other peers simply get the video
// quality of the controlling peer. For this reason we need the video encoders that WebRTC creates to all point
// to the same underlying encoder. We could give an encoder per peer; however, this is slow and uses up NVENC sessions.
struct FEncoderContext
{
	FPixelStreamingVideoEncoderFactory* Factory;
	TUniquePtr<AVEncoder::FVideoEncoder> Encoder;
	FSmoothedValue<60> SmoothedAvgQP;
};

class FPixelStreamingVideoEncoderFactory : public webrtc::VideoEncoderFactory
{

public:
	FPixelStreamingVideoEncoderFactory();
	virtual ~FPixelStreamingVideoEncoderFactory() override;

	/**
	* This is used from the FPlayerSession::OnSuccess to let the factory know
	* what session the next created encoder should belong to.
	* It allows us to get the right FPlayerSession <-> FVideoEncoder relationship
	*/
	void AddSession(FPlayerSession& PlayerSession);

	virtual std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override;

	// Always returns our H264 hardware encoders codec_info for now/
	virtual CodecInfo QueryVideoEncoder(const webrtc::SdpVideoFormat& format) const override;

	// Always returns our H264 hardware encoder for now.
	virtual std::unique_ptr<webrtc::VideoEncoder> CreateVideoEncoder(const webrtc::SdpVideoFormat& format) override;

	void OnEncodedImage(const webrtc::EncodedImage& encoded_image, const webrtc::CodecSpecificInfo* codec_specific_info, const webrtc::RTPFragmentationHeader* fragmentation);
	void ReleaseVideoEncoder(FPixelStreamingVideoEncoder* encoder);

private:
	FEncoderContext EncoderContext;
	TQueue<FPlayerSession*> PendingPlayerSessions;

	// Each encoder is associated with a particular player (peer).
	TArray<FPixelStreamingVideoEncoder*> ActiveEncoders;
	
};
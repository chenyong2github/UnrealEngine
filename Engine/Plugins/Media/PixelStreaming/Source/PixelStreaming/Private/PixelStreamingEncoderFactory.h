// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "WebRTCIncludes.h"
#include "Containers/Queue.h"

class FPlayerSession;
class FPixelStreamingVideoEncoderFactory;
class FPixelStreamingVideoEncoder;

struct FEncoderContext
{
	FPixelStreamingVideoEncoderFactory* Factory;
	TUniquePtr<FVideoEncoder> Encoder;
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
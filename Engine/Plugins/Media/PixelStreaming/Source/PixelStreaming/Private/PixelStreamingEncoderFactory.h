// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "WebRTCIncludes.h"


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
	* This is used from the FPlayerSession::OnSucess to let the factory know
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

/*
class FPixelStreamingVideoDecoderFactory : public webrtc::VideoDecoderFactory
{
public:
	virtual std::vector<SdpVideoFormat> GetSupportedFormats() const override { return std::vector<SdpVideoFormat>(); };
	virtual std::unique_ptr<VideoDecoder> CreateVideoDecoder(const SdpVideoFormat& format) override { return std::unique_ptr<VideoDecoder>();};
	virtual std::unique_ptr<VideoDecoder> LegacyCreateVideoDecoder(const SdpVideoFormat& format, const std::string& receive_stream_id) override {return std::unique_ptr<VideoDecoder>();};
};
*/

/*
 class FPixelStreamingAudioEncoderFactory : public webrtc::AudioEncoderFactory
 {
 public:
 	struct Config
 	{

 	};

 	virtual std::vector<AudioCodecSpec> GetSupportedEncoders() override { return std::vector<AudioCodecSpec>(); };
 	virtual absl::optional<AudioCodecInfo> QueryAudioEncoder(const SdpAudioFormat& format) override { return absl::optional<AudioCodecInfo>(); };
 	virtual std::unique_ptr<AudioEncoder> MakeAudioEncoder(int payload_type, const SdpAudioFormat& format, absl::optional<AudioCodecPairId> codec_pair_id) override { return std::unique_ptr<AudioEncoder>(); };

 	static void AppendSupportedEncoders(std::vector<AudioCodecSpec>* specs) {};
 	static Config* SdpToConfig(const SdpAudioFormat& format) { return nullptr; };
 };
 */

 /*
 class FPixelStreamingAudioDecoderFactory : public webrtc::AudioDecoderFactory
 {
 public:
 	struct Config
 	{

 	};

 	virtual std::vector<AudioCodecSpec> GetSupportedDecoders() override { return std::vector<AudioCodecSpec>(); };
 	virtual bool IsSupportedDecoder(const SdpAudioFormat& format) override { return false; };
 	virtual std::unique_ptr<AudioDecoder> MakeAudioDecoder(const SdpAudioFormat& format, absl::optional<AudioCodecPairId> codec_pair_id) override { return std::unique_ptr<AudioDecoder>(); };

 	static void AppendSupportedDecoders(std::vector<AudioCodecSpec>* specs) {};
 	static Config* SdpToConfig(const SdpAudioFormat& format) { return nullptr; };
 };
 */
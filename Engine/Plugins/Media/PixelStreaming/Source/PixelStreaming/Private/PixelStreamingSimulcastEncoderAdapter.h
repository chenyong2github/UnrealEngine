// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WebRTCIncludes.h"

class FPixelStreamingLayerFrameSource;

namespace webrtc
{
	class SimulcastRateAllocator;
	class VideoEncoderFactory;
} // namespace webrtc

// This is highly modified version of webrtc::simulcast_encoder_adapter

class PixelStreamingSimulcastEncoderAdapter : public webrtc::VideoEncoder
{
public:
	PixelStreamingSimulcastEncoderAdapter(webrtc::VideoEncoderFactory* primarty_factory, const webrtc::SdpVideoFormat& format);
	~PixelStreamingSimulcastEncoderAdapter() override;

	// Implements VideoEncoder.
	int Release() override;
	int InitEncode(const webrtc::VideoCodec* codec_settings, const webrtc::VideoEncoder::Settings& settings) override;
	int Encode(const webrtc::VideoFrame& input_image, const std::vector<webrtc::VideoFrameType>* frame_types) override;
	int RegisterEncodeCompleteCallback(webrtc::EncodedImageCallback* callback) override;

	void SetRates(const RateControlParameters& parameters) override;
	void OnPacketLossRateUpdate(float packet_loss_rate) override;
	void OnRttUpdate(int64_t rtt_ms) override;
	void OnLossNotification(const LossNotification& loss_notification) override;

	// Eventual handler for the contained encoders' EncodedImageCallbacks, but
	// called from an internal helper that also knows the correct stream
	// index.
	webrtc::EncodedImageCallback::Result OnEncodedImage(
		size_t stream_idx,
		const webrtc::EncodedImage& encoded_image,
		const webrtc::CodecSpecificInfo* codec_specific_info,
		const webrtc::RTPFragmentationHeader* fragmentation);

	EncoderInfo GetEncoderInfo() const override;

private:
	struct StreamInfo
	{
		StreamInfo(std::unique_ptr<webrtc::VideoEncoder> encoder,
			std::unique_ptr<webrtc::EncodedImageCallback> callback,
			std::unique_ptr<webrtc::FramerateController> framerate_controller,
			uint16_t width,
			uint16_t height,
			bool send_stream)
			: encoder(std::move(encoder))
			, callback(std::move(callback))
			, framerate_controller(std::move(framerate_controller))
			, width(width)
			, height(height)
			, key_frame_request(false)
			, send_stream(send_stream)
		{
		}
		std::unique_ptr<webrtc::VideoEncoder> encoder;
		std::unique_ptr<webrtc::EncodedImageCallback> callback;
		std::unique_ptr<webrtc::FramerateController> framerate_controller;
		uint16_t width;
		uint16_t height;
		bool key_frame_request;
		bool send_stream;
	};

	bool IsInitialized() const;

	TAtomic<bool> Initialized;
	webrtc::VideoEncoderFactory* const PrimaryEncoderFactory;
	const webrtc::SdpVideoFormat VideoFormat;
	webrtc::VideoCodec CurrentCodec;
	std::vector<StreamInfo> StreamInfos;
	webrtc::EncodedImageCallback* EncodedCompleteCallback;

	int LowestResolutionStreamIndex;
	int HighestResolutionStreamIndex;
	int EncodeStream(const webrtc::VideoFrame& input_image, FPixelStreamingLayerFrameSource* layer_frame_source, size_t stream_idx, bool send_key_frame);
};

// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "PixelStreamingPrivate.h"
#include "HAL/ThreadSafeBool.h"
#include "WebRTCIncludes.h"
#include "VideoEncoder.h"

class FPlayerSession;
struct FEncoderContext;

// Implementation that is a WebRTC video encoder that allows us tie to our actually underlying non-WebRTC video encoder.
class FPixelStreamingVideoEncoder : public webrtc::VideoEncoder
{
public:
	FPixelStreamingVideoEncoder(FPlayerSession* OwnerSession, FEncoderContext* context);
	virtual ~FPixelStreamingVideoEncoder() override;

	bool IsQualityController() const { return bControlsQuality; }
	void SetQualityController(bool bControlsQuality);

	// WebRTC Interface
	virtual int InitEncode(webrtc::VideoCodec const* codec_settings, webrtc::VideoEncoder::Settings const& settings) override;
	virtual int32 RegisterEncodeCompleteCallback(webrtc::EncodedImageCallback* callback) override;
	virtual int32 Release() override;
	virtual int32 Encode(webrtc::VideoFrame const& frame, std::vector<webrtc::VideoFrameType> const* frame_types) override;
	virtual void SetRates(RateControlParameters const& parameters) override;
	virtual webrtc::VideoEncoder::EncoderInfo GetEncoderInfo() const override;

	// Note: These funcs can also be overriden but are not pure virtual
	// virtual void SetFecControllerOverride(FecControllerOverride* fec_controller_override) override;
	// virtual void OnPacketLossRateUpdate(float packet_loss_rate) override;
	// virtual void OnRttUpdate(int64_t rtt_ms) override;
	// virtual void OnLossNotification(const LossNotification& loss_notification) override;
	// End WebRTC Interface.

	AVEncoder::FVideoEncoder::FLayerConfig GetConfig() const { return EncoderConfig; }
	void UpdateConfig(AVEncoder::FVideoEncoder::FLayerConfig const& config);

	void SendEncodedImage(webrtc::EncodedImage const& encoded_image, webrtc::CodecSpecificInfo const* codec_specific_info, webrtc::RTPFragmentationHeader const* fragmentation);
	FPlayerId GetPlayerId();
	bool IsRegisteredWithWebRTC();

	void ForceKeyFrame() { ForceNextKeyframe = true; }

private:
	void CreateAVEncoder(TSharedPtr<AVEncoder::FVideoEncoderInput> encoderInput);
	void OnEncodedPacket(uint32 InLayerIndex, const AVEncoder::FVideoEncoderInputFrame* InFrame, const AVEncoder::FCodecPacket& InPacket);
	void CreateH264FragmentHeader(uint8 const* CodedData, size_t CodedDataSize, webrtc::RTPFragmentationHeader& Fragments) const;

	// We store this so we can restore back to it if the user decides to use then stop using the PixelStreaming.Encoder.TargetBitrate CVar.
	int32 WebRtcProposedTargetBitrate = 5000000; 
	FEncoderContext* Context;
	FPlayerId PlayerId;

	AVEncoder::FVideoEncoder::FLayerConfig EncoderConfig;

	webrtc::EncodedImageCallback* OnEncodedImageCallback = nullptr;
	
	// Only one encoder controls the quality of the stream, all the others just get this peer's quality.
	// The alternative is encoding separate streams for each peer, this is too much processing until we have layered
	// video encoding like hardware accelerated VP9/AV1.
	FThreadSafeBool bControlsQuality = false;

	bool ForceNextKeyframe = false;
};
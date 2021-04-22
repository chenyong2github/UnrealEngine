// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

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
	virtual int InitEncode(const webrtc::VideoCodec* codec_settings,const webrtc::VideoEncoder::Settings& settings) override;
	virtual int32 RegisterEncodeCompleteCallback(webrtc::EncodedImageCallback* callback) override;
	virtual int32 Release() override;
	virtual int32 Encode(const webrtc::VideoFrame& frame, const std::vector<webrtc::VideoFrameType>* frame_types) override;
	virtual void SetRates(const RateControlParameters& parameters) override;
	virtual webrtc::VideoEncoder::EncoderInfo GetEncoderInfo() const override;

	// Note: These funcs can also be overriden but are not pure virtual
	// virtual void SetFecControllerOverride(FecControllerOverride* fec_controller_override) override;
	// virtual void OnPacketLossRateUpdate(float packet_loss_rate) override;
	// virtual void OnRttUpdate(int64_t rtt_ms) override;
	// virtual void OnLossNotification(const LossNotification& loss_notification) override;
	// End WebRTC Interface.

	void SetMaxBitrate(int32 MaxBitrate);
	void SetTargetBitrate(int32 TargetBitrate);
	void SetMinQP(int32 maxqp);
	void SetRateControl(AVEncoder::FVideoEncoder::RateControlMode mode);
	void EnableFillerData(bool enable);
	void SendEncodedImage(const webrtc::EncodedImage& encoded_image, const webrtc::CodecSpecificInfo* codec_specific_info, const webrtc::RTPFragmentationHeader* fragmentation);
	FPlayerId GetPlayerId();
	bool IsRegisteredWithWebRTC();

	void ForceKeyFrame() { ForceNextKeyframe = true; }

private:
	// We store this so we can restore back to it if the user decides to use then stop using the PixelStreaming.Encoder.TargetBitrate CVar.
	int32 WebRtcProposedTargetBitrate = 20000000;
	FEncoderContext* Context;
	FPlayerId PlayerId;

	AVEncoder::FVideoEncoder::FInit VideoInit;

	webrtc::EncodedImageCallback* OnEncodedImageCallback = nullptr;
	
	// Only one encoder controls the quality of the stream, all the others just get this peer's quality.
	// The alternative is encoding separate streams for each peer, this is too much processing until we have layered
	// video encoding like hardware accelerated VP9/AV1.
	FThreadSafeBool bControlsQuality = false;

	bool ForceNextKeyframe = false;
};
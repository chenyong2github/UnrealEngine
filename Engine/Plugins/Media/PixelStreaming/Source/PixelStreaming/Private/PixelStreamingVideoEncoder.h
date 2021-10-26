// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "PlayerId.h"
#include "HAL/ThreadSafeBool.h"
#include "WebRTCIncludes.h"
#include "VideoEncoder.h"
#include "Templates/SharedPointer.h"
#include "Misc/Optional.h"

class IPixelStreamingSessions;
class FPlayerSession;
struct FEncoderContext;

// Implementation that is a WebRTC video encoder that allows us tie to our actually underlying non-WebRTC video encoder.
class FPixelStreamingVideoEncoder : public webrtc::VideoEncoder
{
public:
	FPixelStreamingVideoEncoder(const IPixelStreamingSessions* InPixelStreamingSessions, FEncoderContext* InContext);
	virtual ~FPixelStreamingVideoEncoder() override;

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

	void SendEncodedImage(webrtc::EncodedImage const& encoded_image, webrtc::CodecSpecificInfo const* codec_specific_info, webrtc::RTPFragmentationHeader const* fragmentation);
	FPlayerId GetPlayerId() const;
	bool IsRegisteredWithWebRTC();

	void ForceKeyFrame() { ForceNextKeyframe = true; }
	int32_t GetSmoothedAverageQP() const;
	

private:
	void UpdateConfig(AVEncoder::FVideoEncoder::FLayerConfig const& Config);
	void HandlePendingRateChange();
	void CreateAVEncoder(TSharedPtr<AVEncoder::FVideoEncoderInput> EncoderInput);
	AVEncoder::FVideoEncoder::FLayerConfig CreateEncoderConfigFromCVars(AVEncoder::FVideoEncoder::FLayerConfig BaseEncoderConfig) const;

	// We store this so we can restore back to it if the user decides to use then stop using the PixelStreaming.Encoder.TargetBitrate CVar.
	int32 WebRtcProposedTargetBitrate = 5000000; 
	FEncoderContext* Context;

	AVEncoder::FVideoEncoder::FLayerConfig EncoderConfig;

	webrtc::EncodedImageCallback* OnEncodedImageCallback = nullptr;
	
	// Note: Each encoder is associated with a player/peer.
	// However, only one encoder controls the quality of the stream, all the others just get this peer's quality.
	// The alternative is encoding separate streams for each peer, which is not tenable while NVENC sessions are limited.
	FPlayerId OwnerPlayerId;

	bool ForceNextKeyframe = false;

	// USed for checks such as whether a given player id is associated with the quality controlling player.
	const IPixelStreamingSessions* PixelStreamingSessions;

	// WebRTC may request a bitrate/framerate change using SetRates(), we only respect this if this encoder is actually encoding
	// so we use this optional object to store a rate change and act upon it when this encoder does its next call to Encode().
	TOptional<RateControlParameters> PendingRateChange;
};
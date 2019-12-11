// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PixelStreamingPrivate.h"

#include "HAL/ThreadSafeBool.h"

class FPlayerSession;

class FVideoEncoder : public webrtc::VideoEncoder, public AVEncoder::IVideoEncoderListener
{
public:

	struct FEncoderCookie : AVEncoder::FEncoderVideoFrameCookie
	{
		virtual ~FEncoderCookie() {}
		webrtc::EncodedImage EncodedImage;
		// buffer to hold last encoded frame bitstream, because `webrtc::EncodedImage` doesn't take ownership of
		// the memory
		TArray<uint8> EncodedFrameBuffer;
	};

	FVideoEncoder(FHWEncoderDetails& InHWEncoderDetails, FPlayerSession& OwnerSession);
	~FVideoEncoder() override;

	bool IsQualityController() const
	{ return bControlsQuality; }
	void SetQualityController(bool bControlsQuality);

	//
	// AVEncoder::IVideoEncoderListener
	//
	void OnEncodedVideoFrame(const AVEncoder::FAVPacket& Packet, AVEncoder::FEncoderVideoFrameCookie* Cookie) override;

	//
	// webrtc::VideoEncoder interface
	//
	int32 InitEncode(const webrtc::VideoCodec* CodecSetings, int32 NumberOfCores, size_t MaxPayloadSize) override;
	int32 RegisterEncodeCompleteCallback(webrtc::EncodedImageCallback* Callback) override;
	int32 Release() override;
	int32 Encode(const webrtc::VideoFrame& Frame, const webrtc::CodecSpecificInfo* CodecSpecificInfo, const std::vector<webrtc::FrameType>* FrameTypes) override;
	int32 SetChannelParameters(uint32 PacketLoss, int64 Rtt) override;
	int32 SetRates(uint32 Bitrate, uint32 Framerate) override;
	int32 SetRateAllocation(const webrtc::BitrateAllocation& Allocation, uint32 Framerate) override;
	ScalingSettings GetScalingSettings() const override;
	bool SupportsNativeHandle() const override;

private:
	// Player session that this encoder instance belongs to
	FHWEncoderDetails& HWEncoderDetails;
	FPlayerSession* PlayerSession = nullptr;
	webrtc::EncodedImageCallback* Callback = nullptr;
	webrtc::CodecSpecificInfo CodecSpecific;
	webrtc::RTPFragmentationHeader FragHeader;

	FThreadSafeBool bControlsQuality = false;
	webrtc::BitrateAllocation LastBitrate;
	uint32 LastFramerate = 0;
};

class FVideoEncoderFactory : public webrtc::VideoEncoderFactory
{
public:
	explicit FVideoEncoderFactory(FHWEncoderDetails& HWEncoderDetails);

	/**
	* This is used from the FPlayerSession::OnSucess to let the factory know
	* what session the next created encoder should belong to.
	* It allows us to get the right FPlayerSession <-> FVideoEncoder relationship
	*/
	void AddSession(FPlayerSession& PlayerSession);

	//
	// webrtc::VideoEncoderFactory implementation
	//
	std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override;
	CodecInfo QueryVideoEncoder(const webrtc::SdpVideoFormat& Format) const override;
	std::unique_ptr<webrtc::VideoEncoder> CreateVideoEncoder(const webrtc::SdpVideoFormat& Format) override;

private:
	FHWEncoderDetails& HWEncoderDetails;
	TQueue<FPlayerSession*> PendingPlayerSessions;
};

class FDummyVideoEncoderFactory : public webrtc::VideoEncoderFactory
{
public:
	std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override;

	CodecInfo QueryVideoEncoder(const webrtc::SdpVideoFormat& Format) const override
	{
		unimplemented();
		return {};
	}

	std::unique_ptr<webrtc::VideoEncoder> CreateVideoEncoder(const webrtc::SdpVideoFormat& Format) override
	{
		unimplemented();
		return {};
	}
};

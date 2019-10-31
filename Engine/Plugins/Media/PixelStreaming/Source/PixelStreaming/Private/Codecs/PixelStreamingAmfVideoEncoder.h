// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PixelStreamingPrivate.h"
#include "Templates/UniquePtr.h"
#include "PixelStreamingBaseVideoEncoder.h"

#include "RHI.h"
#include "RHICommandList.h"
#include "RHIResources.h"
#include "Misc/Timespan.h"
#include "HAL/CriticalSection.h"
#include "HAL/ThreadSafeBool.h"

THIRD_PARTY_INCLUDES_START
	#include "AmdAmf/core/Result.h"
	#include "AmdAmf/core/Factory.h"
	#include "AmdAmf/components/VideoEncoderVCE.h"
	#include "AmdAmf/core/Compute.h"
	#include "AmdAmf/core/Plane.h"
THIRD_PARTY_INCLUDES_END

// Video encoder implementation based on AMD's AMF SDK (https://github.com/GPUOpen-LibrariesAndSDKs/AMF/)
class FPixelStreamingAmfVideoEncoder : public FPixelStreamingBaseVideoEncoder
{
public:

	static bool CheckPlatformCompatibility();

	explicit FPixelStreamingAmfVideoEncoder();
	virtual ~FPixelStreamingAmfVideoEncoder();


	bool CopyBackBuffer(const FTexture2DRHIRef& BackBuffer, FTimespan Timestamp, FBufferId& BufferId) override;

	/**
	* Encode an input back buffer.
	*/
	void EncodeFrame(FBufferId BufferId, const webrtc::EncodedImage& EncodedFrame, uint32 Bitrate) override;

	void OnFrameDropped(FBufferId BufferId) override;

	void SubscribeToFrameEncodedEvent(FVideoEncoder& Subscriber) override;
	void UnsubscribeFromFrameEncodedEvent(FVideoEncoder& Subscriber) override;

private:

	struct FInputFrame
	{
		FTexture2DRHIRef BackBuffer;
		FTimespan CaptureTs;
	};

	struct FOutputFrame
	{
		amf::AMFDataPtr EncodedData;
		webrtc::EncodedImage EncodedFrame;
	};

	enum class EFrameState
	{
		Free,
		Captured,
		Encoding
	};

	struct FFrame
	{
		const FBufferId Id = 0;
		EFrameState State = EFrameState::Free;
		FInputFrame InputFrame;
		FOutputFrame OutputFrame;
		// #AMF : Is this needed?
		uint64 FrameIdx = 0;
	};

	amf_handle DllHandle = nullptr;
	amf::AMFFactory* AmfFactory = nullptr;
	amf::AMFContextPtr AmfContext;
	amf::AMFComponentPtr AmfEncoder;
	// #AMF : Move this to base class
	uint32 CapturedFrameCount = 0; // of captured, not encoded frames
	static const uint32 NumBufferedFrames = 3;
	FFrame BufferedFrames[NumBufferedFrames];
	TAtomic<double> RequestedBitrateMbps{ 0 };

	TQueue<FFrame*> EncodingQueue;

	// Used to make sure we don't have a race condition trying to access a deleted "this" captured
	// in the render command lambda sent to the render thread from EncoderCheckLoop
	static FThreadSafeCounter ImplCounter;

	// buffer to hold last encoded frame bitstream, because `webrtc::EncodedImage` doesn't take ownership of
	// the memory
	TArray<uint8> EncodedFrameBuffer;

	FCriticalSection SubscribersMutex;
	TSet<FVideoEncoder*> Subscribers;

	struct
	{
		uint32 AverageBitRate;
		uint32 FrameRate;
		uint32 Width;
		uint32 Height;
		uint32 MinQP;
		TAtomic<bool> ForceIDR;
	} EncoderConfig;


	FRHICOMMAND_MACRO(FRHISubmitFrameToEncoder)
	{
		FPixelStreamingAmfVideoEncoder* Encoder;
		FFrame* Frame;
		FRHISubmitFrameToEncoder(FPixelStreamingAmfVideoEncoder* InEncoder, FFrame* InFrame)
			: Encoder(InEncoder), Frame(InFrame)
		{
		}

		void Execute(FRHICommandListBase& CmdList)
		{
			Encoder->SubmitFrameToEncoder(*Frame);
		}
	};

	bool Initialize();
	void Shutdown();
	void ResetResolvedBackBuffer(FInputFrame& Frame, uint32 Width, uint32 Height);
	void CopyBackBuffer(const FTexture2DRHIRef& BackBuffer, FInputFrame& InputFrame);
	bool UpdateFramerate();
	void UpdateEncoderConfig(const FInputFrame& InputFrame, uint32 Bitrate);
	void UpdateRes(const FTexture2DRHIRef& BackBuffer, FInputFrame& InputFrame);
	void EncodeFrameInRenderingThread(FFrame& Frame, uint32 Bitrate);
	bool SubmitFrameToEncoder(FFrame& Frame);
	bool HandleEncodedFrame(FFrame& Frame);
	bool ProcessOutput();
	void OnEncodedFrame(const webrtc::EncodedImage& EncodedImage);
};

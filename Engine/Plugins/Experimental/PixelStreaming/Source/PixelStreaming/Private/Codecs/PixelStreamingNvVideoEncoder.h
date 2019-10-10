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
#include "NvEncoder/nvEncodeAPI.h"
THIRD_PARTY_INCLUDES_END

class FVideoEncoder;
class FThread;

struct FNvEncStats
{
	double LatencyMs = 0;
	double ProducedBitrateMbps = 0;
	double QP = 0;
};

// Video encoder implementation based on NVIDIA Video Codecs SDK: https://developer.nvidia.com/nvidia-video-codec-sdk
// Uses only encoder part
class FPixelStreamingNvVideoEncoder : public FPixelStreamingBaseVideoEncoder
{
public:
	/**
	 * Check to see if the Nvidia NVENC Video Encoder is available on the
	 * platform we are running on.
	 */
	static bool CheckPlatformCompatibility();

	/**
	* Note bEnableAsyncMode flag is for debugging purpose, it should be set to true normally unless user wants to test in synchronous mode.
	*/
	explicit FPixelStreamingNvVideoEncoder(bool bEnableAsyncMode = true);
	~FPixelStreamingNvVideoEncoder();

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
		void* RegisteredResource = nullptr;
		NV_ENC_INPUT_PTR MappedResource = nullptr;
		NV_ENC_BUFFER_FORMAT BufferFormat;
		FTexture2DRHIRef BackBuffer;
		FTimespan CaptureTs;
	};

	struct FOutputFrame
	{
		NV_ENC_OUTPUT_PTR BitstreamBuffer = nullptr;
		HANDLE EventHandle = nullptr;
		webrtc::EncodedImage EncodedFrame;
	};

	struct FFrame
	{
		FInputFrame InputFrame;
		FOutputFrame OutputFrame;
		uint64 FrameIdx = 0;
	};

	void Init_RenderThread(bool bEnableAsyncMode);
	void InitFrameInputBuffer(FInputFrame& InputFrame, uint32 Width, uint32 Heigh);
	void InitializeResources();
	void ReleaseFrameInputBuffer(FInputFrame& InputFrame);
	void ReleaseResources();
	void RegisterAsyncEvent(void** OutEvent);
	void UnregisterAsyncEvent(void* Event);

	bool UpdateFramerate();
	void UpdateNvEncConfig(const FInputFrame& InputFrame, uint32 Bitrate);
	void UpdateRes(const FTexture2DRHIRef& BackBuffer, FInputFrame& InputFrame);
	void CopyBackBuffer(const FTexture2DRHIRef& BackBuffer, FInputFrame& InputFrame);

	void EncodeFrameInRenderingThread(FFrame& Frame, uint32 Bitrate);

	void EncoderCheckLoop();

	void ProcessFrame(FFrame& Frame);

	void UpdateSettings(FInputFrame& InputFrame, uint32 Bitrate);
	void TransferRenderTargetToHWEncoder(FFrame& Frame);

	void PostRenderingThreadCreated()
	{
		bWaitForRenderThreadToResume = false;
	}

	void PreRenderingThreadDestroyed()
	{
		bWaitForRenderThreadToResume = true;
	}

	void OnEncodedFrame(const webrtc::EncodedImage& EncodedImage);

	FRHICOMMAND_MACRO(FRHITransferRenderTargetToNvEnc)
	{
		FPixelStreamingNvVideoEncoder* Encoder;
		FFrame* Frame;

		FRHITransferRenderTargetToNvEnc(FPixelStreamingNvVideoEncoder* InEncoder, FFrame* InFrame)
			: Encoder(InEncoder), Frame(InFrame)
		{}

		void Execute(FRHICommandListBase& CmdList)
		{
			Encoder->TransferRenderTargetToHWEncoder(*Frame);
		}
	};

	void* DllHandle = nullptr;

	TUniquePtr<NV_ENCODE_API_FUNCTION_LIST> NvEncodeAPI;
	void* EncoderInterface = nullptr;
	NV_ENC_INITIALIZE_PARAMS NvEncInitializeParams;
	NV_ENC_CONFIG NvEncConfig;
	FThreadSafeBool bWaitForRenderThreadToResume = false;
	// Used to make sure we don't have a race condition trying to access a deleted "this" captured
	// in the render command lambda sent to the render thread from EncoderCheckLoop
	static FThreadSafeCounter ImplCounter;
	uint32 CapturedFrameCount = 0; // of captured, not encoded frames
	static const uint32 NumBufferedFrames = 3;
	FFrame BufferedFrames[NumBufferedFrames];
	TUniquePtr<FThread> EncoderThread;
	FThreadSafeBool bExitEncoderThread = false;
	// buffer to hold last encoded frame bitstream, because `webrtc::EncodedImage` doesn't take ownership of
	// the memory
	TArray<uint8> EncodedFrameBuffer;

	// collaboration with WebRTC is quite convoluted. We capture frame and pass it to WebRTC
	// TODO(andriy): implement proper waiting on empty queues instead of spin-lock
	TQueue<FBufferId> FreeBuffers;
	TQueue<FBufferId> CapturedBuffers;
	TQueue<FBufferId> BuffersBeingEncoded;

	float InitialMaxFPS;

	// #AMF(Andriy) : This is only used from one thread, I think, so the comment below is wrong, and it doesn't need to be TAtomic. Need to confirm.
	//		  cached value because it's used from another thread.
	//		  Update : If running with RHITRhead (e.g: use r.rhithread.enable 1 ), this is indeed used from two threads (RenderThread and RHIThread, I think)
	TAtomic<double> RequestedBitrateMbps{ 0 };

	FCriticalSection SubscribersMutex;
	TSet<FVideoEncoder*> Subscribers;
};

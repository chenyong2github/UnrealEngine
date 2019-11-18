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

// This is mostly to use internally at Epic.
// Setting this to 1 will collect detailed timings in the `Timings` member array.
// It will also clear every frame with a solid colour before copying the backbuffer into it.
#define NVENC_VIDEO_ENCODER_DEBUG 0

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

	FPixelStreamingNvVideoEncoder();
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
		FInputFrame() {}
		UE_NONCOPYABLE(FInputFrame);
		void* RegisteredResource = nullptr;
		NV_ENC_INPUT_PTR MappedResource = nullptr;
		NV_ENC_BUFFER_FORMAT BufferFormat;
		FTexture2DRHIRef BackBuffer;
		ID3D11Texture2D* SharedBackBuffer = nullptr;
		FTimespan CaptureTs;
		FGPUFenceRHIRef CopyFence;
	};

	struct FOutputFrame
	{
		FOutputFrame() {}
		UE_NONCOPYABLE(FOutputFrame);
		NV_ENC_OUTPUT_PTR BitstreamBuffer = nullptr;
		HANDLE EventHandle = nullptr;
		webrtc::EncodedImage EncodedFrame;
	};

	enum class EFrameState
	{
		Free,
		Capturing,
		Captured,
		Encoding
	};

	struct FFrame
	{
		FFrame() {}
		UE_NONCOPYABLE(FFrame);

		// Array index of this FFrame. This is set at startup, and should never be changed
		FBufferId Id = 0;

		TAtomic<EFrameState> State = { EFrameState::Free };
		// Bitrate requested at the time the video encoder asked us to encode this frame
		// We save this, because we can't use it at the moment we receive it.
		uint32 BitrateRequested = 0;
		FInputFrame InputFrame;
		FOutputFrame OutputFrame;
		uint64 FrameIdx = 0;

		// Some timestamps for debugging
#if NVENC_VIDEO_ENCODER_DEBUG
		FTimespan CopyBufferStartTs;
		FTimespan CopyBufferFinishTs;
		FTimespan EncodingStartTs;
		FTimespan EncodingFinishTs;
#endif
	};

	void Init();
	void InitFrameInputBuffer(FFrame& Frame, uint32 Width, uint32 Heigh);
	void InitializeResources();
	void ReleaseFrameInputBuffer(FFrame& Frame);
	void ReleaseResources();
	void RegisterAsyncEvent(void** OutEvent);
	void UnregisterAsyncEvent(void* Event);

	bool UpdateFramerate();
	void UpdateNvEncConfig(const FInputFrame& InputFrame, uint32 Bitrate);
	void UpdateRes(const FTexture2DRHIRef& BackBuffer, FFrame& Frame);
	void CopyBackBuffer(const FTexture2DRHIRef& BackBuffer, FFrame& Frame);

	void EncoderCheckLoop();

	void ProcessFrame(FFrame& Frame);

	void UpdateSettings(FInputFrame& InputFrame, uint32 Bitrate);
	void SubmitFrameToEncoder(FFrame& Frame);

	void OnEncodedFrame(const webrtc::EncodedImage& EncodedImage);

	void* DllHandle = nullptr;

	TUniquePtr<NV_ENCODE_API_FUNCTION_LIST> NvEncodeAPI;
	void* EncoderInterface = nullptr;
	NV_ENC_INITIALIZE_PARAMS NvEncInitializeParams;
	NV_ENC_CONFIG NvEncConfig;
	FThreadSafeBool bWaitForRenderThreadToResume = false;
	uint32 CapturedFrameCount = 0; // of captured, not encoded frames
	static constexpr uint32 NumBufferedFrames = 3;
	FFrame BufferedFrames[NumBufferedFrames];
	TUniquePtr<FThread> EncoderThread;
	FThreadSafeBool bExitEncoderThread = false;
	// buffer to hold last encoded frame bitstream, because `webrtc::EncodedImage` doesn't take ownership of
	// the memory
	TArray<uint8> EncodedFrameBuffer;

	float InitialMaxFPS;

	class FEncoderDevice
	{
	public:
		FEncoderDevice();

		TRefCountPtr<ID3D11Device> Device;
		TRefCountPtr<ID3D11DeviceContext> DeviceContext;
	};

	TUniquePtr<FEncoderDevice> EncoderDevice;

	// After a back buffer is processed and copied then we will want to send it
	// to the encoder. This happens on a different thread so we use a queue of
	// frame pointers to tell the thread which frames should be encoded.
	struct FEncodeQueue
	{
		// The frames which we should encode.
		// We can never be encoding more frames that can be buffered.
		FFrame* Frames[NumBufferedFrames] = { nullptr };

		// The start position of elements in this FIFO ring buffer queue.
		int Start = 0;

		// The number of elements in this FIFO ring buffer queue.
		int Length = 0;

		// Allow access by the Render Thread and the Pixel Streaming Encoder
		// thread.
		FCriticalSection CriticalSection;

		// An event to signal the Pixel Streaming Encoder thread that it can
		// encode some frames.
		HANDLE EncodeEvent = CreateEvent(nullptr, false, false, nullptr);

		virtual ~FEncodeQueue()
		{
			CloseHandle(EncodeEvent);
		}

		// Add another frame to be encoded.
		void Push(FFrame* Frame)
		{
			FScopeLock ScopedLock(&CriticalSection);
			check(Length < NumBufferedFrames);
			bool bWasEmpty = Length == 0;
			int Position = (Start + Length) % NumBufferedFrames;
			Frames[Position] = Frame;
			Length++;
			if (bWasEmpty)
			{
				SetEvent(EncodeEvent);
			}
		}

		// Get the list of all frame which we should encode.
		void PopAll(FFrame* OutFrames[NumBufferedFrames], int& OutNumFrames)
		{
			FScopeLock ScopedLock(&CriticalSection);
			OutNumFrames = Length;
			for (int Position = 0; Position < Length; Position++)
			{
				OutFrames[Position] = Frames[Start];
				Start = (Start + 1) % NumBufferedFrames;
			}
			Length = 0;
			ResetEvent(EncodeEvent);
		}
	};

#if NVENC_VIDEO_ENCODER_DEBUG
	// This is just for debugging
	void ClearFrame(FFrame& Frame);
	// Timings in milliseconds. Just for debugging
	struct FFrameTiming
	{
		// 0 : CopyBufferStart -> CopyBufferFinish
		// 1 : CopyBufferStart -> EncodingStart
		// 2 : CopyBufferStart -> EncodingFinish
		double Total[3];
		// 0 : CopyBufferStart -> CopyBufferFinish
		// 1 : CopyBufferFinish -> EncodingStart
		// 2 : EncodingStart -> EncodingFinish
		double Steps[3];
	};
	TArray<FFrameTiming> Timings;
#endif

	FEncodeQueue EncodeQueue;
	double RequestedBitrateMbps = 0;
	FCriticalSection SubscribersMutex;
	TSet<FVideoEncoder*> Subscribers;
};

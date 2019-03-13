// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BaseVideoEncoder.h"

#if PLATFORM_WINDOWS

THIRD_PARTY_INCLUDES_START
	#include "NvEncoder/nvEncodeAPI.h"
THIRD_PARTY_INCLUDES_END

#include "D3D11VideoProcessor.h"

DECLARE_LOG_CATEGORY_EXTERN(NvVideoEncoder, Log, VeryVerbose);

class FEncoderDevice;
class FThread;

class FNvVideoEncoder : public FBaseVideoEncoder
{
public:
	explicit FNvVideoEncoder(const FOutputSampleCallback& OutputCallback, TSharedPtr<FEncoderDevice> InEncoderDevice);
	~FNvVideoEncoder() override;

	bool Initialize(const FVideoEncoderConfig& Config) override;
	bool Start() override;
	void Stop() override;
	bool Process(const FTexture2DRHIRef& Texture, FTimespan Timestamp, FTimespan Duration) override;

	bool SetBitrate(uint32 Bitrate) override;
	bool SetFramerate(uint32 Framerate) override;

private:
	struct FInputFrame
	{
		void* RegisteredResource = nullptr;
		NV_ENC_INPUT_PTR MappedResource = nullptr;
		NV_ENC_BUFFER_FORMAT BufferFormat;
	};

	struct FOutputFrame
	{
		NV_ENC_OUTPUT_PTR BitstreamBuffer = nullptr;
		HANDLE EventHandle = nullptr;
	};

	struct FFrame
	{
		FTexture2DRHIRef ResolvedBackBuffer;
		ID3D11Texture2D* SharedBackBuffer = nullptr;
		FInputFrame InputFrame;
		FOutputFrame OutputFrame;
		TArray<uint8> EncodedFrame;
		uint64 FrameIdx = 0;

		// These are passed to the mp4 writer
		FTimespan TimeStamp = 0;
		FTimespan Duration = 0;

		// Timestamps to measure encoding latency
		FTimespan CaptureTimeStamp;
		FTimespan EncodeStartTimeStamp;
		FTimespan EncodeEndTimeStamp;

		FThreadSafeBool bEncoding = false;
	};

	bool ProcessInput(const FTexture2DRHIRef& Texture, FTimespan Timestamp, FTimespan Duration);

	void CopyBackBuffer(const FTexture2DRHIRef& SrcBackBuffer, const FFrame& DstFrame);
	bool InitFrameInputBuffer(FFrame& Frame);
	bool InitializeResources();
	bool ReleaseFrameInputBuffer(FNvVideoEncoder::FFrame& Frame);
	bool RegisterAsyncEvent(void** OutEvent);
	bool UnregisterAsyncEvent(void* Event);
	bool ReleaseResources();
	void EncoderCheckLoop();
	bool SubmitFrameToEncoder(FFrame& Frame);
	bool HandleEncodedFrame(FNvVideoEncoder::FFrame& Frame);

	bool Reconfigure();

	void* DllHandle = nullptr;
	bool bInitialized = false;
	TUniquePtr<NV_ENCODE_API_FUNCTION_LIST> NvEncodeAPI;
	void* EncoderInterface = nullptr;
	static const uint32 NumBufferedFrames = 3;
	FFrame BufferedFrames[NumBufferedFrames];
	FD3D11VideoProcessor D3D11VideoProcessor;

	NV_ENC_INITIALIZE_PARAMS NvEncInitializeParams;
	NV_ENC_CONFIG NvEncConfig;

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
			bool bWasEmpty = Length == 0;
			int Position = (Start + Length) % NumBufferedFrames;
			Frames[Position] = Frame;
			Length++;
			check(Length <= NumBufferedFrames);
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

	// We use a separate D3D device with NvEnv so we can do the encoding on a
	// separate thread without problems.
	TSharedPtr<FEncoderDevice> EncoderDevice;

	// The Pixel Streaming Encoder thread which NvEnc encodes on.
	TUniquePtr<FThread> EncoderThread;

	// We enqueue frame pointers to tell the Pixel Streaming Encoder thread
	// which frames to encode.
	FEncodeQueue EncodeQueue;

	// Whether the Pixel Streaming Encoder thread is complete and should exit.
	FThreadSafeBool bExitEncoderThread = false;
};

#endif // PLATFORM_WINDOWS


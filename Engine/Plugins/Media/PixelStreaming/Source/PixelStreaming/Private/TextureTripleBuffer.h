// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "PixelStreamingTextureSource.h"
#include "Templates/SharedPointer.h"
#include "Templates/RefCounting.h"
#include "HAL/ThreadSafeBool.h"

namespace UE::PixelStreaming
{
	struct FWriteBuffer
	{
		bool bAvailable = true;
		uint64 PreWaitingOnCopy;
		TSharedPtr<IPixelStreamingFrameCapturer> FrameCapturer;
		TSharedPtr<FPixelStreamingTextureWrapper> CapturedTexture;
	};

	/*
	* A triple buffering mechanism for textures being captured and encoded in Pixel Streaming.
	* Texture copying happens on its own thread.
	* Encoding happens on its own thread.
	* Thus we have a multithreaded single producer/single consumer setup and the following requirements:
	* 1) Reads and writes must be threadsafe
	* 2) Neither read or write should block
	* 3) The rate of capture should not impact the rate of encode.
	* 
	* We achieve these requirements by using a triple buffering strategy where reading a texture is always fast and non-blocking
	* by returning the most recently written texture (which may be one we have read before).
	* The result is we can read textures at a constant speed regardless of how slow writes are, which means we have decoupled render fps from encode fps.
	*/
	class FTextureTripleBuffer
	{
	public:
		FTextureTripleBuffer(float InFrameScale, TUniquePtr<FPixelStreamingTextureSource> TextureGenerator);
		virtual ~FTextureTripleBuffer();
		virtual TSharedPtr<FPixelStreamingTextureWrapper> GetCurrent();
		virtual rtc::scoped_refptr<webrtc::I420Buffer> ToWebRTCI420Buffer(TSharedPtr<FPixelStreamingTextureWrapper> Texture) { return TextureGenerator->ToWebRTCI420Buffer(Texture); }
		virtual void SetEnabled(bool bInEnabled);
		virtual bool IsEnabled() const { return *bEnabled; }
		virtual bool IsAvailable() const { return bInitialized; }
		virtual uint32 GetSourceWidth() const { return SourceWidth; }
		virtual uint32 GetSourceHeight() const { return SourceHeight; }
		virtual float GetFrameScaling() const { return FrameScale; }

	protected:
		virtual void OnNewTexture(FPixelStreamingTextureWrapper& NewFrame, uint32 FrameWidth, uint32 FrameHeight);
		virtual TSharedPtr<FWriteBuffer> CreateWriteBuffer(uint32 Width, uint32 Height);
		virtual void Initialize(uint32 Width, uint32 Height);

	private:
		const float FrameScale;
		TUniquePtr<FPixelStreamingTextureSource> TextureGenerator;
		uint32 SourceWidth = 0;
		uint32 SourceHeight = 0;
		FThreadSafeBool bInitialized = false;
		TSharedRef<bool, ESPMode::ThreadSafe> bEnabled = MakeShared<bool, ESPMode::ThreadSafe>(true);
		bool bWaitForTextureCopy = false;
		/*
		* Triple buffer setup with queued write buffers (since we have to wait for RHI copy).
		* 1 Read buffer (read the captured texture)
		* 1 Temp buffer (for swapping what is read and written)
		* 2 Write buffers (2 write buffers because UE can render two frames before presenting sometimes)
		*/
		FCriticalSection CriticalSection;
		bool bWriteParity = true;
		TSharedPtr<FPixelStreamingTextureWrapper> TempBuffer;
		TSharedPtr<FPixelStreamingTextureWrapper> ReadBuffer;
		TSharedPtr<FWriteBuffer> WriteBuffer1;
		TSharedPtr<FWriteBuffer> WriteBuffer2;
		FThreadSafeBool bIsTempDirty;
	};
}
// Copyright Epic Games, Inc. All Rights Reserved.

#include "FrameBuffer.h"
#include "Utils.h"
#include "Stats.h"

namespace UE::PixelStreaming
{
	/*
	* ----------------- FInitializeFrameBuffer -----------------
	*/

	FInitializeFrameBuffer::FInitializeFrameBuffer(TSharedPtr<FTextureTripleBuffer> InTextureBuffer)
		: TextureBuffer(InTextureBuffer)
	{
	}

	FInitializeFrameBuffer::~FInitializeFrameBuffer()
	{
	}

	int FInitializeFrameBuffer::width() const
	{
		return TextureBuffer->GetSourceWidth();
	}

	int FInitializeFrameBuffer::height() const
	{
		return TextureBuffer->GetSourceHeight();
	}

	/*
	* ----------------- FSimulcastFrameBuffer -----------------
	*/

	FSimulcastFrameBuffer::FSimulcastFrameBuffer(TArray<TSharedPtr<FTextureTripleBuffer>>& InTextureBuffers)
		: TextureBuffers(InTextureBuffers)
	{
	}

	FSimulcastFrameBuffer::~FSimulcastFrameBuffer()
	{
	}

	int FSimulcastFrameBuffer::GetNumLayers() const
	{
		return TextureBuffers.Num();
	}

	TSharedPtr<FTextureTripleBuffer> FSimulcastFrameBuffer::GetLayerFrameSource(int LayerIndex) const
	{
		checkf(LayerIndex >= 0 && LayerIndex < TextureBuffers.Num(), TEXT("Requested layer index was out of bounds."));
		return TextureBuffers[LayerIndex];
	}

	int FSimulcastFrameBuffer::width() const
	{
		checkf(TextureBuffers.Num() > 0, TEXT("Must be at least one texture source to get the width from."));
		return FMath::Max(TextureBuffers[0]->GetSourceWidth(), TextureBuffers[TextureBuffers.Num() - 1]->GetSourceWidth());
	}

	int FSimulcastFrameBuffer::height() const
	{
		checkf(TextureBuffers.Num() > 0, TEXT("Must be at least one texture source to get the height from."));
		return FMath::Max(TextureBuffers[0]->GetSourceHeight(), TextureBuffers[TextureBuffers.Num() - 1]->GetSourceHeight());
	}

	/*
	* ----------------- FLayerFrameBuffer -----------------
	*/

	FLayerFrameBuffer::FLayerFrameBuffer(TSharedPtr<FTextureTripleBuffer> InTextureBuffer)
		: TextureBuffer(InTextureBuffer)
	{
	}

	FLayerFrameBuffer::~FLayerFrameBuffer()
	{
	}

	TSharedPtr<FPixelStreamingTextureWrapper> FLayerFrameBuffer::GetFrame() const
	{
		return TextureBuffer->GetCurrent();
	}

	int FLayerFrameBuffer::width() const
	{
		return TextureBuffer->GetSourceWidth();
	}

	int FLayerFrameBuffer::height() const
	{
		return TextureBuffer->GetSourceHeight();
	}

	/*
	* ----------------- FFrameBufferI420 -----------------
	*/

	FFrameBufferI420::FFrameBufferI420(TSharedPtr<FTextureTripleBuffer> InTextureBuffer)
		: TextureBuffer(InTextureBuffer)
	{
	}

	FFrameBufferI420::~FFrameBufferI420()
	{
	}

	TSharedPtr<FPixelStreamingTextureWrapper> FFrameBufferI420::GetFrame() const
	{
		return TextureBuffer->GetCurrent();
	}

	int FFrameBufferI420::width() const
	{
		return TextureBuffer->GetSourceWidth();
	}

	int FFrameBufferI420::height() const
	{
		return TextureBuffer->GetSourceHeight();
	}

	const webrtc::I420BufferInterface* FFrameBufferI420::GetI420() const
	{
		return Buffer;
	}

	/*
	* NOTE: Only used for non-hardware encoders (e.g. VP8) - H264 does NOT hit this function.
	*/
	rtc::scoped_refptr<webrtc::I420BufferInterface> FFrameBufferI420::ToI420()
	{
		TSharedPtr<FPixelStreamingTextureWrapper> Current = TextureBuffer->GetCurrent();

		uint64 PreConversion = FPlatformTime::Cycles64();

		Buffer = TextureBuffer->ToWebRTCI420Buffer(Current);

		uint64 PostConversion = FPlatformTime::Cycles64();
		UE::PixelStreaming::FStats* Stats = UE::PixelStreaming::FStats::Get();
		if (Stats)
		{
			double ConversionMs = FPlatformTime::ToMilliseconds64(PostConversion - PreConversion);
			Stats->StoreApplicationStat(UE::PixelStreaming::FStatData(FName(*FString::Printf(TEXT("BGRA->YUV420 (ms)"))), ConversionMs, 2, true));
		}

		return Buffer;
	}
}
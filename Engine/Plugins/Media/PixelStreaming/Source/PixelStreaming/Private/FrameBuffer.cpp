// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreamingFrameBuffer.h"
#include "PixelStreamingTextureSource.h"
#include "Utils.h"

/*
* ----------------- FInitializeFrameBuffer -----------------
*/

FInitializeFrameBuffer::FInitializeFrameBuffer(TSharedPtr<IPixelStreamingTextureSource> InTextureSource)
	: TextureSource(InTextureSource)
{
}

FInitializeFrameBuffer::~FInitializeFrameBuffer()
{
}

int FInitializeFrameBuffer::width() const
{
	return TextureSource->GetSourceWidth();
}

int FInitializeFrameBuffer::height() const
{
	return TextureSource->GetSourceHeight();
}

/*
* ----------------- FSimulcastFrameBuffer -----------------
*/

FSimulcastFrameBuffer::FSimulcastFrameBuffer(TArray<TSharedPtr<IPixelStreamingTextureSource>>& InTextureSources)
	: TextureSources(InTextureSources)
{
}

FSimulcastFrameBuffer::~FSimulcastFrameBuffer()
{
}

int FSimulcastFrameBuffer::GetNumLayers() const
{
	return TextureSources.Num();
}

TSharedPtr<IPixelStreamingTextureSource> FSimulcastFrameBuffer::GetLayerFrameSource(int LayerIndex) const
{
	checkf(LayerIndex >= 0 && LayerIndex < TextureSources.Num(), TEXT("Requested layer index was out of bounds."));
	return TextureSources[LayerIndex];
}

int FSimulcastFrameBuffer::width() const
{
	checkf(TextureSources.Num() > 0, TEXT("Must be at least one texture source to get the width from."));
	return FMath::Max(TextureSources[0]->GetSourceWidth(), TextureSources[TextureSources.Num() - 1]->GetSourceWidth());
}

int FSimulcastFrameBuffer::height() const
{
	checkf(TextureSources.Num() > 0, TEXT("Must be at least one texture source to get the height from."));
	return FMath::Max(TextureSources[0]->GetSourceHeight(), TextureSources[TextureSources.Num() - 1]->GetSourceHeight());
}

/*
* ----------------- FLayerFrameBuffer -----------------
*/

FLayerFrameBuffer::FLayerFrameBuffer(TSharedPtr<IPixelStreamingTextureSource> InTextureSource)
	: TextureSource(InTextureSource)
{
}

FLayerFrameBuffer::~FLayerFrameBuffer()
{
}

FTexture2DRHIRef FLayerFrameBuffer::GetFrame() const
{
	return TextureSource->GetTexture();
}

int FLayerFrameBuffer::width() const
{
	return TextureSource->GetSourceWidth();
}

int FLayerFrameBuffer::height() const
{
	return TextureSource->GetSourceHeight();
}

/*
* ----------------- FFrameBufferI420 -----------------
*/

FFrameBufferI420::FFrameBufferI420(TSharedPtr<IPixelStreamingTextureSource> InTextureSource)
	: TextureSource(InTextureSource)
{
}

FFrameBufferI420::~FFrameBufferI420()
{
}

FTexture2DRHIRef FFrameBufferI420::GetFrame() const
{
	return TextureSource->GetTexture();
}

int FFrameBufferI420::width() const
{
	return TextureSource->GetSourceWidth();
}

int FFrameBufferI420::height() const
{
	return TextureSource->GetSourceHeight();
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
	IBackBufferTextureSource* TextureSourcePtr = static_cast<IBackBufferTextureSource*>(TextureSource.Get());
	checkf(TextureSourcePtr && TextureSourcePtr->GetName() == FString(TEXT("FBackBufferToCPUTextureSource")), TEXT("To use VPX encoder texture source must be FBackBufferToCPUTextureSource"));

	TRefCountPtr<FPixelStreamingCPUReadableBackbufferTexture> Current = static_cast<FPixelStreamingCPUReadableBackbufferTexture*>(TextureSourcePtr->GetCurrent().GetReference());

	uint32 TextureWidth = Current->GetTexture()->GetSizeX();
	uint32 TextureHeight = Current->GetTexture()->GetSizeY();

	uint32 i = 0;
	uint32 NumPixels = TextureWidth * TextureHeight;
	uint32 ui = NumPixels;
	uint32 vi = NumPixels + NumPixels / 4;
	uint32 s = 0;

// data is coming in as B,G,R,A
#define sR ((uint32)(rgbIn[s + 2]))
#define sG ((uint32)(rgbIn[s + 1]))
#define sB ((uint32)(rgbIn[s + 0]))

	Buffer = webrtc::I420Buffer::Create(TextureWidth, TextureHeight, TextureWidth, TextureWidth / 2, TextureWidth / 2);

	// If we have no raw pixels then early exit this swizzle operation
	if (Current->GetRawPixels().Num() == 0)
	{
		webrtc::I420Buffer::SetBlack(Buffer);
		return Buffer;
	}

	uint8* yuv420p = Buffer->MutableDataY();
	const uint8* rgbIn = (const uint8*)Current->GetRawPixels().GetData();
	for (uint32 j = 0; j < TextureHeight; j++)
	{
		for (uint32 k = 0; k < TextureWidth; k++)
		{
			yuv420p[i] = (uint8)((66 * sR + 129 * sG + 25 * sB + 128) >> 8) + 16;

			if (0 == j % 2 && 0 == k % 2)
			{
				yuv420p[ui++] = (uint8)((-38 * sR - 74 * sG + 112 * sB + 128) >> 8) + 128;
				yuv420p[vi++] = (uint8)((112 * sR - 94 * sG - 18 * sB + 128) >> 8) + 128;
			}
			i++;
			s += 4;
		}
	}

#undef sR
#undef sG
#undef sB

	return Buffer;
}

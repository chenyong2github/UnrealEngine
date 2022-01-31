// Copyright Epic Games, Inc. All Rights Reserved.

#include "FrameBuffer.h"
#include "TextureSource.h"
#include "Utils.h"

/*
* ----------------- FInitializeFrameBuffer -----------------
*/

UE::PixelStreaming::FInitializeFrameBuffer::FInitializeFrameBuffer(TSharedPtr<ITextureSource> InTextureSource)
	: TextureSource(InTextureSource)
{
}

UE::PixelStreaming::FInitializeFrameBuffer::~FInitializeFrameBuffer()
{
}

int UE::PixelStreaming::FInitializeFrameBuffer::width() const
{
	return TextureSource->GetSourceWidth();
}

int UE::PixelStreaming::FInitializeFrameBuffer::height() const
{
	return TextureSource->GetSourceHeight();
}

/*
* ----------------- FSimulcastFrameBuffer -----------------
*/

UE::PixelStreaming::FSimulcastFrameBuffer::FSimulcastFrameBuffer(TArray<TSharedPtr<ITextureSource>>& InTextureSources)
	: TextureSources(InTextureSources)
{
}

UE::PixelStreaming::FSimulcastFrameBuffer::~FSimulcastFrameBuffer()
{
}

int UE::PixelStreaming::FSimulcastFrameBuffer::GetNumLayers() const
{
	return TextureSources.Num();
}

TSharedPtr<UE::PixelStreaming::ITextureSource> UE::PixelStreaming::FSimulcastFrameBuffer::GetLayerFrameSource(int LayerIndex) const
{
	checkf(LayerIndex >= 0 && LayerIndex < TextureSources.Num(), TEXT("Requested layer index was out of bounds."));
	return TextureSources[LayerIndex];
}

int UE::PixelStreaming::FSimulcastFrameBuffer::width() const
{
	checkf(TextureSources.Num() > 0, TEXT("Must be at least one texture source to get the width from."));
	return FMath::Max(TextureSources[0]->GetSourceWidth(), TextureSources[TextureSources.Num() - 1]->GetSourceWidth());
}

int UE::PixelStreaming::FSimulcastFrameBuffer::height() const
{
	checkf(TextureSources.Num() > 0, TEXT("Must be at least one texture source to get the height from."));
	return FMath::Max(TextureSources[0]->GetSourceHeight(), TextureSources[TextureSources.Num() - 1]->GetSourceHeight());
}

/*
* ----------------- FLayerFrameBuffer -----------------
*/

UE::PixelStreaming::FLayerFrameBuffer::FLayerFrameBuffer(TSharedPtr<UE::PixelStreaming::ITextureSource> InTextureSource)
	: TextureSource(InTextureSource)
{
}

UE::PixelStreaming::FLayerFrameBuffer::~FLayerFrameBuffer()
{
}

FTexture2DRHIRef UE::PixelStreaming::FLayerFrameBuffer::GetFrame() const
{
	return TextureSource->GetTexture();
}

int UE::PixelStreaming::FLayerFrameBuffer::width() const
{
	return TextureSource->GetSourceWidth();
}

int UE::PixelStreaming::FLayerFrameBuffer::height() const
{
	return TextureSource->GetSourceHeight();
}

/*
* ----------------- FFrameBufferI420 -----------------
*/

UE::PixelStreaming::FFrameBufferI420::FFrameBufferI420(TSharedPtr<UE::PixelStreaming::ITextureSource> InTextureSource)
	: TextureSource(InTextureSource)
{
}

UE::PixelStreaming::FFrameBufferI420::~FFrameBufferI420()
{
}

FTexture2DRHIRef UE::PixelStreaming::FFrameBufferI420::GetFrame() const
{
	return TextureSource->GetTexture();
}

int UE::PixelStreaming::FFrameBufferI420::width() const
{
	return TextureSource->GetSourceWidth();
}

int UE::PixelStreaming::FFrameBufferI420::height() const
{
	return TextureSource->GetSourceHeight();
}

const webrtc::I420BufferInterface* UE::PixelStreaming::FFrameBufferI420::GetI420() const
{
	return Buffer;
}

/*
* NOTE: Only used for non-hardware encoders (e.g. VP8) - H264 does NOT hit this function.
*/
rtc::scoped_refptr<webrtc::I420BufferInterface> UE::PixelStreaming::FFrameBufferI420::ToI420()
{
	ITextureSource* TextureSourcePtr = TextureSource.Get();
	checkf(TextureSourcePtr && TextureSourcePtr->GetName() == FString(TEXT("FTextureSourceBackBufferToCPU")), TEXT("To use VPX encoder texture source must be FTextureSourceBackBufferToCPU"));

	FTextureSourceBackBufferToCPU* TextureSourceCPU = static_cast<FTextureSourceBackBufferToCPU*>(TextureSourcePtr);
	TRefCountPtr<FRawPixelsTexture> Current = TextureSourceCPU->GetCurrent();

	uint32 TextureWidth = Current->TextureRef->GetSizeX();
	uint32 TextureHeight = Current->TextureRef->GetSizeY();

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
	if (Current->RawPixels.Num() == 0)
	{
		webrtc::I420Buffer::SetBlack(Buffer);
		return Buffer;
	}

	uint8* yuv420p = Buffer->MutableDataY();
	const uint8* rgbIn = (const uint8*)Current->RawPixels.GetData();
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

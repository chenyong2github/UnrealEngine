// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WebRTCIncludes.h"
#include "VideoEncoderInput.h"
#include "RHI.h"

class FPixelStreamingFrameBuffer : public webrtc::VideoFrameBuffer
{
public:
	explicit FPixelStreamingFrameBuffer(FTexture2DRHIRef SourceTexture, AVEncoder::FVideoEncoderInputFrame* InputFrame, TSharedPtr<AVEncoder::FVideoEncoderInput> InputVideoEncoderInput)
		: TextureRef(SourceTexture), Frame(InputFrame), VideoEncoderInput(InputVideoEncoderInput)
	{
		Frame->Obtain();
	}

	~FPixelStreamingFrameBuffer()
	{
		Frame->Release();
	}

	//
	// webrtc::VideoFrameBuffer interface
	//
	Type type() const override
	{
		return Type::kNative;
	}

	virtual int width() const override
	{
		return Frame->GetWidth();
	}

	virtual int height() const override
	{
		return Frame->GetHeight();
	}

	//////////////////////////////////////////////
	// NOTE: Only used for non-hardware encoders
	//////////////////////////////////////////////
	rtc::scoped_refptr<webrtc::I420BufferInterface> ToI420() override
	{
		rtc::scoped_refptr<webrtc::I420Buffer> Buffer = webrtc::I420Buffer::Create(Frame->GetWidth(), Frame->GetHeight());

		// TODO Texture conversion can happen here as we can add a refernce to the Unreal texture to this class rather than doing raw RHI calls
#if 1
		{
			// Slow CPU based path for testing
			uint32 stride;
			uint8* TextureData = (uint8*)GDynamicRHI->RHILockTexture2D(TextureRef, 0, EResourceLockMode::RLM_ReadOnly, stride, true);

			uint32 components = GPixelFormats[TextureRef->GetFormat()].NumComponents;

			// Convert from BGRA to I420
			uint32 height = Frame->GetHeight();
			uint32 width = Frame->GetWidth();

			uint32 image_size = width * height;
			uint32 upos = 0;
			uint32 vpos = 0;
			uint32 i = 0;

			uint8* DataY = Buffer->MutableDataY();
			uint8* DataU = Buffer->MutableDataU();
			uint8* DataV = Buffer->MutableDataV();

			FColor Pixel;

			for (uint32 row = 0; row < TextureRef->GetSizeY(); ++row)
			{
				for (uint32 col = 0; col < TextureRef->GetSizeX(); ++col)
				{
					i = row * stride + col * components;
					Pixel = { TextureData[i+2] , TextureData[i+1], TextureData[i] };
					if (row % 2)
					{
						DataY[row * width + col] = ((66 * Pixel.R + 129 * Pixel.G + 25 * Pixel.B) >> 8) + 16;
						// on every second pixel
						DataU[row / 2 * (width / 2) + col / 2] = ((-38 * Pixel.R + -74 * Pixel.G + 112 * Pixel.B) >> 8) + 128;
						DataV[row / 2 * (width / 2) + col / 2] = ((112 * Pixel.R + -94 * Pixel.G + -18 * Pixel.B) >> 8) + 128;

						col += 1;
						i = row * stride + col * components;
						Pixel = { TextureData[i + 2] , TextureData[i + 1], TextureData[i] };
						DataY[row * width + col] = ((66 * Pixel.R + 129 * Pixel.G + 25 * Pixel.B) >> 8) + 16;
					}
					else
					{
						DataY[row * width + col] = ((66 * Pixel.R + 129 * Pixel.G + 25 * Pixel.B) >> 8) + 16;
					}
				}
			}

			GDynamicRHI->RHIUnlockTexture2D(TextureRef, 0, true);
		}
#else
		// Faster GPU path
		uint8* DataY = Buffer->MutableDataY();
		uint8* DataU = Buffer->MutableDataU();
		uint8* DataV = Buffer->MutableDataV();

		// TODO Write shader that repacks RGBA texture into I420
		FComputeFenceRHIRef fence = GDynamicRHI->RHICreateComputeFence(TEXT("RGBA_to_YUV420_fence"));



		fence.wait();

		// Copy data out of I420 packed into RGBA
		uint8* TextureData = (uint8*)GDynamicRHI->RHILockTexture2D(TextureRef, 0, EResourceLockMode::RLM_ReadOnly, stride, true);

		uint32 size = Frame->GetWidth() * FrameGetHeight();
		uint32 half_size = size / 2;

		FMemory::Memcpy(DataY, TextureData, size);
		FMemory::Memcpy(DataU, TextureData + size, half_size);
		FMemory::Memcpy(DataV, TextureData + half_size, half_size);

		GDynamicRHI->RHIUnlockTexture2D(TextureRef, 0, true);
#endif

		return Buffer;
	}

	AVEncoder::FVideoEncoderInputFrame* GetFrame() const
	{
		return Frame;
	}

	TSharedPtr<AVEncoder::FVideoEncoderInput> GetVideoEncoderInput() const
	{
		return VideoEncoderInput;
	}

private:
	FTexture2DRHIRef TextureRef;
	AVEncoder::FVideoEncoderInputFrame* Frame;
	TSharedPtr<AVEncoder::FVideoEncoderInput> VideoEncoderInput;
};
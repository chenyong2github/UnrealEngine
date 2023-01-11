// Copyright Epic Games, Inc. All Rights Reserved.

#include "RivermaxMediaTextureSample.h"

#include "RenderGraphUtils.h"
#include "RivermaxMediaTextureSampleConverter.h"
#include "RivermaxMediaUtils.h"

FRivermaxMediaTextureSample::FRivermaxMediaTextureSample()
	: TextureConverter(MakeUnique<FRivermaxMediaTextureSampleConverter>())
{
	
}

const FMatrix& FRivermaxMediaTextureSample::GetYUVToRGBMatrix() const
{
	return MediaShaders::YuvToRgbRec709Scaled;
}

IMediaTextureSampleConverter* FRivermaxMediaTextureSample::GetMediaTextureSampleConverter()
{
	return TextureConverter.Get();
}

bool FRivermaxMediaTextureSample::IsOutputSrgb() const
{
	// We always do the sRGB to Linear conversion in shader if specified by the source
	// If true is returned here, MediaTextureResource will create (try) a sRGB texture which only works for 8 bits
	return false;
}

bool FRivermaxMediaTextureSample::ConfigureSample(uint32 InWidth, uint32 InHeight, uint32 InStride, ERivermaxMediaSourcePixelFormat InSampleFormat, FTimespan InTime, const FFrameRate& InFrameRate, const TOptional<FTimecode>& InTimecode, bool bInIsSRGBInput)
{
	EMediaTextureSampleFormat VideoSampleFormat;
	switch (InSampleFormat)
	{
		case ERivermaxMediaSourcePixelFormat::RGB_12bit:
			// Falls through
		case ERivermaxMediaSourcePixelFormat::RGB_16bit_Float:
		{
			VideoSampleFormat = EMediaTextureSampleFormat::FloatRGBA;
			break;
		}
		case ERivermaxMediaSourcePixelFormat::RGB_10bit:
			// Falls through
		case ERivermaxMediaSourcePixelFormat::YUV422_10bit:
		{
			VideoSampleFormat = EMediaTextureSampleFormat::CharBGR10A2;
			break;
		}
		case ERivermaxMediaSourcePixelFormat::YUV422_8bit:
			// Falls through
		case ERivermaxMediaSourcePixelFormat::RGB_8bit:
			// Falls through
		default:
		{
			VideoSampleFormat = EMediaTextureSampleFormat::CharBGRA;
			break;
		}
	}

	TextureConverter->Setup(InSampleFormat, AsShared(), bInIsSRGBInput);
	return Super::SetProperties(InStride, InWidth, InHeight, VideoSampleFormat, InTime, InFrameRate, InTimecode, bInIsSRGBInput);
}

TRefCountPtr<FRDGPooledBuffer> FRivermaxMediaTextureSample::GetGPUBuffer() const
{
	return GPUBuffer;
}

void FRivermaxMediaTextureSample::InitializeGPUBuffer(const FIntPoint& InResolution, ERivermaxMediaSourcePixelFormat InSampleFormat)
{
	using namespace UE::RivermaxMediaUtils::Private;

	const FSourceBufferDesc BufferDescription = GetBufferDescription(InResolution, InSampleFormat);

	FRDGBufferDesc RDGBufferDesc = FRDGBufferDesc::CreateStructuredDesc(BufferDescription.BytesPerElement, BufferDescription.NumberOfElements);
	
	// Required to share resource across different graphics API (DX, Cuda)
	RDGBufferDesc.Usage |= EBufferUsageFlags::Shared;

	TWeakPtr<FRivermaxMediaTextureSample> WeakSample = AsShared();
	ENQUEUE_RENDER_COMMAND(RivermaxPlayerBufferCreation)(
	[WeakSample, RDGBufferDesc](FRHICommandListImmediate& CommandList)
	{
		if (TSharedPtr<FRivermaxMediaTextureSample> Sample = WeakSample.Pin())
		{
			Sample->GPUBuffer = AllocatePooledBuffer(RDGBufferDesc, TEXT("RmaxInput Buffer"));
		}
	});
}


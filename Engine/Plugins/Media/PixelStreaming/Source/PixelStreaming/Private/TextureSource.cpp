// Copyright Epic Games, Inc. All Rights Reserved.

#include "TextureSource.h"
#include "Utils.h"


/*
* -------- UE::PixelStreaming::FTextureSourceBackBuffer ----------------
*/

UE::PixelStreaming::FTextureSourceBackBuffer::FTextureSourceBackBuffer() 
	: TTextureSourceBackBufferBase()
{

};

UE::PixelStreaming::FTextureSourceBackBuffer::FTextureSourceBackBuffer(float InScale) 
	: TTextureSourceBackBufferBase(InScale)
{

};

void UE::PixelStreaming::FTextureSourceBackBuffer::CopyTexture(const FTexture2DRHIRef& SourceTexture, TRefCountPtr<FRHITexture2D> DestTexture, FGPUFenceRHIRef& CopyFence)
{
	UE::PixelStreaming::CopyTexture(SourceTexture, DestTexture, CopyFence);
}

TRefCountPtr<FRHITexture2D> UE::PixelStreaming::FTextureSourceBackBuffer::CreateTexture(int Width, int Height)
{
	return UE::PixelStreaming::CreateTexture(Width, Height);
}

FTexture2DRHIRef UE::PixelStreaming::FTextureSourceBackBuffer::ToTextureRef(TRefCountPtr<FRHITexture2D> Texture)
{
	return Texture;
}

/*
* -------- UE::PixelStreaming::FTextureSourceBackBufferToCPU ----------------
*/

UE::PixelStreaming::FTextureSourceBackBufferToCPU::FTextureSourceBackBufferToCPU() 
	: TTextureSourceBackBufferBase()
{

};

UE::PixelStreaming::FTextureSourceBackBufferToCPU::FTextureSourceBackBufferToCPU(float InScale) 
	: TTextureSourceBackBufferBase(InScale)
{

};

void UE::PixelStreaming::FTextureSourceBackBufferToCPU::CopyTexture(const FTexture2DRHIRef& SourceTexture, TRefCountPtr<UE::PixelStreaming::FRawPixelsTexture> DestTexture, FGPUFenceRHIRef& CopyFence)
{
	UE::PixelStreaming::CopyTexture(SourceTexture, DestTexture->TextureRef, CopyFence);
	UE::PixelStreaming::ReadTextureToCPU(FRHICommandListExecutor::GetImmediateCommandList(), DestTexture->TextureRef, DestTexture->RawPixels);
}

TRefCountPtr<UE::PixelStreaming::FRawPixelsTexture> UE::PixelStreaming::FTextureSourceBackBufferToCPU::CreateTexture(int Width, int Height)
{
	FRawPixelsTexture* Tex = new FRawPixelsTexture(UE::PixelStreaming::CreateTexture(Width, Height));
	return TRefCountPtr<FRawPixelsTexture>(Tex);
}

FTexture2DRHIRef UE::PixelStreaming::FTextureSourceBackBufferToCPU::ToTextureRef(TRefCountPtr<UE::PixelStreaming::FRawPixelsTexture> Texture)
{
	return Texture->TextureRef;
}
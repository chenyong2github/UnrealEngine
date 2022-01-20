// Copyright Epic Games, Inc. All Rights Reserved.

#include "FrameBuffer.h"
#include "TextureSource.h"

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
	return FMath::Max(TextureSources[0]->GetSourceWidth(), TextureSources[TextureSources.Num()-1]->GetSourceWidth());
}

int UE::PixelStreaming::FSimulcastFrameBuffer::height() const
{
	checkf(TextureSources.Num() > 0, TEXT("Must be at least one texture source to get the height from."));
	return FMath::Max(TextureSources[0]->GetSourceHeight(), TextureSources[TextureSources.Num()-1]->GetSourceHeight());
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

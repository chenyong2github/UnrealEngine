// Copyright Epic Games, Inc. All Rights Reserved.
#include "PixelStreamingFrameBuffer.h"
#include "PixelStreamingFrameSource.h"

FPixelStreamingInitializeFrameBuffer::FPixelStreamingInitializeFrameBuffer(FPixelStreamingFrameSource* InFrameSource)
	: FrameSource(InFrameSource)
{
}

FPixelStreamingInitializeFrameBuffer::~FPixelStreamingInitializeFrameBuffer()
{
}

int FPixelStreamingInitializeFrameBuffer::width() const
{
	return FrameSource->GetSourceWidth();
}

int FPixelStreamingInitializeFrameBuffer::height() const
{
	return FrameSource->GetSourceHeight();
}

FPixelStreamingSimulcastFrameBuffer::FPixelStreamingSimulcastFrameBuffer(FPixelStreamingFrameSource* InFrameSource)
	: FrameSource(InFrameSource)
{
}

FPixelStreamingSimulcastFrameBuffer::~FPixelStreamingSimulcastFrameBuffer()
{
}

int FPixelStreamingSimulcastFrameBuffer::GetNumLayers() const
{
	return FrameSource->GetNumLayers();
}

FPixelStreamingLayerFrameSource* FPixelStreamingSimulcastFrameBuffer::GetLayerFrameSource(int LayerIndex) const
{
	return FrameSource->GetLayerFrameSource(LayerIndex);
}

int FPixelStreamingSimulcastFrameBuffer::width() const
{
	return FrameSource->GetSourceWidth();
}

int FPixelStreamingSimulcastFrameBuffer::height() const
{
	return FrameSource->GetSourceHeight();
}

FPixelStreamingLayerFrameBuffer::FPixelStreamingLayerFrameBuffer(FPixelStreamingLayerFrameSource* InLayerFrameSource)
	: LayerFrameSource(InLayerFrameSource)
{
}

FPixelStreamingLayerFrameBuffer::~FPixelStreamingLayerFrameBuffer()
{
}

FTexture2DRHIRef FPixelStreamingLayerFrameBuffer::GetFrame() const
{
	return LayerFrameSource->GetFrame();
}

int FPixelStreamingLayerFrameBuffer::width() const
{
	return LayerFrameSource->GetSourceWidth();
}

int FPixelStreamingLayerFrameBuffer::height() const
{
	return LayerFrameSource->GetSourceHeight();
}

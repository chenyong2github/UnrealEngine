#include "PixelStreamingFrameBuffer.h"

#include "PixelStreamingFrameSource.h"

FPixelStreamingSimulcastFrameBuffer::FPixelStreamingSimulcastFrameBuffer(FPixelStreamingFrameSource* InFrameSource)
:FrameSource(InFrameSource)
{

}

FPixelStreamingSimulcastFrameBuffer::~FPixelStreamingSimulcastFrameBuffer()
{

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
:LayerFrameSource(InLayerFrameSource)
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

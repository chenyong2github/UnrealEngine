// Copyright Epic Games, Inc. All Rights Reserved.

#include "FrameBufferI420.h"
#include "FrameAdapter.h"

namespace UE::PixelStreaming
{
	FFrameBufferI420Base::FFrameBufferI420Base(TSharedPtr<IPixelStreamingAdaptedFrameSource> InFrameSource)
		: FrameSource(InFrameSource)
	{
	}

	FFrameBufferI420Simulcast::FFrameBufferI420Simulcast(TSharedPtr<IPixelStreamingAdaptedFrameSource> InFrameSource)
		: FFrameBufferI420Base(InFrameSource)
	{
	}

	int FFrameBufferI420Simulcast::width() const
	{
		return FrameSource->GetWidth(GetNumLayers() - 1);
	}

	int FFrameBufferI420Simulcast::height() const
	{
		return FrameSource->GetHeight(GetNumLayers() - 1);
	}

	int FFrameBufferI420Simulcast::GetNumLayers() const
	{
		return FrameSource->GetNumLayers();
	}

	FFrameBufferI420::FFrameBufferI420(TSharedPtr<IPixelStreamingAdaptedFrameSource> InFrameSource, int InLayerIndex)
		: FFrameBufferI420Base(InFrameSource)
		, LayerIndex(InLayerIndex)
	{
	}

	int FFrameBufferI420::width() const
	{
		return FrameSource->GetWidth(LayerIndex);
	}

	int FFrameBufferI420::height() const
	{
		return FrameSource->GetHeight(LayerIndex);
	}

	rtc::scoped_refptr<webrtc::I420BufferInterface> FFrameBufferI420::ToI420()
	{
		return GetAdaptedLayer()->GetI420Buffer();
	}

	const webrtc::I420BufferInterface* FFrameBufferI420::GetI420() const
	{
		return GetAdaptedLayer()->GetI420Buffer().get();
	}

	FPixelStreamingAdaptedOutputFrameI420* FFrameBufferI420::GetAdaptedLayer() const
	{
		EnsureCachedAdaptedLayer();
		return CachedAdaptedLayer.Get();
	}

	void FFrameBufferI420::EnsureCachedAdaptedLayer() const
	{
		if (CachedAdaptedLayer == nullptr)
		{
			FFrameAdapter* FrameAdapter = StaticCast<FFrameAdapter*>(FrameSource.Get());
			CachedAdaptedLayer = StaticCastSharedPtr<FPixelStreamingAdaptedOutputFrameI420>(FrameAdapter->ReadOutput(LayerIndex));
			CachedAdaptedLayer->Metadata.Layer = LayerIndex;
		}
	}
} // namespace UE::PixelStreaming

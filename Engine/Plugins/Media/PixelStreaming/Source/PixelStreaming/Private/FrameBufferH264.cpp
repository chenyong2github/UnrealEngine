// Copyright Epic Games, Inc. All Rights Reserved.

#include "FrameBufferH264.h"
#include "FrameAdapterH264.h"

namespace UE::PixelStreaming
{
	FFrameBufferH264Base::FFrameBufferH264Base(TSharedPtr<IPixelStreamingFrameSource> InFrameSource)
		: FrameSource(InFrameSource)
	{
	}

	FFrameBufferH264Simulcast::FFrameBufferH264Simulcast(TSharedPtr<IPixelStreamingFrameSource> InFrameSource)
		: FFrameBufferH264Base(InFrameSource)
	{
	}

	int FFrameBufferH264Simulcast::width() const
	{
		return FrameSource->GetWidth(GetNumLayers() - 1);
	}

	int FFrameBufferH264Simulcast::height() const
	{
		return FrameSource->GetHeight(GetNumLayers() - 1);
	}

	int FFrameBufferH264Simulcast::GetNumLayers() const
	{
		return FrameSource->GetNumLayers();
	}

	FFrameBufferH264::FFrameBufferH264(TSharedPtr<IPixelStreamingFrameSource> InFrameSource, int InLayerIndex)
		: FFrameBufferH264Base(InFrameSource)
		, LayerIndex(InLayerIndex)
	{
	}

	int FFrameBufferH264::width() const
	{
		return FrameSource->GetWidth(LayerIndex);
	}

	int FFrameBufferH264::height() const
	{
		return FrameSource->GetHeight(LayerIndex);
	}

	FAdaptedVideoFrameLayerH264* FFrameBufferH264::GetAdaptedLayer() const
	{
		EnsureCachedAdaptedLayer();
		return CachedAdaptedLayer.Get();
	}

	void FFrameBufferH264::EnsureCachedAdaptedLayer() const
	{
		if (CachedAdaptedLayer == nullptr)
		{
			FPixelStreamingFrameAdapter* FrameAdapter = StaticCast<FPixelStreamingFrameAdapter*>(FrameSource.Get());
			CachedAdaptedLayer = StaticCastSharedPtr<FAdaptedVideoFrameLayerH264>(FrameAdapter->ReadOutput(LayerIndex));
			CachedAdaptedLayer->Metadata.Layer = LayerIndex;
		}
	}
} // namespace UE::PixelStreaming

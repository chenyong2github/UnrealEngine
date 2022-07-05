// Copyright Epic Games, Inc. All Rights Reserved.

#include "FrameBufferH264.h"
#include "FrameAdapter.h"

namespace UE::PixelStreaming
{
	FFrameBufferH264Base::FFrameBufferH264Base(TSharedPtr<IPixelStreamingAdaptedFrameSource> InFrameSource)
		: FrameSource(InFrameSource)
	{
	}

	FFrameBufferH264Simulcast::FFrameBufferH264Simulcast(TSharedPtr<IPixelStreamingAdaptedFrameSource> InFrameSource)
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

	FFrameBufferH264::FFrameBufferH264(TSharedPtr<IPixelStreamingAdaptedFrameSource> InFrameSource, int InLayerIndex)
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

	FPixelStreamingAdaptedOutputFrameH264* FFrameBufferH264::GetAdaptedLayer() const
	{
		EnsureCachedAdaptedLayer();
		return CachedAdaptedLayer.Get();
	}

	void FFrameBufferH264::EnsureCachedAdaptedLayer() const
	{
		if (CachedAdaptedLayer == nullptr)
		{
			FFrameAdapter* FrameAdapter = StaticCast<FFrameAdapter*>(FrameSource.Get());
			CachedAdaptedLayer = StaticCastSharedPtr<FPixelStreamingAdaptedOutputFrameH264>(FrameAdapter->ReadOutput(LayerIndex));
			CachedAdaptedLayer->Metadata.Layer = LayerIndex;
		}
	}
} // namespace UE::PixelStreaming

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PixelStreamingFrameAdapter.h"
#include "PixelStreamingFrameMetadata.h"

namespace UE::PixelStreaming
{
	class FAdaptedVideoFrameLayerH264 : public IPixelStreamingAdaptedVideoFrameLayer
	{
	public:
		FAdaptedVideoFrameLayerH264(FTexture2DRHIRef InFrameTexture)
			: FrameTexture(InFrameTexture)
		{
		}
		virtual ~FAdaptedVideoFrameLayerH264() = default;

		virtual int32 GetWidth() const override { return FrameTexture->GetDesc().Extent.X; }
		virtual int32 GetHeight() const override { return FrameTexture->GetDesc().Extent.Y; }

		FTexture2DRHIRef GetFrameTexture() const { return FrameTexture; }

		FPixelStreamingFrameMetadata Metadata;
		
	private:
		FTexture2DRHIRef FrameTexture;
	};
	
	class FFrameAdapterH264 : public FPixelStreamingFrameAdapter
	{
	public:
		FFrameAdapterH264(TSharedPtr<FPixelStreamingVideoInput> VideoInput);
		FFrameAdapterH264(TSharedPtr<FPixelStreamingVideoInput> VideoInput, TArray<float> LayerScales);
		virtual ~FFrameAdapterH264() = default;

	private:
		virtual TSharedPtr<FPixelStreamingFrameAdapterProcess> CreateAdaptProcess(float Scale) override;
	};
} // namespace UE::PixelStreaming

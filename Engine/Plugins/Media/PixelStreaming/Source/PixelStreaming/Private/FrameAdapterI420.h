// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WebRTCIncludes.h"
#include "PixelStreamingFrameAdapter.h"
#include "PixelStreamingFrameMetadata.h"

namespace UE::PixelStreaming
{
	class FAdaptedVideoFrameLayerI420 : public IPixelStreamingAdaptedVideoFrameLayer
	{
	public:
		FAdaptedVideoFrameLayerI420(rtc::scoped_refptr<webrtc::I420Buffer> InI420Buffer)
			: I420Buffer(InI420Buffer)
		{
		}
		virtual ~FAdaptedVideoFrameLayerI420() = default;

		virtual int32 GetWidth() const override { return I420Buffer->width(); }
		virtual int32 GetHeight() const override { return I420Buffer->height(); }

		rtc::scoped_refptr<webrtc::I420Buffer> GetI420Buffer() const { return I420Buffer; }

		FPixelStreamingFrameMetadata Metadata;
		
	private:
		rtc::scoped_refptr<webrtc::I420Buffer> I420Buffer;
	};
	
	class FFrameAdapterI420 : public FPixelStreamingFrameAdapter
	{
	public:
		FFrameAdapterI420(TSharedPtr<FPixelStreamingVideoInput> VideoInput);
		FFrameAdapterI420(TSharedPtr<FPixelStreamingVideoInput> VideoInput, TArray<float> LayerScales);
		virtual ~FFrameAdapterI420() = default;
	};

	class FFrameAdapterI420CPU : public FFrameAdapterI420
	{
	public:
		FFrameAdapterI420CPU(TSharedPtr<FPixelStreamingVideoInput> VideoInput);
		FFrameAdapterI420CPU(TSharedPtr<FPixelStreamingVideoInput> VideoInput, TArray<float> LayerScales);
		virtual ~FFrameAdapterI420CPU() = default;

	private:
		virtual TSharedPtr<FPixelStreamingFrameAdapterProcess> CreateAdaptProcess(float Scale) override;
	};

	class FFrameAdapterI420Compute : public FFrameAdapterI420
	{
	public:
		FFrameAdapterI420Compute(TSharedPtr<FPixelStreamingVideoInput> VideoInput);
		FFrameAdapterI420Compute(TSharedPtr<FPixelStreamingVideoInput> VideoInput, TArray<float> LayerScales);
		virtual ~FFrameAdapterI420Compute() = default;

	private:
		virtual TSharedPtr<FPixelStreamingFrameAdapterProcess> CreateAdaptProcess(float Scale) override;
	};
} // namespace UE::PixelStreaming

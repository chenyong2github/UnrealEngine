// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IPixelStreamingFrameBuffer.h"

namespace UE::PixelStreaming
{
	class FAdaptedVideoFrameLayerI420;

	class FFrameBufferI420Base : public IPixelStreamingFrameBuffer
	{
	public:
		FFrameBufferI420Base(TSharedPtr<IPixelStreamingFrameSource> InFrameSource);
		virtual ~FFrameBufferI420Base() = default;

	protected:
		TSharedPtr<IPixelStreamingFrameSource> FrameSource;
	};

	class FFrameBufferI420Simulcast : public FFrameBufferI420Base
	{
	public:
		FFrameBufferI420Simulcast(TSharedPtr<IPixelStreamingFrameSource> InFrameSource);
		virtual ~FFrameBufferI420Simulcast() = default;

		virtual EPixelStreamingFrameBufferType GetFrameBufferType() const { return EPixelStreamingFrameBufferType::Simulcast; }

		virtual int width() const override;
		virtual int height() const override;

		TSharedPtr<IPixelStreamingFrameSource> GetFrameAdapter() const { return FrameSource; }
		int GetNumLayers() const;
	};

	/*
	* A single frame buffer for I420 frames
	*/
	class FFrameBufferI420 : public FFrameBufferI420Base
	{
	public:
		FFrameBufferI420(TSharedPtr<IPixelStreamingFrameSource> InFrameSource, int InLayerIndex);
		virtual ~FFrameBufferI420() = default;

		virtual EPixelStreamingFrameBufferType GetFrameBufferType() const { return EPixelStreamingFrameBufferType::Layer; }

		virtual int width() const override;
		virtual int height() const override;

		virtual rtc::scoped_refptr<webrtc::I420BufferInterface> ToI420() override;
		virtual const webrtc::I420BufferInterface* GetI420() const override;

		FAdaptedVideoFrameLayerI420* GetAdaptedLayer() const;

	private:
		int32 LayerIndex;

		// see comments in FrameBufferH264.h
		void EnsureCachedAdaptedLayer() const;
		mutable TSharedPtr<FAdaptedVideoFrameLayerI420> CachedAdaptedLayer;
	};
} // namespace UE::PixelStreaming

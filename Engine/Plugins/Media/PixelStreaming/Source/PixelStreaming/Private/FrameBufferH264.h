// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IPixelStreamingFrameBuffer.h"
#include "RHI.h"

namespace UE::PixelStreaming
{
	class FAdaptedVideoFrameLayerH264;
	
	/*
	 * The base of the H264 frame buffer. Shouldn't be used directly.
	 */
	class FFrameBufferH264Base : public IPixelStreamingFrameBuffer
	{
	public:
		FFrameBufferH264Base(TSharedPtr<IPixelStreamingFrameSource> InFrameSource);
		virtual ~FFrameBufferH264Base() = default;

	protected:
		TSharedPtr<IPixelStreamingFrameSource> FrameSource;
	};

	/*
	 * A frame buffer for simulcast encoders. The adapter held within should provide multiple layers
	 * to be streamed. Should only be used to create single layer frame buffers.
	 */
	class FFrameBufferH264Simulcast : public FFrameBufferH264Base
	{
	public:
		FFrameBufferH264Simulcast(TSharedPtr<IPixelStreamingFrameSource> InFrameSource);
		virtual ~FFrameBufferH264Simulcast() = default;

		virtual EPixelStreamingFrameBufferType GetFrameBufferType() const override { return EPixelStreamingFrameBufferType::Simulcast; }

		virtual int width() const override;
		virtual int height() const override;

		TSharedPtr<IPixelStreamingFrameSource> GetFrameSource() const { return FrameSource; }
		int GetNumLayers() const;
	};

	/*
	 * A frame buffer for single layer encoders. Contains a layer index into the adapter. Should be
	 * used in the encoder.
	 */
	class FFrameBufferH264 : public FFrameBufferH264Base
	{
	public:
		FFrameBufferH264(TSharedPtr<IPixelStreamingFrameSource> InFrameSource, int InLayerIndex = 0);
		virtual ~FFrameBufferH264() = default;

		virtual EPixelStreamingFrameBufferType GetFrameBufferType() const override { return EPixelStreamingFrameBufferType::Layer; }

		virtual int width() const override;
		virtual int height() const override;

		FAdaptedVideoFrameLayerH264* GetAdaptedLayer() const;

	private:
		int LayerIndex;

		// used so we dont call ReadOutput multiple times and possibly flip the read buffer and get a different frame
		// probably not a big deal but i would like to make sure that the buffer always refers to the same frame.
		// this way allows the user to call GetAdaptedLayer() repeatedly and will always get the same frame.
		void EnsureCachedAdaptedLayer() const;
		mutable TSharedPtr<FAdaptedVideoFrameLayerH264> CachedAdaptedLayer;
	};
} // namespace UE::PixelStreaming

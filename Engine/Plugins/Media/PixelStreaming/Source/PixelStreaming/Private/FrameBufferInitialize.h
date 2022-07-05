// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IPixelStreamingFrameBuffer.h"

class IPixelStreamingAdaptedFrameSource;

namespace UE::PixelStreaming
{
	/*
	 * Used only to initialize hardware encoders and query for size. Will never actually provide image data.
	 */
	class FFrameBufferInitialize : public IPixelStreamingFrameBuffer
	{
	public:
		FFrameBufferInitialize(TSharedPtr<IPixelStreamingAdaptedFrameSource> InFrameSource);
		virtual ~FFrameBufferInitialize() = default;

		virtual EPixelStreamingFrameBufferType GetFrameBufferType() const { return EPixelStreamingFrameBufferType::Initialize; }

		virtual int width() const override;
		virtual int height() const override;

	private:
		TSharedPtr<IPixelStreamingAdaptedFrameSource> FrameSource;
	};
} // namespace UE::PixelStreaming

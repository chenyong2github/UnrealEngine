// Copyright Epic Games, Inc. All Rights Reserved.

#include "FrameBufferInitialize.h"
#include "IPixelStreamingAdaptedFrameSource.h"

namespace UE::PixelStreaming
{
	FFrameBufferInitialize::FFrameBufferInitialize(TSharedPtr<IPixelStreamingAdaptedFrameSource> InFrameSource)
		: FrameSource(InFrameSource)
	{
	}

	int FFrameBufferInitialize::width() const
	{
		return FrameSource->GetWidth(0);
	}

	int FFrameBufferInitialize::height() const
	{
		return FrameSource->GetHeight(0);
	}
} // namespace UE::PixelStreaming

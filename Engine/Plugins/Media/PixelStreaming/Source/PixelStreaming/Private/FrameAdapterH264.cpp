// Copyright Epic Games, Inc. All Rights Reserved.

#include "FrameAdapterH264.h"
#include "FrameAdapterProcessH264.h"

namespace UE::PixelStreaming
{
	FFrameAdapterH264::FFrameAdapterH264(TSharedPtr<FPixelStreamingVideoInput> VideoInput)
		: FPixelStreamingFrameAdapter(VideoInput)
	{
	}

	FFrameAdapterH264::FFrameAdapterH264(TSharedPtr<FPixelStreamingVideoInput> VideoInput, TArray<float> LayerScales)
		: FPixelStreamingFrameAdapter(VideoInput, LayerScales)
	{
	}

	TSharedPtr<FPixelStreamingFrameAdapterProcess> FFrameAdapterH264::CreateAdaptProcess(float Scale)
	{
		return MakeShared<FFrameAdapterProcessH264>(Scale);
	}
} // namespace UE::PixelStreaming

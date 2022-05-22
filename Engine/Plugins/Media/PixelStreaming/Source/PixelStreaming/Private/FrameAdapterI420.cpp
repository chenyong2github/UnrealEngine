// Copyright Epic Games, Inc. All Rights Reserved.

#include "FrameAdapterI420.h"
#include "FrameAdapterProcessI420CPU.h"
#include "FrameAdapterProcessI420Compute.h"

namespace UE::PixelStreaming
{
	FFrameAdapterI420::FFrameAdapterI420(TSharedPtr<FPixelStreamingVideoInput> VideoInput)
		: FPixelStreamingFrameAdapter(VideoInput)
	{
	}

	FFrameAdapterI420::FFrameAdapterI420(TSharedPtr<FPixelStreamingVideoInput> VideoInput, TArray<float> LayerScales)
		: FPixelStreamingFrameAdapter(VideoInput, LayerScales)
	{
	}

	FFrameAdapterI420CPU::FFrameAdapterI420CPU(TSharedPtr<FPixelStreamingVideoInput> VideoInput)
		: FFrameAdapterI420(VideoInput)
	{
	}

	FFrameAdapterI420CPU::FFrameAdapterI420CPU(TSharedPtr<FPixelStreamingVideoInput> VideoInput, TArray<float> LayerScales)
		: FFrameAdapterI420(VideoInput, LayerScales)
	{
	}

	TSharedPtr<FPixelStreamingFrameAdapterProcess> FFrameAdapterI420CPU::CreateAdaptProcess(float Scale)
	{
		return MakeShared<FFrameAdapterProcessI420CPU>(Scale);
	}

	FFrameAdapterI420Compute::FFrameAdapterI420Compute(TSharedPtr<FPixelStreamingVideoInput> VideoInput)
		: FFrameAdapterI420(VideoInput)
	{
	}

	FFrameAdapterI420Compute::FFrameAdapterI420Compute(TSharedPtr<FPixelStreamingVideoInput> VideoInput, TArray<float> LayerScales)
		: FFrameAdapterI420(VideoInput, LayerScales)
	{
	}

	TSharedPtr<FPixelStreamingFrameAdapterProcess> FFrameAdapterI420Compute::CreateAdaptProcess(float Scale)
	{
		return MakeShared<FFrameAdapterProcessI420Compute>(Scale);
	}
} // namespace UE::PixelStreaming

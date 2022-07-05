// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Delegates/DelegateCombinations.h"
#include "PixelStreamingFrameAdapterProcess.h"
#include "IPixelStreamingInputFrame.h"

enum class EPixelStreamingFrameBufferFormat
{
	Unknown,
	RHITexture,
	IYUV420,
};

/*
 * Feeds FStreamer with frame data.
 * Broadcast the OnFrame delegate when a new frame is ready to be streamed.
 */
class PIXELSTREAMING_API IPixelStreamingVideoInput
{
public:
	virtual ~IPixelStreamingVideoInput() = default;

	/**
	 * Return a process object that will convert the input frame to the requested output
	 * and scale.
	 * @param FinalFormat The destination buffer format we need for the selected encoder.
	 * @param FinalScale The destination scale for the process.
	 * @return An FPixelStreamingFrameAdapterProcess implementation that handles the conversion of frames.
	 */
	virtual TSharedPtr<FPixelStreamingFrameAdapterProcess> CreateAdaptProcess(EPixelStreamingFrameBufferFormat FinalFormat, float FinalScale) = 0;

	virtual void OnFrame(const IPixelStreamingInputFrame&) = 0;

	/**
	 * Broadcast on this delegate when a frame is ready to be fed to pixel streaming. Once
	 * a frame has been emitted, all frames should be the same dimension. If you need to emit
	 * frames of a new resolution you are required to broadcast on "OnResolutionChanged" first.
	 */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnFrame, const IPixelStreamingInputFrame&);
	FOnFrame OnFrameReady;

	/**
	 * You are required to broadcast on this delegate when the resolution of frames changes.
	 */
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnResolutionChanged, int32, int32);
	FOnResolutionChanged OnResolutionChanged;
};

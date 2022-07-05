// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct PIXELSTREAMING_API FPixelStreamingFrameMetadata
{
	// Identifier for the pipeline this frame took
	FString ProcessName = "Unknown";

	// Frames can be composed of multiple frames
	int32 Layer = 0;

	// The time this frame was sourced
	uint64 SourceTime = 0;

	// Adapt process timings (happens once per frame)
	uint64 AdaptCallTime = 0;
	uint64 AdaptProcessStartTime = 0;
	uint64 AdaptProcessFinalizeTime = 0;
	uint64 AdaptProcessEndTime = 0;

	// Frame use timings (can happen multiple times. ie. we are consuming frames faster than producing them)
	uint32 UseCount = 0;
	uint64 FirstEncodeStartTime = 0;
	uint64 LastEncodeStartTime = 0;
	uint64 LastEncodeEndTime = 0;

	// wanted this to be explicit with a name
	FPixelStreamingFrameMetadata Copy() const { return *this; }
};

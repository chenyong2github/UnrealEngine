// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Delegates/DelegateCombinations.h"
#include "CoreMinimal.h"

class FPixelStreamingSourceFrame;

/*
 * Feeds FStreamer with frame data.
 * Broadcast the OnFrame delegate when a new frame is ready to be streamed.
 */
class PIXELSTREAMING_API FPixelStreamingVideoInput
{
public:
	virtual ~FPixelStreamingVideoInput() = default;

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnFrame, const FPixelStreamingSourceFrame&);
	FOnFrame OnFrame;
};

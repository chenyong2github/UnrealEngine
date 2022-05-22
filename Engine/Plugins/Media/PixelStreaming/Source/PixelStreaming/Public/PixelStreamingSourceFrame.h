// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PixelStreamingFrameMetadata.h"
#include "RHI.h"

/*
 * The unit that FPixelStreamingVideoInput pushes around.
 * Contains the source frame data and also some metadata
 * that can be used for timing metrics.
 */
class PIXELSTREAMING_API FPixelStreamingSourceFrame
{
public:
	FPixelStreamingSourceFrame(FTexture2DRHIRef InFrameTexture)
		: FrameTexture(InFrameTexture)
	{
		Metadata.SourceTime = FPlatformTime::Cycles64();
	}
	virtual ~FPixelStreamingSourceFrame() = default;

	virtual int32 GetWidth() const { return FrameTexture->GetDesc().Extent.X; }
	virtual int32 GetHeight() const { return FrameTexture->GetDesc().Extent.Y; }

	FTexture2DRHIRef FrameTexture;
	FPixelStreamingFrameMetadata Metadata;
};

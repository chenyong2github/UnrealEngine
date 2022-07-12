// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IPixelStreamingInputFrame.h"
#include "RHI.h"

/*
 * The default pixel streamer source frame that contains an RHI texture as input.
 */
class PIXELSTREAMING_API FPixelStreamingInputFrameRHI : public IPixelStreamingInputFrame
{
public:
	FPixelStreamingInputFrameRHI(FTexture2DRHIRef InFrameTexture)
		: FrameTexture(InFrameTexture)
	{
		Metadata.SourceTime = FPlatformTime::Cycles64();
	}
	virtual ~FPixelStreamingInputFrameRHI() = default;

	virtual int32 GetType() const override { return static_cast<int32>(EPixelStreamingInputFrameType::RHI); }
	virtual int32 GetWidth() const override { return FrameTexture->GetDesc().Extent.X; }
	virtual int32 GetHeight() const override { return FrameTexture->GetDesc().Extent.Y; }

	FTexture2DRHIRef FrameTexture;
};

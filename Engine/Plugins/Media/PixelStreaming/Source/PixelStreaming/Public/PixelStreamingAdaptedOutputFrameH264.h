// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IPixelStreamingAdaptedOutputFrame.h"
#include "RHI.h"

class FPixelStreamingAdaptedOutputFrameH264 : public IPixelStreamingAdaptedOutputFrame
{
public:
	FPixelStreamingAdaptedOutputFrameH264(FTexture2DRHIRef InFrameTexture)
		: FrameTexture(InFrameTexture)
	{
	}
	virtual ~FPixelStreamingAdaptedOutputFrameH264() = default;

	virtual int32 GetWidth() const override { return FrameTexture->GetDesc().Extent.X; }
	virtual int32 GetHeight() const override { return FrameTexture->GetDesc().Extent.Y; }

	FTexture2DRHIRef GetFrameTexture() const { return FrameTexture; }

private:
	FTexture2DRHIRef FrameTexture;
};

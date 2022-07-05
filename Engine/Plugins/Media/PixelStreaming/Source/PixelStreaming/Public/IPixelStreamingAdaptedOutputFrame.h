// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PixelStreamingFrameMetadata.h"

/**
 * Wraps the output of the adapt process. Extend this for your own result types.
 * You must implement GetWidth and GetHeight to return the width and height of the
 * frame. Add your own method to extract the adapted data.
 */
class IPixelStreamingAdaptedOutputFrame
{
public:
	virtual ~IPixelStreamingAdaptedOutputFrame() = default;

	virtual int32 GetWidth() const = 0;
	virtual int32 GetHeight() const = 0;

    /**
     * Internal structure that contains various bits of information about the adapt and encode process
     */
    FPixelStreamingFrameMetadata Metadata;
};

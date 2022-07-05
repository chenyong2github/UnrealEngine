// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

/*
 * Frame buffers will contain a frame source. This could be a static buffer
 * of data for the frame or an object which pulls the data from elsewhere.
 * Implement this interface and create your frame buffer object by implementing
 * IPixelStreamingFrameBuffer.
 * Could contain multiple layers.
 */
class IPixelStreamingAdaptedFrameSource
{
public:
	virtual ~IPixelStreamingAdaptedFrameSource() = default;

	/*
	 * Returns true when the adapter has frames ready to read.
	 */
	virtual bool IsReady() const = 0;

	/*
	 * Gets the number of layers in the adapted output.
	 */
	virtual int32 GetNumLayers() const = 0;

	/*
	 * Gets the output frame width of the given index.
	 */
	virtual int32 GetWidth(int LayerIndex) const = 0;

	/*
	 * Gets the output frame height of the given index.
	 */
	virtual int32 GetHeight(int LayerIndex) const = 0;
};

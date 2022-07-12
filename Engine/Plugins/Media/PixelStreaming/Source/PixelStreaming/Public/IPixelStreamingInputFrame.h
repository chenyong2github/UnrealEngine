// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PixelStreamingFrameMetadata.h"
#include "HAL/PlatformTime.h"

/**
 * Extensions to source frame need to return a valid ID to GetType(). When writing extensions
 * implement your own enum with your first value set to User from here so that all extensions
 * have unique type values. eg.
 * enum EExtraSourceFrameType
 * {
 *     NewType1 = EPixelStreamingInputFrameType::User,
 *     NewType2
 * }
 */
enum class EPixelStreamingInputFrameType : int32
{
	Unknown,
	RHI,

	User = 128
};

/**
 * The base interface that is fed into the adapter processes for the encoder. It should
 * contain the actual frame data the process needs to adapt a frame to the requested format.
 */
class PIXELSTREAMING_API IPixelStreamingInputFrame
{
public:
	IPixelStreamingInputFrame() { Metadata.SourceTime = FPlatformTime::Cycles64(); }
	virtual ~IPixelStreamingInputFrame() = default;

	/**
	 * Should return a unique type id from either EPixelStreamingInputFrameType or an
	 * extended user implemented enum. Value crashes could result in incorrect/unsafe casting.
	 * @return A unique id value for the implementation of this interface.
	 */
	virtual int32 GetType() const = 0;

	/**
	 * Gets the width of the input frame.
	 * @return The pixel width of the input frame.
	 */
	virtual int32 GetWidth() const = 0;

	/**
	 * Gets the height of the input frame.
	 * @return The pixel height of the input frame.
	 */
	virtual int32 GetHeight() const = 0;

	/**
	 * Internal structure that contains various bits of information about the adapt and encode process
	 */
	FPixelStreamingFrameMetadata Metadata;
};

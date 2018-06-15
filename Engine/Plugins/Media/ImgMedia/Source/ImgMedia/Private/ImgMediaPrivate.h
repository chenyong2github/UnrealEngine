// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ImgMediaSettings.h"
#include "Logging/LogMacros.h"
#include "Stats/Stats.h"
#include "UObject/NameTypes.h"

class FQueuedThreadPool;


/** The OpenEXR image reader is supported on macOS, Windows, and Unix only. */
#define IMGMEDIA_EXR_SUPPORTED_PLATFORM (PLATFORM_MAC || PLATFORM_WINDOWS || PLATFORM_UNIX)

/** Whether to use a separate thread pool for image frame deallocations. */
#define USE_IMGMEDIA_DEALLOC_POOL UE_BUILD_DEBUG


/** Log category for the this module. */
DECLARE_LOG_CATEGORY_EXTERN(LogImgMedia, Log, All);


#if USE_IMGMEDIA_DEALLOC_POOL
	/** Thread pool used for deleting image frame buffers. */
	extern FQueuedThreadPool* GImgMediaThreadPoolSlow;
#endif


namespace ImgMedia
{
	/** Default frame rate for image sequences (24 fps). */
	static const FFrameRate DefaultFrameRate(24, 1);

	/** Name of the FramesRateOverrideDenonimator media option. */
	static const FName FrameRateOverrideDenonimatorOption("FrameRateOverrideDenonimator");

	/** Name of the FramesRateOverrideNumerator media option. */
	static const FName FrameRateOverrideNumeratorOption("FrameRateOverrideNumerator");

	/** Name of the ProxyOverride media option. */
	static const FName ProxyOverrideOption("ProxyOverride");
}

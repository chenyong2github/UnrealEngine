// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WebRTCIncludes.h"

// TODO: Add back in when we implement decoder.
// #if PLATFORM_WINDOWS || PLATFORM_XBOXONE
// #include "WmfIncludes.h"
// #endif


#include "VideoEncoder.h"
typedef AVEncoder::FVideoEncoder FVideoEncoder;

#include "HAL/IConsoleManager.h"
#include "Logging/LogMacros.h"

DECLARE_LOG_CATEGORY_EXTERN(PixelStreaming, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(PixelStreamer, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(PixelPlayer, Log, All);

using FPlayerId = FString;

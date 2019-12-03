// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WebRTCIncludes.h"
#include "Codecs/WmfIncludes.h"
#include "AVEncoder.h"
#include "HAL/IConsoleManager.h"
#include "Logging/LogMacros.h"

DECLARE_LOG_CATEGORY_EXTERN(PixelStreaming, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(PixelStreamer, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(PixelPlayer, Log, All);

using FPlayerId = uint32;

//
// Since a given encoder is shared between sessions, we need a place to put together
// the encoder and any state that should not be repeated per session
struct FHWEncoderDetails
{
	// Max FPS when Pixel streaming was initialized.
	// We need to keep this, so we can adjust framerate at runtime to keep quality without increasing bitrate
	uint32 InitialMaxFPS;
	uint32 LastBitrate = 0;
	uint32 LastFramerate = 0;
	int LastQP = -1;
	FString LastRcMode = TEXT("");
	TUniquePtr<AVEncoder::FVideoEncoder> Encoder;
};


// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Engine/Engine.h"
#include "MagicLeapAudioTypes.generated.h"

USTRUCT()
struct FMagicLeapAudioDummyStruct { GENERATED_BODY() };

/** Delegate used to notify that an audio device has been plugged into the audio jack. */
DECLARE_DYNAMIC_DELEGATE(FMagicLeapAudioJackPluggedDelegate);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMagicLeapAudioJackPluggedDelegateMulti);

/** Delegate used to notify that an audio device has been unplugged from the audio jack. */
DECLARE_DYNAMIC_DELEGATE(FMagicLeapAudioJackUnpluggedDelegate);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMagicLeapAudioJackUnpluggedDelegateMulti);

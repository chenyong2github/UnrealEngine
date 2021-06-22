// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

#include "OnlineSubsystemTwitch.h"
#include "OnlineSubsystemTwitchModule.h"

/** pre-pended to all Twitch logging */
#undef ONLINE_LOG_PREFIX
#define ONLINE_LOG_PREFIX TEXT("TWITCH: ")


// from OnlineSubsystemTypes.h
PRAGMA_DISABLE_DEPRECATION_WARNINGS
TEMP_UNIQUENETIDSTRING_SUBCLASS(FUniqueNetIdTwitch, TWITCH_SUBSYSTEM);
PRAGMA_ENABLE_DEPRECATION_WARNINGS

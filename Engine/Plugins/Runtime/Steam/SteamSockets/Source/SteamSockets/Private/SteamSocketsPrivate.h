// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// To make less changes to the Steam OSS, we'll define this here
// This will allow us to easily snap into the Steam OSS code
#ifndef STEAM_URL_PREFIX
#define STEAM_URL_PREFIX TEXT("steam.")
#endif

THIRD_PARTY_INCLUDES_START

// Main headers
#include "steam/steam_api.h"
#include "steam/steam_gameserver.h"

// Socket specific headers
#include "steam/isteamnetworkingsockets.h"
#include "steam/isteamnetworkingutils.h"
#include "steam/steamnetworkingtypes.h"

THIRD_PARTY_INCLUDES_END

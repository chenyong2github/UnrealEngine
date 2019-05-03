// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreTypes.h"

#if PLATFORM_PS4
#include "PS4/PS4Splash.h"
#elif PLATFORM_XBOXONE
#include "XboxOne/XboxOneSplash.h"
#elif PLATFORM_ANDROID
#include "Android/AndroidSplash.h"
#else
#include COMPILED_PLATFORM_HEADER(PlatformSplash.h)
#endif

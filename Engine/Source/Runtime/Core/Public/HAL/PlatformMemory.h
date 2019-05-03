// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreTypes.h"
#include "GenericPlatform/GenericPlatformMemory.h"

#if PLATFORM_PS4

#if USE_NEW_PS4_MEMORY_SYSTEM
#include "PS4/PS4Memory2.h"
#else
#include "PS4/PS4Memory.h"
#endif
#elif PLATFORM_XBOXONE
#include "XboxOne/XboxOneMemory.h"
#elif PLATFORM_ANDROID
#include "Android/AndroidMemory.h"
#elif PLATFORM_UNIX
#include "Unix/UnixPlatformMemory.h"
#else
#include COMPILED_PLATFORM_HEADER(PlatformMemory.h)
#endif

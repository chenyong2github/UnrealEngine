// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreTypes.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsPlatformStackWalk.h"
#elif PLATFORM_PS4
#include "PS4/PS4StackWalk.h"
#elif PLATFORM_XBOXONE
#include "XboxOne/XboxOneStackWalk.h"
#elif PLATFORM_MAC
#include "Apple/ApplePlatformStackWalk.h"
#elif PLATFORM_IOS
#include "Apple/ApplePlatformStackWalk.h"
#elif PLATFORM_ANDROID
#include "Android/AndroidPlatformStackWalk.h"
#elif PLATFORM_HTML5
#include "HTML5/HTML5PlatformStackWalk.h"
#elif PLATFORM_UNIX
#include "Unix/UnixPlatformStackWalk.h"
#elif PLATFORM_SWITCH
#include "Switch/SwitchPlatformStackWalk.h"
// @ATG_CHANGE : BEGIN HoloLens support
#elif PLATFORM_HOLOLENS
#include "HoloLens/HoloLensStackWalk.h"
// @ATG_CHANGE : END
#else
#error Unknown platform
#endif

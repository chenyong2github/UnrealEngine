// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

//On all un-implemented platforms, just uses GenericPlatform/GenericPlatformBackgroundHttp.h

#if PLATFORM_WINDOWS
#include "Windows/WindowsPlatformBackgroundHttp.h"
#elif PLATFORM_PS4
#include "PS4/PS4PlatformBackgroundHttp.h"
#elif PLATFORM_XBOXONE
#include "XboxOne/XboxOnePlatformBackgroundHttp.h"
#elif PLATFORM_MAC
#include "Mac/MacPlatformBackgroundHttp.h" // These 2 are a little confusing right now. Only thing holding back Mac is an implementation
#elif PLATFORM_IOS                         // of FBackgroundURLSessionHandler. For now everything is setup generically "Apple" so that
#include "IOS/ApplePlatformBackgroundHttp.h" // we can transition easier if we decide to implement this on Mac. So for now ApplePlatformX is actually only iOS in background http
#elif PLATFORM_ANDROID
#include "Android/AndroidPlatformBackgroundHttp.h"
#elif PLATFORM_HTML5
#include "HTML5/HTML5PlatformBackgroundHttp.h"
#elif PLATFORM_UNIX
#include "Unix/UnixPlatformBackgroundHttp.h"
#elif PLATFORM_SWITCH
#include "Switch/SwitchPlatformBackgroundHttp.h"
#else
#error Unknown platform
#endif

// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

//On all un-implemented platforms, just uses GenericPlatform/GenericPlatformBackgroundHttp.h

#if PLATFORM_IOS                         // of FBackgroundURLSessionHandler. For now everything is setup generically "Apple" so that
#include "IOS/ApplePlatformBackgroundHttp.h" // we can transition easier if we decide to implement this on Mac. So for now ApplePlatformX is actually only iOS in background http
#elif PLATFORM_UNIX
#include "Unix/UnixPlatformBackgroundHttp.h"
#else
#include COMPILED_PLATFORM_HEADER(PlatformBackgroundHttp.h)
#endif

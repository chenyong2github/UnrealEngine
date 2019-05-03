// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#if PLATFORM_XBOXONE
#include "XboxOne/XboxOneHttp.h"
#elif PLATFORM_MAC
#include "Apple/ApplePlatformHttp.h"
#elif PLATFORM_IOS
#include "Apple/ApplePlatformHttp.h"
#elif PLATFORM_ANDROID
#include "Android/AndroidHttp.h"
#elif PLATFORM_UNIX
#include "Unix/UnixPlatformHttp.h"
#else
#include COMPILED_PLATFORM_HEADER(PlatformHttp.h)
#endif

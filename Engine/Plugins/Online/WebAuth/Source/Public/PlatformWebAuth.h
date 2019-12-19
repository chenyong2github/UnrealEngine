// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#if PLATFORM_WINDOWS
#include "NullPlatformWebAuth.h"
#elif PLATFORM_PS4
#include "NullPlatformWebAuth.h"
#elif PLATFORM_XBOXONE
#include "NullPlatformWebAuth.h"
#elif PLATFORM_MAC
#include "NullPlatformWebAuth.h"
#elif PLATFORM_IOS
#include "IOS/IOSPlatformWebAuth.h"
#elif PLATFORM_ANDROID
#include "Android/AndroidPlatformWebAuth.h"
#elif PLATFORM_UNIX
#include "NullPlatformWebAuth.h"
#elif PLATFORM_SWITCH
#include "NullPlatformWebAuth.h"
#else
#error Unknown platform
#endif


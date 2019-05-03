// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreTypes.h"

#if PLATFORM_PS4
#include "PS4/PS4String.h"
#elif PLATFORM_XBOXONE
#include "XboxOne/XboxOneString.h"
#elif PLATFORM_MAC
#include "Apple/ApplePlatformString.h"
#elif PLATFORM_IOS
#include "Apple/ApplePlatformString.h"
#elif PLATFORM_ANDROID
#include "Android/AndroidString.h"
#elif PLATFORM_UNIX
#include "Unix/UnixPlatformString.h"
#else
#include COMPILED_PLATFORM_HEADER(PlatformString.h)
#endif


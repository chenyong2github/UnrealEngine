// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "GenericPlatform/GenericPlatformTLS.h"

#if PLATFORM_PS4
#include "PS4/PS4TLS.h"
#elif PLATFORM_XBOXONE
#include "XboxOne/XboxOneTLS.h"
#elif PLATFORM_MAC
#include "Apple/ApplePlatformTLS.h"
#elif PLATFORM_IOS
#include "Apple/ApplePlatformTLS.h"
#elif PLATFORM_ANDROID
#include "Android/AndroidTLS.h"
#elif PLATFORM_UNIX
#include "Unix/UnixPlatformTLS.h"
#else
#include COMPILED_PLATFORM_HEADER(PlatformTLS.h)
#endif

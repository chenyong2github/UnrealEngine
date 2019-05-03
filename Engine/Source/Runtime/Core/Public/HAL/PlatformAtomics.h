// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "GenericPlatform/GenericPlatformAtomics.h"

#if PLATFORM_PS4
#include "PS4/PS4Atomics.h"
#elif PLATFORM_XBOXONE
#include "XboxOne/XboxOneAtomics.h"
#elif PLATFORM_MAC
#include "Apple/ApplePlatformAtomics.h"
#elif PLATFORM_IOS
#include "Apple/ApplePlatformAtomics.h"
#elif PLATFORM_ANDROID
#include "Android/AndroidAtomics.h"
#elif PLATFORM_UNIX
#include "Unix/UnixPlatformAtomics.h"
#else
#include COMPILED_PLATFORM_HEADER(PlatformAtomics.h)
#endif

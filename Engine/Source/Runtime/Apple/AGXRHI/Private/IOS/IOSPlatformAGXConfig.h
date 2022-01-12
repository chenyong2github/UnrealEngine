// Copyright Epic Games, Inc. All rights reserved.

#pragma once

#include "HAL/Platform.h"

#if PLATFORM_TVOS
#include "../TVOS/TVOSPlatformAGXConfig.h"
#else

class FIOSAGXConfig
{
public:
	static void PopulateRHIGlobals();
};

typedef FIOSAGXConfig FPlatformAGXConfig;

#endif // PLATFORM_TVOS

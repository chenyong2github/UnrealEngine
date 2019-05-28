// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================================
	HoloLensString.h: HoloLens platform string classes, mostly implemented with ANSI C++
==============================================================================================*/

#pragma once
#include "GenericPlatform/MicrosoftPlatformString.h"
#include "HAL/Platform.h"

/**
 * HoloLens string implementation
 */
struct FHoloLensString : public FMicrosoftPlatformString
{
};

typedef FHoloLensString FPlatformString;

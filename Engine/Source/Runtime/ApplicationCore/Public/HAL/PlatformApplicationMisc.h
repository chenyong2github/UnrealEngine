// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

#if PLATFORM_PS4
#include "PS4/PS4ApplicationMisc.h"
#elif PLATFORM_XBOXONE
#include "XboxOne/XboxOneApplicationMisc.h"
#elif PLATFORM_ANDROID
#include "Android/AndroidApplicationMisc.h"
#else
#include COMPILED_PLATFORM_HEADER(PlatformApplicationMisc.h)
#endif

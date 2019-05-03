// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

#if PLATFORM_PS4
#include "PS4/PS4Process.h"
#elif PLATFORM_XBOXONE
#include "XboxOne/XboxOneProcess.h"
#elif PLATFORM_ANDROID
#include "Android/AndroidProcess.h"
#else
#include COMPILED_PLATFORM_HEADER(PlatformProcess.h)
#endif

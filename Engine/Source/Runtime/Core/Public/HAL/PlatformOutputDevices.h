// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreTypes.h"

#if PLATFORM_PS4
#include "PS4/PS4OutputDevices.h"
#elif PLATFORM_XBOXONE
#include "XboxOne/XboxOneOutputDevices.h"
#elif PLATFORM_ANDROID
#include "Android/AndroidOutputDevices.h"
#elif PLATFORM_UNIX
#include "Unix/UnixPlatformOutputDevices.h"
#else
#include COMPILED_PLATFORM_HEADER(PlatformOutputDevices.h)
#endif

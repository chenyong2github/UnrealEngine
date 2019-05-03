// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreTypes.h"
#include "GenericPlatform/GenericPlatformFile.h"

#if PLATFORM_PS4
#include "PS4/PS4File.h"
#elif PLATFORM_XBOXONE
#include "XboxOne/XboxOneFile.h"
#elif PLATFORM_MAC
#include "Apple/ApplePlatformFile.h"
#elif PLATFORM_IOS
#include "Apple/ApplePlatformFile.h"
#elif PLATFORM_ANDROID
#include "Android/AndroidFile.h"
#elif PLATFORM_HTML5
//#include "HTML5PlatformFile.h"
#elif PLATFORM_LINUX
#include "Linux/LinuxPlatformFile.h"
#else
#include COMPILED_PLATFORM_HEADER(PlatformFile.h)
#endif

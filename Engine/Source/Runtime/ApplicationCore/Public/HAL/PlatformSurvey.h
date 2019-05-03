// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreTypes.h"

#if PLATFORM_PS4
#include "PS4/PS4Survey.h"
#elif PLATFORM_XBOXONE
#include "XboxOne/XboxOneSurvey.h"
#elif PLATFORM_ANDROID
#include "Android/AndroidSurvey.h"
#elif PLATFORM_UNIX
#include "Unix/UnixPlatformSurvey.h"
#else
#include COMPILED_PLATFORM_HEADER(PlatformSurvey.h)
#endif


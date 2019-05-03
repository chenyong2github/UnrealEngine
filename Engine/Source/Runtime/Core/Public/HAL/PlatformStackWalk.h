// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreTypes.h"

#if PLATFORM_PS4
#include "PS4/PS4StackWalk.h"
#elif PLATFORM_XBOXONE
#include "XboxOne/XboxOneStackWalk.h"
#else
#include COMPILED_PLATFORM_HEADER(PlatformStackWalk.h)
#endif

// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#if PLATFORM_WINDOWS
	#include "Windows/WindowsHWrapper.h"
#elif PLATFORM_HOLOLENS
	#include "HoloLens/WindowsHWrapper.h"
#else
    #include "Microsoft/WindowsHWrapperPrivate.h"
#endif

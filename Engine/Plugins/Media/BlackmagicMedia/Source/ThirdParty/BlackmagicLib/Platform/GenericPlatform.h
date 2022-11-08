// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if _WINDOWS
	#include "Win/WinPlatform.h"
#elif __linux__
	#include "Linux/LinuxPlatform.h"
#else
	#error "The platform is not specified or not defined"
#endif

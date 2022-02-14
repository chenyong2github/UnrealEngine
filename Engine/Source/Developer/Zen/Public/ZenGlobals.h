// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"

#ifndef UE_WITH_ZEN
#	if PLATFORM_WINDOWS
#		define UE_WITH_ZEN 1
#	else
#		define UE_WITH_ZEN 0
#	endif
#endif

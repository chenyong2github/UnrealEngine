// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// This header configures the compile-time settings used by LLM, based on defines from the compilation environment.
// This header can be read from c code, so it should not include any c++ code, or any headers that include c++ code.

#include "Misc/Build.h"

#ifndef ALLOW_LOW_LEVEL_MEM_TRACKER_IN_TEST
	#define ALLOW_LOW_LEVEL_MEM_TRACKER_IN_TEST 0
#endif

// *** When locally instrumenting, override this definition of LLM_ENABLED_IN_CONFIG
#define LLM_ENABLED_IN_CONFIG ( \
	!UE_BUILD_SHIPPING && (!UE_BUILD_TEST || ALLOW_LOW_LEVEL_MEM_TRACKER_IN_TEST) && \
	WITH_ENGINE \
)

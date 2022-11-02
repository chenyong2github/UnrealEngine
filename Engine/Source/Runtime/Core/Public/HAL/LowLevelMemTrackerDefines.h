// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// This header configures the compile-time settings used by LLM, based on defines from the compilation environment.
// This header can be read from c code, so it should not include any c++ code, or any headers that include c++ code.

#include "Misc/Build.h"

#ifndef ALLOW_LOW_LEVEL_MEM_TRACKER_IN_TEST
	#define ALLOW_LOW_LEVEL_MEM_TRACKER_IN_TEST 0
#endif

// TODO: If the initialization of PLATFORM_USES_FIXED_GMalloc_CLASS in Platform.h ever changes, move it to a separate
// header and include that header instead of duplicating.
#ifndef PLATFORM_USES_FIXED_GMalloc_CLASS
	#define PLATFORM_USES_FIXED_GMalloc_CLASS 0
#endif

#ifndef PLATFORM_SUPPORTS_LLM
	#define PLATFORM_SUPPORTS_LLM 1
#endif

// LLM is currently incompatible with PLATFORM_USES_FIXED_GMalloc_CLASS, because LLM is activated way too early
// This is not a problem, because fixed GMalloc is only used in Test/Shipping builds
#define LLM_ENABLED_ON_PLATFORM (PLATFORM_SUPPORTS_LLM && !PLATFORM_USES_FIXED_GMalloc_CLASS)

// *** When locally instrumenting, override this definition of ENABLE_LOW_LEVEL_MEM_TRACKER
#if !defined(ENABLE_LOW_LEVEL_MEM_TRACKER) || !LLM_ENABLED_ON_PLATFORM 
	#undef ENABLE_LOW_LEVEL_MEM_TRACKER
	#define ENABLE_LOW_LEVEL_MEM_TRACKER ( \
		LLM_ENABLED_ON_PLATFORM && \
		!UE_BUILD_SHIPPING && (!UE_BUILD_TEST || ALLOW_LOW_LEVEL_MEM_TRACKER_IN_TEST) && \
		WITH_ENGINE \
	)
#endif

#if ENABLE_LOW_LEVEL_MEM_TRACKER

// Public defines configuring LLM; see also private defines in LowLevelMemTracker.cpp

// LLM_ALLOW_ASSETS_TAGS: Set to 1 to enable run-time toggling of AssetTags reporting, 0 to disable.
// Enabling the define causes extra cputime costs to track costs even when AssetTags are toggled off.
// When defined on, the feature can be toggled on at runtime with commandline -llmtagsets=assets.
// Toggling the feature on causes a huge number of stat ids to be created and has a high cputime cost.
// When defined on and runtime-toggled on, AssetTags report the asset that is in scope for each allocation
// LLM Assets can be viewed in game using 'Stat LLMAssets'.
#ifndef LLM_ALLOW_ASSETS_TAGS
	#define LLM_ALLOW_ASSETS_TAGS 0
#endif

// LLM_ALLOW_STATS: Set to 1 to allow stats to be used as tags, 0 to disable.
// When enabled LLM_SCOPED_TAG_WITH_STAT macros are enabled and create an LLM tag per stat at the cost of more LLM
// memory usage per allocation. Turning this on uses the same amount of memory per allocation as LLM_ALLOW_NAMES_TAGS.
// Turning both of them on has no extra cost.
#ifndef LLM_ALLOW_STATS
	#define LLM_ALLOW_STATS 0
#endif

// Enable stat tags if: (1) Stats or (2) Asset tags are allowed (asset tags use the stat macros to record asset scopes)
#define LLM_ENABLED_STAT_TAGS (LLM_ALLOW_STATS || LLM_ALLOW_ASSETS_TAGS)

#endif // ENABLE_LOW_LEVEL_MEM_TRACKER

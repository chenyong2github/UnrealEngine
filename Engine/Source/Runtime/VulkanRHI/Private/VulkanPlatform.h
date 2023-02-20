// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/PreprocessorHelpers.h"

// Vulkan is currently organized just a little differently than COMPILED_PLATFORM_HEADER does things.
#if PLATFORM_IS_EXTENSION
	#define VULKAN_COMPILED_PLATFORM_HEADER(Suffix) PREPROCESSOR_TO_STRING(Suffix)
#else
	#define VULKAN_COMPILED_PLATFORM_HEADER(Suffix) PREPROCESSOR_TO_STRING(PLATFORM_HEADER_NAME/Suffix)
#endif

#include VULKAN_COMPILED_PLATFORM_HEADER(VulkanPlatformDefines.h)

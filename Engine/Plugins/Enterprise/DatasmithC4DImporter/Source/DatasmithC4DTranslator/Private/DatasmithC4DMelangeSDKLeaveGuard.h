// Copyright Epic Games, Inc. All Rights Reserved.

#if PLATFORM_WINDOWS

__pragma(warning(pop))

// Restore some preprocessor identifiers.
__pragma(pop_macro("PI"))
__pragma(pop_macro("BYTE_MAX"))
__pragma(pop_macro("_HAS_EXCEPTIONS"))

// Leave Datasmith platform include guard.
#include "Windows/HideWindowsPlatformTypes.h"
#endif

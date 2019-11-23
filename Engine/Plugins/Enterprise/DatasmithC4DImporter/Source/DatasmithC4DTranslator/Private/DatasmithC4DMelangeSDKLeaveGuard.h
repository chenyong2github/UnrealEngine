// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#if PLATFORM_WINDOWS

__pragma(warning(pop))

// Restore some preprocessor identifiers.
__pragma(pop_macro("PI"))
__pragma(pop_macro("BYTE_MAX"))

// Leave Datasmith platform include guard.
#include "Windows/HideWindowsPlatformTypes.h"
#endif

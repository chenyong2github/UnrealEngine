// Copyright Epic Games, Inc. All Rights Reserved.

// Enter Datasmith platform include gard.
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"

// Back up some preprocessor identifiers.
__pragma(push_macro("PI"))
__pragma(push_macro("BYTE_MAX"))
__pragma(push_macro("_HAS_EXCEPTIONS"))

#undef PI

// Make Melange private_symbols.h build.
#undef BYTE_MAX

__pragma(warning(push))
__pragma(warning(disable: 6297)) /* melange\20.004_rbmelange20.0_259890\includes\c4d_drawport.h(276): Arithmetic overflow:  32-bit value is shifted, then cast to 64-bit value.  Results might not be an expected value. */

#endif // PLATFORM_WINDOWS

#ifdef __clang__
_Pragma( "clang diagnostic push" )
_Pragma( "clang diagnostic ignored \"-Wdeprecated-declarations\"" )
#endif



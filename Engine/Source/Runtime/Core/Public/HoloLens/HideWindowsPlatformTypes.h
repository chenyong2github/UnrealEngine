// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	HideWindowsPlatformTypes.h: Defines for hiding Windows type names.
=============================================================================*/

#ifdef WINDOWS_PLATFORM_TYPES_GUARD
	#undef WINDOWS_PLATFORM_TYPES_GUARD
#else
	#error Mismatched HideWindowsPLatformTypes.h detected.
#endif

#undef INT
#undef UINT
#undef DWORD
#undef FLOAT

// Copyright Epic Games, Inc. All Rights Reserved.

#ifdef WINDOWS_PLATFORM_TYPES_GUARD
	#undef WINDOWS_PLATFORM_TYPES_GUARD
#else
	#error Mismatched HideWindowsPlatformTypes.h detected.
#endif

#undef INT
#undef UINT
#undef DWORD
#undef FLOAT

#ifdef TRUE
	#undef TRUE
#endif

#ifdef FALSE
	#undef FALSE
#endif

#pragma warning( pop )

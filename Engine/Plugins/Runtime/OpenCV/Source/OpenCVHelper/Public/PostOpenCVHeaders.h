// Copyright Epic Games, Inc. All Rights Reserved.

#ifdef OPENCV_HEADERS_TYPES_GUARD
#undef OPENCV_HEADERS_TYPES_GUARD
#else
#error Mismatched PreOpenCVHeadersTypes.h detected.
#endif

#if PLATFORM_WINDOWS

__pragma(warning(pop))
UE_POP_MACRO("check")
THIRD_PARTY_INCLUDES_END

#elif PLATFORM_LINUX

#pragma warning(pop)
UE_POP_MACRO("check")
THIRD_PARTY_INCLUDES_END

#else

UE_POP_MACRO("check")
THIRD_PARTY_INCLUDES_END

#endif
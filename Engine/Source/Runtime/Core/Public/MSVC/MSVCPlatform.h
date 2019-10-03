// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*================================================================================
	MSVCPlatform.h: Setup for any MSVC-using platform
==================================================================================*/

#pragma once

#if _MSC_FULL_VER >= 191125507 && defined(_MSVC_LANG) && _MSVC_LANG >= 201402
	#define PLATFORM_COMPILER_HAS_IF_CONSTEXPR 1
#else
	#define PLATFORM_COMPILER_HAS_IF_CONSTEXPR 0
#endif

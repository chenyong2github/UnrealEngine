// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VirtualShadowMapConfig.h
=============================================================================*/

#pragma once

#if defined(GPUCULL_TODO)
	#define ENABLE_NON_NANITE_VSM 1
#else // !defined(GPUCULL_TODO)
	#define ENABLE_NON_NANITE_VSM 0
#endif // defined(GPUCULL_TODO)
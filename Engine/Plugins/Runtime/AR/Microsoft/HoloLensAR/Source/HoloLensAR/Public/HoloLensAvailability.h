// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#ifndef PLATFORM_HOLOLENS
	#define PLATFORM_HOLOLENS 0
#endif

#if PLATFORM_HOLOLENS

	// Check for baseline remoting players support as our 1.0 feature level
	#if WIN10_SDK_VERSION >= 17763
		#define SUPPORTS_HOLOLENS_1_0 1
	#else
		#define SUPPORTS_HOLOLENS_1_0 0
	#endif

	// Base version for hand tracking support for HoloLens 2
	#if WIN10_SDK_VERSION >= 18317
		#define SUPPORTS_HOLOLENS_2_0 1
	#else
		#define SUPPORTS_HOLOLENS_2_0 0
	#endif

#else

	#define SUPPORTS_HOLOLENS_1_0 0
	#define SUPPORTS_HOLOLENS_2_0 0

#endif

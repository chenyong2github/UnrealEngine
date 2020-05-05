// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"

#if WITH_LIBWEBSOCKETS
	#if PLATFORM_SWITCH
		#include "Lws/LwsSwitchWebSocketsManager.h"
	#else
		#include "Lws/LwsWebSocketsManager.h"
	#endif //PLATFORM_SWITCH

#elif PLATFORM_XBOXONE && WITH_LEGACY_XDK
#include "XboxOneWebSocketsManager.h"
#elif PLATFORM_HOLOLENS
#include "HoloLens/HoloLensWebSocketsManager.h"
typedef FHoloLensWebSocketsManager FPlatformWebSocketsManager;
#else
#error "Web sockets not implemented on this platform yet"
#endif // WITH_LIBWEBSOCKETS

#if WITH_LIBWEBSOCKETS
	#if PLATFORM_SWITCH
		typedef FLwsSwitchWebSocketsManager FPlatformWebSocketsManager;
	#else
		typedef FLwsWebSocketsManager FPlatformWebSocketsManager;
	#endif // PLATFORM_SWITCH

#elif PLATFORM_XBOXONE && WITH_LEGACY_XDK
	typedef FXboxOneWebSocketsManager FPlatformWebSocketsManager;
#endif //WITH_LIBWEBSOCKETS

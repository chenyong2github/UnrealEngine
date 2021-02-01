// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"

#if WITH_LIBWEBSOCKETS
	#if PLATFORM_SWITCH
		#include "Lws/LwsSwitchWebSocketsManager.h"
	#else
		#include "Lws/LwsWebSocketsManager.h"
	#endif //PLATFORM_SWITCH
#elif WITH_WINHTTPWEBSOCKETS
	#include "WinHttp/WinHttpWebSocketsManager.h"
#elif PLATFORM_HOLOLENS
	#include "HoloLens/HoloLensWebSocketsManager.h"
#else
	#error "Web Sockets not implemented on this platform yet"
#endif

#if WITH_LIBWEBSOCKETS
	#if PLATFORM_SWITCH
		typedef FLwsSwitchWebSocketsManager FPlatformWebSocketsManager;
	#else
		typedef FLwsWebSocketsManager FPlatformWebSocketsManager;
	#endif // !PLATFORM_SWITCH
#elif WITH_WINHTTPWEBSOCKETS
	typedef FWinHttpWebSocketsManager FPlatformWebSocketsManager;
#elif PLATFORM_HOLOLENS
	typedef FHoloLensWebSocketsManager FPlatformWebSocketsManager;
#endif

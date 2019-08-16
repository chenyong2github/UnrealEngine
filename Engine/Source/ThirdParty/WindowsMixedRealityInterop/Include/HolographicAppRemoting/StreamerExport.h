#pragma once

#define HOLOGRAPHIC_APP_REMOTING_VERSION L"2.0.7.0"

#ifdef HolographicAppRemoting_EXPORTS
#    define STREAMER_EXPORT __declspec(dllexport)
#else
#    define STREAMER_EXPORT __declspec(dllimport)
#endif
#define STREAMER_CALL __cdecl

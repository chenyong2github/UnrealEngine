// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AJALib.h"

namespace AJA
{

	extern LoggingCallbackPtr GLogInfo;
	extern LoggingCallbackPtr GLogWarning;
	extern LoggingCallbackPtr GLogError;


#define ENABLE_AJA_LOGGING		1

	/*
	 * Wrappers around logging callbacks
	 */

#if ENABLE_AJA_LOGGING == 1
#define LOG_INFO(Format, ...) \
{ \
	if(GLogInfo != nullptr) \
	{ \
		GLogInfo(Format, ##__VA_ARGS__); \
	} \
}
#else
#define LOG_INFO(Format, ...)
#endif

#if ENABLE_AJA_LOGGING == 1
#define LOG_WARNING(Format, ...) \
{ \
	if(GLogWarning != nullptr) \
	{ \
		GLogWarning(Format, ##__VA_ARGS__); \
	} \
}
#else
#define LOG_WARNING(Format, ...)
#endif

#if ENABLE_AJA_LOGGING == 1
#define LOG_ERROR(Format, ...) \
{ \
	if(GLogError != nullptr) \
	{ \
		GLogError(Format, ##__VA_ARGS__); \
	} \
}
#else
#define LOG_ERROR(Format, ...)
#endif

}

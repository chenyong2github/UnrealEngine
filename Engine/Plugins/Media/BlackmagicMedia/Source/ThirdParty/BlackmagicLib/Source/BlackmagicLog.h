// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


#include "BlackmagicLib.h"

namespace BlackmagicDesign
{

extern LoggingCallbackPtr GLogInfo;
extern LoggingCallbackPtr GLogWarning;
extern LoggingCallbackPtr GLogError;
extern LoggingCallbackPtr GLogVerbose;

};

#define ENABLE_BM_LOGGING 1

/*
 * Wrappers around logging callbacks
 */

#if ENABLE_BM_LOGGING
#define LOG_INFO(Format, ...) \
{ \
	if(BlackmagicDesign::GLogInfo != nullptr) \
	{ \
		BlackmagicDesign::GLogInfo(Format, ##__VA_ARGS__); \
	} \
}
#define LOG_WARNING(Format, ...) \
{ \
	if(BlackmagicDesign::GLogWarning != nullptr) \
	{ \
		BlackmagicDesign::GLogWarning(Format, ##__VA_ARGS__); \
	} \
}
#define LOG_ERROR(Format, ...) \
{ \
	if(BlackmagicDesign::GLogError != nullptr) \
	{ \
		BlackmagicDesign::GLogError(Format, ##__VA_ARGS__); \
	} \
}
#define LOG_VERBOSE(Format, ...) \
{ \
	if(BlackmagicDesign::GLogVerbose != nullptr) \
	{ \
		BlackmagicDesign::GLogVerbose(Format, ##__VA_ARGS__); \
	} \
}
#else
#define LOG_INFO(Format, ...)
#define LOG_WARNING(Format, ...)
#define LOG_ERROR(Format, ...)
#define LOG_VERBOSE(Format, ...)
#endif

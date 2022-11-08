// Copyright Epic Games, Inc. All Rights Reserved.

#include "stdafx.h"

#if _WINDOWS
#include "GPUTextureTransferLog.h"
#include "GPUTextureTransfer.h"
#endif

/*
 * Global logging callbacks
 */

namespace BlackmagicDesign
{
	LoggingCallbackPtr GLogInfo = nullptr;
	LoggingCallbackPtr GLogWarning = nullptr;
	LoggingCallbackPtr GLogError = nullptr;
	LoggingCallbackPtr GLogVerbose = nullptr;

	UEBLACKMAGICDESIGN_API void SetLoggingCallbacks(LoggingCallbackPtr LogInfoFunc, LoggingCallbackPtr LogWarningFunc, LoggingCallbackPtr LogErrorFunc, LoggingCallbackPtr LogVerboseFunc)
	{
		GLogInfo = LogInfoFunc;
		GLogWarning = LogWarningFunc;
		GLogError = LogErrorFunc;
		GLogVerbose = LogVerboseFunc;

#if _WINDOWS
		GPUTextureTransfer::SetLoggingCallbacks(LogInfoFunc, LogWarningFunc, LogErrorFunc);
#endif
	}
};
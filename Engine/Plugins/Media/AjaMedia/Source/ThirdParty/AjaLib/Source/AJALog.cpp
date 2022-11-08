// Copyright Epic Games, Inc. All Rights Reserved.

#include "stdafx.h"
#include <string>

#include "AJALog.h"

/*
 * Global logging callbacks
 */

namespace AJA
{

	LoggingCallbackPtr GLogInfo = nullptr;
	LoggingCallbackPtr GLogWarning = nullptr;
	LoggingCallbackPtr GLogError = nullptr;

}



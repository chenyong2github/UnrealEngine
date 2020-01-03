// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_logging.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_ext_logging, void, MLLoggingEnableLogLevel)
#define MLLoggingEnableLogLevel ::MLSDK_API::MLLoggingEnableLogLevelShim
CREATE_FUNCTION_SHIM(ml_ext_logging, bool, MLLoggingLogLevelIsEnabled)
#define MLLoggingLogLevelIsEnabled ::MLSDK_API::MLLoggingLogLevelIsEnabledShim
CREATE_FUNCTION_SHIM(ml_ext_logging, void, MLLoggingLogVargs)
#define MLLoggingLogVargs ::MLSDK_API::MLLoggingLogVargsShim
CREATE_FUNCTION_SHIM(ml_ext_logging, void, MLLoggingLog)
#define MLLoggingLog ::MLSDK_API::MLLoggingLogShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK

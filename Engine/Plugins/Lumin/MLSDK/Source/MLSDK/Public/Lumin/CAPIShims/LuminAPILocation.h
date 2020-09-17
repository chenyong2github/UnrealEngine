// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_location.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace LUMIN_MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_location, const char *, MLLocationGetResultString)
#define MLLocationGetResultString ::LUMIN_MLSDK_API::MLLocationGetResultStringShim
CREATE_FUNCTION_SHIM(ml_location, MLResult, MLLocationGetLastCoarseLocation)
#define MLLocationGetLastCoarseLocation ::LUMIN_MLSDK_API::MLLocationGetLastCoarseLocationShim
CREATE_FUNCTION_SHIM(ml_location, MLResult, MLLocationGetLastFineLocation)
#define MLLocationGetLastFineLocation ::LUMIN_MLSDK_API::MLLocationGetLastFineLocationShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK

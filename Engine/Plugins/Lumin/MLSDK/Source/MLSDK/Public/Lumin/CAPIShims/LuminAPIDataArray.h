// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_data_array.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLDataArrayInitDiff)
#define MLDataArrayInitDiff ::MLSDK_API::MLDataArrayInitDiffShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLDataArrayTryLock)
#define MLDataArrayTryLock ::MLSDK_API::MLDataArrayTryLockShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLDataArrayUnlock)
#define MLDataArrayUnlock ::MLSDK_API::MLDataArrayUnlockShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK

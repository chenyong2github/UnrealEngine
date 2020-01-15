// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_eye_tracking.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLEyeTrackingCreate)
#define MLEyeTrackingCreate ::MLSDK_API::MLEyeTrackingCreateShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLEyeTrackingDestroy)
#define MLEyeTrackingDestroy ::MLSDK_API::MLEyeTrackingDestroyShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLEyeTrackingGetStaticData)
#define MLEyeTrackingGetStaticData ::MLSDK_API::MLEyeTrackingGetStaticDataShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLEyeTrackingGetState)
#define MLEyeTrackingGetState ::MLSDK_API::MLEyeTrackingGetStateShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK

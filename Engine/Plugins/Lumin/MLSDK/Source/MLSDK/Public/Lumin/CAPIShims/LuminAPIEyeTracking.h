// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_eye_tracking.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace LUMIN_MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLEyeTrackingCreate)
#define MLEyeTrackingCreate ::LUMIN_MLSDK_API::MLEyeTrackingCreateShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLEyeTrackingDestroy)
#define MLEyeTrackingDestroy ::LUMIN_MLSDK_API::MLEyeTrackingDestroyShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLEyeTrackingGetStaticData)
#define MLEyeTrackingGetStaticData ::LUMIN_MLSDK_API::MLEyeTrackingGetStaticDataShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLEyeTrackingGetState)
#define MLEyeTrackingGetState ::LUMIN_MLSDK_API::MLEyeTrackingGetStateShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK

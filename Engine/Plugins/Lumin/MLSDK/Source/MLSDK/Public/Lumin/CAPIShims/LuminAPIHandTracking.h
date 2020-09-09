// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_hand_tracking.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace LUMIN_MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLHandTrackingCreate)
#define MLHandTrackingCreate ::LUMIN_MLSDK_API::MLHandTrackingCreateShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLHandTrackingDestroy)
#define MLHandTrackingDestroy ::LUMIN_MLSDK_API::MLHandTrackingDestroyShim
CREATE_DEPRECATED_MSG_SHIM(ml_perception_client, MLResult, MLHandTrackingGetData, "Replaced by MLHandTrackingDataEx.")
#define MLHandTrackingGetData ::LUMIN_MLSDK_API::MLHandTrackingGetDataShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLHandTrackingGetDataEx)
#define MLHandTrackingGetDataEx ::LUMIN_MLSDK_API::MLHandTrackingGetDataExShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLHandTrackingGetStaticData)
#define MLHandTrackingGetStaticData ::LUMIN_MLSDK_API::MLHandTrackingGetStaticDataShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLHandTrackingSetConfiguration)
#define MLHandTrackingSetConfiguration ::LUMIN_MLSDK_API::MLHandTrackingSetConfigurationShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLHandTrackingGetConfiguration)
#define MLHandTrackingGetConfiguration ::LUMIN_MLSDK_API::MLHandTrackingGetConfigurationShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK

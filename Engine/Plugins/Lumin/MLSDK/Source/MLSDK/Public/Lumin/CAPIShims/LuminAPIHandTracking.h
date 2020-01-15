// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_hand_tracking.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLHandTrackingCreate)
#define MLHandTrackingCreate ::MLSDK_API::MLHandTrackingCreateShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLHandTrackingDestroy)
#define MLHandTrackingDestroy ::MLSDK_API::MLHandTrackingDestroyShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLHandTrackingGetData)
#define MLHandTrackingGetData ::MLSDK_API::MLHandTrackingGetDataShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLHandTrackingGetStaticData)
#define MLHandTrackingGetStaticData ::MLSDK_API::MLHandTrackingGetStaticDataShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLHandTrackingSetConfiguration)
#define MLHandTrackingSetConfiguration ::MLSDK_API::MLHandTrackingSetConfigurationShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLHandTrackingGetConfiguration)
#define MLHandTrackingGetConfiguration ::MLSDK_API::MLHandTrackingGetConfigurationShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK

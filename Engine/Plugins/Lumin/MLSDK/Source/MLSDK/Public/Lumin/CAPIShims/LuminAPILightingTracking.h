// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_lighting_tracking.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLLightingTrackingCreate)
#define MLLightingTrackingCreate ::MLSDK_API::MLLightingTrackingCreateShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLLightingTrackingDestroy)
#define MLLightingTrackingDestroy ::MLSDK_API::MLLightingTrackingDestroyShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLLightingTrackingGetAmbientGlobalState)
#define MLLightingTrackingGetAmbientGlobalState ::MLSDK_API::MLLightingTrackingGetAmbientGlobalStateShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLLightingTrackingGetAmbientGridState)
#define MLLightingTrackingGetAmbientGridState ::MLSDK_API::MLLightingTrackingGetAmbientGridStateShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLLightingTrackingGetColorTemperatureState)
#define MLLightingTrackingGetColorTemperatureState ::MLSDK_API::MLLightingTrackingGetColorTemperatureStateShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK

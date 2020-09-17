// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_head_tracking.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace LUMIN_MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLHeadTrackingCreate)
#define MLHeadTrackingCreate ::LUMIN_MLSDK_API::MLHeadTrackingCreateShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLHeadTrackingDestroy)
#define MLHeadTrackingDestroy ::LUMIN_MLSDK_API::MLHeadTrackingDestroyShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLHeadTrackingGetStaticData)
#define MLHeadTrackingGetStaticData ::LUMIN_MLSDK_API::MLHeadTrackingGetStaticDataShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLHeadTrackingGetState)
#define MLHeadTrackingGetState ::LUMIN_MLSDK_API::MLHeadTrackingGetStateShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLHeadTrackingGetMapEvents)
#define MLHeadTrackingGetMapEvents ::LUMIN_MLSDK_API::MLHeadTrackingGetMapEventsShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK

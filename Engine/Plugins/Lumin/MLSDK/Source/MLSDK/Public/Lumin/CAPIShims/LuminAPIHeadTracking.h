// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_head_tracking.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLHeadTrackingCreate)
#define MLHeadTrackingCreate ::MLSDK_API::MLHeadTrackingCreateShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLHeadTrackingDestroy)
#define MLHeadTrackingDestroy ::MLSDK_API::MLHeadTrackingDestroyShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLHeadTrackingGetStaticData)
#define MLHeadTrackingGetStaticData ::MLSDK_API::MLHeadTrackingGetStaticDataShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLHeadTrackingGetState)
#define MLHeadTrackingGetState ::MLSDK_API::MLHeadTrackingGetStateShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLHeadTrackingGetMapEvents)
#define MLHeadTrackingGetMapEvents ::MLSDK_API::MLHeadTrackingGetMapEventsShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK

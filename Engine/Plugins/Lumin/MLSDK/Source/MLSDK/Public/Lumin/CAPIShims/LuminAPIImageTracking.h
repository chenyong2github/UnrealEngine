// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_image_tracking.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLImageTrackerCreate)
#define MLImageTrackerCreate ::MLSDK_API::MLImageTrackerCreateShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLImageTrackerUpdateSettings)
#define MLImageTrackerUpdateSettings ::MLSDK_API::MLImageTrackerUpdateSettingsShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLImageTrackerDestroy)
#define MLImageTrackerDestroy ::MLSDK_API::MLImageTrackerDestroyShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLImageTrackerInitSettings)
#define MLImageTrackerInitSettings ::MLSDK_API::MLImageTrackerInitSettingsShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLImageTrackerAddTargetFromImageFile)
#define MLImageTrackerAddTargetFromImageFile ::MLSDK_API::MLImageTrackerAddTargetFromImageFileShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLImageTrackerAddTargetFromArray)
#define MLImageTrackerAddTargetFromArray ::MLSDK_API::MLImageTrackerAddTargetFromArrayShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLImageTrackerRemoveTarget)
#define MLImageTrackerRemoveTarget ::MLSDK_API::MLImageTrackerRemoveTargetShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLImageTrackerUpdateTargetSettings)
#define MLImageTrackerUpdateTargetSettings ::MLSDK_API::MLImageTrackerUpdateTargetSettingsShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLImageTrackerGetTargetStaticData)
#define MLImageTrackerGetTargetStaticData ::MLSDK_API::MLImageTrackerGetTargetStaticDataShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLImageTrackerGetTargetResult)
#define MLImageTrackerGetTargetResult ::MLSDK_API::MLImageTrackerGetTargetResultShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK

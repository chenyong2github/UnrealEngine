// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_raycast.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace LUMIN_MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLRaycastCreate)
#define MLRaycastCreate ::LUMIN_MLSDK_API::MLRaycastCreateShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLRaycastDestroy)
#define MLRaycastDestroy ::LUMIN_MLSDK_API::MLRaycastDestroyShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLRaycastRequest)
#define MLRaycastRequest ::LUMIN_MLSDK_API::MLRaycastRequestShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLRaycastGetResult)
#define MLRaycastGetResult ::LUMIN_MLSDK_API::MLRaycastGetResultShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK

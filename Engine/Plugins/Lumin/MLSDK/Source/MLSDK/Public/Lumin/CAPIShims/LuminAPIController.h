// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_controller.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace LUMIN_MLSDK_API
{

	CREATE_DEPRECATED_MSG_SHIM(ml_perception_client, MLResult, MLControllerCreate, "Replaced by MLControllerCreateEx.")
#define MLControllerCreate ::LUMIN_MLSDK_API::MLControllerCreateShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLControllerCreateEx)
#define MLControllerCreateEx ::LUMIN_MLSDK_API::MLControllerCreateExShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLControllerDestroy)
#define MLControllerDestroy ::LUMIN_MLSDK_API::MLControllerDestroyShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLControllerGetState)
#define MLControllerGetState ::LUMIN_MLSDK_API::MLControllerGetStateShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK

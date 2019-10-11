// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_controller.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLControllerCreate)
#define MLControllerCreate ::MLSDK_API::MLControllerCreateShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLControllerCreateEx)
#define MLControllerCreateEx ::MLSDK_API::MLControllerCreateExShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLControllerDestroy)
#define MLControllerDestroy ::MLSDK_API::MLControllerDestroyShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLControllerGetState)
#define MLControllerGetState ::MLSDK_API::MLControllerGetStateShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK

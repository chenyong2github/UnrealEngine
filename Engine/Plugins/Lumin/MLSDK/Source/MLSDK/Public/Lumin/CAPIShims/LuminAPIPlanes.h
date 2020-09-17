// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_planes.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace LUMIN_MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLPlanesCreate)
#define MLPlanesCreate ::LUMIN_MLSDK_API::MLPlanesCreateShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLPlanesDestroy)
#define MLPlanesDestroy ::LUMIN_MLSDK_API::MLPlanesDestroyShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLPlanesQueryBegin)
#define MLPlanesQueryBegin ::LUMIN_MLSDK_API::MLPlanesQueryBeginShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLPlanesQueryGetResultsWithBoundaries)
#define MLPlanesQueryGetResultsWithBoundaries ::LUMIN_MLSDK_API::MLPlanesQueryGetResultsWithBoundariesShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLPlanesReleaseBoundariesList)
#define MLPlanesReleaseBoundariesList ::LUMIN_MLSDK_API::MLPlanesReleaseBoundariesListShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK

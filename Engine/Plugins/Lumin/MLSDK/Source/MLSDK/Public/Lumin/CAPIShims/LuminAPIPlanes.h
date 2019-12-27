// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_planes.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLPlanesCreate)
#define MLPlanesCreate ::MLSDK_API::MLPlanesCreateShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLPlanesDestroy)
#define MLPlanesDestroy ::MLSDK_API::MLPlanesDestroyShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLPlanesQueryBegin)
#define MLPlanesQueryBegin ::MLSDK_API::MLPlanesQueryBeginShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLPlanesQueryGetResultsWithBoundaries)
#define MLPlanesQueryGetResultsWithBoundaries ::MLSDK_API::MLPlanesQueryGetResultsWithBoundariesShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLPlanesReleaseBoundariesList)
#define MLPlanesReleaseBoundariesList ::MLSDK_API::MLPlanesReleaseBoundariesListShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK

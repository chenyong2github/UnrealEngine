// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_persistent_coordinate_frames.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLPersistentCoordinateFrameTrackerCreate)
#define MLPersistentCoordinateFrameTrackerCreate ::MLSDK_API::MLPersistentCoordinateFrameTrackerCreateShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLPersistentCoordinateFrameGetCount)
#define MLPersistentCoordinateFrameGetCount ::MLSDK_API::MLPersistentCoordinateFrameGetCountShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLPersistentCoordinateFrameGetAll)
#define MLPersistentCoordinateFrameGetAll ::MLSDK_API::MLPersistentCoordinateFrameGetAllShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLPersistentCoordinateFrameGetAllEx)
#define MLPersistentCoordinateFrameGetAllEx ::MLSDK_API::MLPersistentCoordinateFrameGetAllExShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLPersistentCoordinateFrameGetClosest)
#define MLPersistentCoordinateFrameGetClosest ::MLSDK_API::MLPersistentCoordinateFrameGetClosestShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLPersistentCoordinateFrameTrackerDestroy)
#define MLPersistentCoordinateFrameTrackerDestroy ::MLSDK_API::MLPersistentCoordinateFrameTrackerDestroyShim
CREATE_FUNCTION_SHIM(ml_perception_client, const char*, MLPersistentCoordinateFrameGetResultString)
#define MLPersistentCoordinateFrameGetResultString ::MLSDK_API::MLPersistentCoordinateFrameGetResultStringShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLPersistentCoordinateFramesGetFrameState)
#define MLPersistentCoordinateFramesGetFrameState ::MLSDK_API::MLPersistentCoordinateFramesGetFrameStateShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK

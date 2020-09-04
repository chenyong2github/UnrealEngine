// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_perception.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace LUMIN_MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLPerceptionInitSettings)
#define MLPerceptionInitSettings ::LUMIN_MLSDK_API::MLPerceptionInitSettingsShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLPerceptionStartup)
#define MLPerceptionStartup ::LUMIN_MLSDK_API::MLPerceptionStartupShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLPerceptionShutdown)
#define MLPerceptionShutdown ::LUMIN_MLSDK_API::MLPerceptionShutdownShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLPerceptionGetSnapshot)
#define MLPerceptionGetSnapshot ::LUMIN_MLSDK_API::MLPerceptionGetSnapshotShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLPerceptionReleaseSnapshot)
#define MLPerceptionReleaseSnapshot ::LUMIN_MLSDK_API::MLPerceptionReleaseSnapshotShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLPerceptionSetRootCoordinateFrame)
#define MLPerceptionSetRootCoordinateFrame ::LUMIN_MLSDK_API::MLPerceptionSetRootCoordinateFrameShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLPerceptionGetRootCoordinateFrame)
#define MLPerceptionGetRootCoordinateFrame ::LUMIN_MLSDK_API::MLPerceptionGetRootCoordinateFrameShim
CREATE_FUNCTION_SHIM(ml_perception_client, const char*, MLPerceptionGetResultString)
#define MLPerceptionGetResultString ::LUMIN_MLSDK_API::MLPerceptionGetResultStringShim
}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK

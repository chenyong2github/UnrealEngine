// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_perception.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLPerceptionInitSettings)
#define MLPerceptionInitSettings ::MLSDK_API::MLPerceptionInitSettingsShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLPerceptionStartup)
#define MLPerceptionStartup ::MLSDK_API::MLPerceptionStartupShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLPerceptionShutdown)
#define MLPerceptionShutdown ::MLSDK_API::MLPerceptionShutdownShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLPerceptionGetSnapshot)
#define MLPerceptionGetSnapshot ::MLSDK_API::MLPerceptionGetSnapshotShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLPerceptionReleaseSnapshot)
#define MLPerceptionReleaseSnapshot ::MLSDK_API::MLPerceptionReleaseSnapshotShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK

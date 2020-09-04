// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_movement.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace LUMIN_MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_movement, MLResult, MLMovementGetDefaultSettings)
#define MLMovementGetDefaultSettings ::LUMIN_MLSDK_API::MLMovementGetDefaultSettingsShim
CREATE_FUNCTION_SHIM(ml_movement, MLResult, MLMovementStart3Dof)
#define MLMovementStart3Dof ::LUMIN_MLSDK_API::MLMovementStart3DofShim
CREATE_FUNCTION_SHIM(ml_movement, MLResult, MLMovementStart6Dof)
#define MLMovementStart6Dof ::LUMIN_MLSDK_API::MLMovementStart6DofShim
CREATE_FUNCTION_SHIM(ml_movement, MLResult, MLMovementChangeDepth)
#define MLMovementChangeDepth ::LUMIN_MLSDK_API::MLMovementChangeDepthShim
CREATE_FUNCTION_SHIM(ml_movement, MLResult, MLMovementChangeRotation)
#define MLMovementChangeRotation ::LUMIN_MLSDK_API::MLMovementChangeRotationShim
CREATE_FUNCTION_SHIM(ml_movement, MLResult, MLMovementUpdate3Dof)
#define MLMovementUpdate3Dof ::LUMIN_MLSDK_API::MLMovementUpdate3DofShim
CREATE_FUNCTION_SHIM(ml_movement, MLResult, MLMovementUpdate6Dof)
#define MLMovementUpdate6Dof ::LUMIN_MLSDK_API::MLMovementUpdate6DofShim
CREATE_FUNCTION_SHIM(ml_movement, MLResult, MLMovementEnd)
#define MLMovementEnd ::LUMIN_MLSDK_API::MLMovementEndShim
CREATE_FUNCTION_SHIM(ml_movement, MLResult, MLMovementStartHardCollision)
#define MLMovementStartHardCollision ::LUMIN_MLSDK_API::MLMovementStartHardCollisionShim
CREATE_FUNCTION_SHIM(ml_movement, MLResult, MLMovementStartSoftCollision)
#define MLMovementStartSoftCollision ::LUMIN_MLSDK_API::MLMovementStartSoftCollisionShim
CREATE_FUNCTION_SHIM(ml_movement, MLResult, MLMovementUpdateHardCollision)
#define MLMovementUpdateHardCollision ::LUMIN_MLSDK_API::MLMovementUpdateHardCollisionShim
CREATE_FUNCTION_SHIM(ml_movement, MLResult, MLMovementEndCollision)
#define MLMovementEndCollision ::LUMIN_MLSDK_API::MLMovementEndCollisionShim
CREATE_FUNCTION_SHIM(ml_movement, const char*, MLMovementGetResultString)
#define MLMovementGetResultString ::LUMIN_MLSDK_API::MLMovementGetResultStringShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK

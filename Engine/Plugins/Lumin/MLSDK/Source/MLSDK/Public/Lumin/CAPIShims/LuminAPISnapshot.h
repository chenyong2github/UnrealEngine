// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_snapshot.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLSnapshotGetTransform)
#define MLSnapshotGetTransform ::MLSDK_API::MLSnapshotGetTransformShim
CREATE_FUNCTION_SHIM(ml_perception_client, const char*, MLSnapshotGetResultString)
#define MLSnapshotGetResultString ::MLSDK_API::MLSnapshotGetResultStringShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK

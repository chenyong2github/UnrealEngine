// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_meshing2.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace LUMIN_MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLMeshingCreateClient)
#define MLMeshingCreateClient ::LUMIN_MLSDK_API::MLMeshingCreateClientShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLMeshingDestroyClient)
#define MLMeshingDestroyClient ::LUMIN_MLSDK_API::MLMeshingDestroyClientShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLMeshingInitSettings)
#define MLMeshingInitSettings ::LUMIN_MLSDK_API::MLMeshingInitSettingsShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLMeshingUpdateSettings)
#define MLMeshingUpdateSettings ::LUMIN_MLSDK_API::MLMeshingUpdateSettingsShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLMeshingRequestMeshInfo)
#define MLMeshingRequestMeshInfo ::LUMIN_MLSDK_API::MLMeshingRequestMeshInfoShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLMeshingGetMeshInfoResult)
#define MLMeshingGetMeshInfoResult ::LUMIN_MLSDK_API::MLMeshingGetMeshInfoResultShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLMeshingRequestMesh)
#define MLMeshingRequestMesh ::LUMIN_MLSDK_API::MLMeshingRequestMeshShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLMeshingGetMeshResult)
#define MLMeshingGetMeshResult ::LUMIN_MLSDK_API::MLMeshingGetMeshResultShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLMeshingFreeResource)
#define MLMeshingFreeResource ::LUMIN_MLSDK_API::MLMeshingFreeResourceShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_meshing2.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLMeshingCreateClient)
#define MLMeshingCreateClient ::MLSDK_API::MLMeshingCreateClientShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLMeshingDestroyClient)
#define MLMeshingDestroyClient ::MLSDK_API::MLMeshingDestroyClientShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLMeshingInitSettings)
#define MLMeshingInitSettings ::MLSDK_API::MLMeshingInitSettingsShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLMeshingUpdateSettings)
#define MLMeshingUpdateSettings ::MLSDK_API::MLMeshingUpdateSettingsShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLMeshingRequestMeshInfo)
#define MLMeshingRequestMeshInfo ::MLSDK_API::MLMeshingRequestMeshInfoShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLMeshingGetMeshInfoResult)
#define MLMeshingGetMeshInfoResult ::MLSDK_API::MLMeshingGetMeshInfoResultShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLMeshingRequestMesh)
#define MLMeshingRequestMesh ::MLSDK_API::MLMeshingRequestMeshShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLMeshingGetMeshResult)
#define MLMeshingGetMeshResult ::MLSDK_API::MLMeshingGetMeshResultShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLMeshingFreeResource)
#define MLMeshingFreeResource ::MLSDK_API::MLMeshingFreeResourceShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_lifecycle.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_lifecycle, MLResult, MLLifecycleInitEx)
#define MLLifecycleInitEx ::MLSDK_API::MLLifecycleInitExShim
CREATE_FUNCTION_SHIM(ml_lifecycle, MLResult, MLLifecycleGetSelfInfo)
#define MLLifecycleGetSelfInfo ::MLSDK_API::MLLifecycleGetSelfInfoShim
CREATE_FUNCTION_SHIM(ml_lifecycle, MLResult, MLLifecycleFreeSelfInfo)
#define MLLifecycleFreeSelfInfo ::MLSDK_API::MLLifecycleFreeSelfInfoShim
CREATE_FUNCTION_SHIM(ml_lifecycle, MLResult, MLLifecycleGetInitArgList)
#define MLLifecycleGetInitArgList ::MLSDK_API::MLLifecycleGetInitArgListShim
CREATE_FUNCTION_SHIM(ml_lifecycle, MLResult, MLLifecycleGetInitArgListLength)
#define MLLifecycleGetInitArgListLength ::MLSDK_API::MLLifecycleGetInitArgListLengthShim
CREATE_FUNCTION_SHIM(ml_lifecycle, MLResult, MLLifecycleGetInitArgByIndex)
#define MLLifecycleGetInitArgByIndex ::MLSDK_API::MLLifecycleGetInitArgByIndexShim
CREATE_FUNCTION_SHIM(ml_lifecycle, MLResult, MLLifecycleGetInitArgUri)
#define MLLifecycleGetInitArgUri ::MLSDK_API::MLLifecycleGetInitArgUriShim
CREATE_FUNCTION_SHIM(ml_lifecycle, MLResult, MLLifecycleGetFileInfoListLength)
#define MLLifecycleGetFileInfoListLength ::MLSDK_API::MLLifecycleGetFileInfoListLengthShim
CREATE_FUNCTION_SHIM(ml_lifecycle, MLResult, MLLifecycleGetFileInfoByIndex)
#define MLLifecycleGetFileInfoByIndex ::MLSDK_API::MLLifecycleGetFileInfoByIndexShim
CREATE_FUNCTION_SHIM(ml_lifecycle, MLResult, MLLifecycleFreeInitArgList)
#define MLLifecycleFreeInitArgList ::MLSDK_API::MLLifecycleFreeInitArgListShim
CREATE_FUNCTION_SHIM(ml_lifecycle, MLResult, MLLifecycleSetReadyIndication)
#define MLLifecycleSetReadyIndication ::MLSDK_API::MLLifecycleSetReadyIndicationShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_identity.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace LUMIN_MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_identity, MLResult, MLIdentityGetAttributeNames)
#define MLIdentityGetAttributeNames ::LUMIN_MLSDK_API::MLIdentityGetAttributeNamesShim
CREATE_FUNCTION_SHIM(ml_identity, MLResult, MLIdentityGetKnownAttributeNames)
#define MLIdentityGetKnownAttributeNames ::LUMIN_MLSDK_API::MLIdentityGetKnownAttributeNamesShim
CREATE_FUNCTION_SHIM(ml_identity, MLResult, MLIdentityGetAttributeNamesAsync)
#define MLIdentityGetAttributeNamesAsync ::LUMIN_MLSDK_API::MLIdentityGetAttributeNamesAsyncShim
CREATE_FUNCTION_SHIM(ml_identity, MLResult, MLIdentityGetAttributeNamesWait)
#define MLIdentityGetAttributeNamesWait ::LUMIN_MLSDK_API::MLIdentityGetAttributeNamesWaitShim
CREATE_FUNCTION_SHIM(ml_identity, MLResult, MLIdentityRequestAttributeValues)
#define MLIdentityRequestAttributeValues ::LUMIN_MLSDK_API::MLIdentityRequestAttributeValuesShim
CREATE_FUNCTION_SHIM(ml_identity, MLResult, MLIdentityRequestAttributeValuesAsync)
#define MLIdentityRequestAttributeValuesAsync ::LUMIN_MLSDK_API::MLIdentityRequestAttributeValuesAsyncShim
CREATE_FUNCTION_SHIM(ml_identity, MLResult, MLIdentityRequestAttributeValuesWait)
#define MLIdentityRequestAttributeValuesWait ::LUMIN_MLSDK_API::MLIdentityRequestAttributeValuesWaitShim
CREATE_FUNCTION_SHIM(ml_identity, MLResult, MLIdentityReleaseUserProfile)
#define MLIdentityReleaseUserProfile ::LUMIN_MLSDK_API::MLIdentityReleaseUserProfileShim
CREATE_FUNCTION_SHIM(ml_identity, const char*, MLIdentityGetResultString)
#define MLIdentityGetResultString ::LUMIN_MLSDK_API::MLIdentityGetResultStringShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK

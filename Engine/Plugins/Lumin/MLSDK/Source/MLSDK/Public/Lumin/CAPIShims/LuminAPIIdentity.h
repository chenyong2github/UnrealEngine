// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_identity.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_identity, MLResult, MLIdentityGetAttributeNames)
#define MLIdentityGetAttributeNames ::MLSDK_API::MLIdentityGetAttributeNamesShim
CREATE_FUNCTION_SHIM(ml_identity, MLResult, MLIdentityGetKnownAttributeNames)
#define MLIdentityGetKnownAttributeNames ::MLSDK_API::MLIdentityGetKnownAttributeNamesShim
CREATE_FUNCTION_SHIM(ml_identity, MLResult, MLIdentityGetAttributeNamesAsync)
#define MLIdentityGetAttributeNamesAsync ::MLSDK_API::MLIdentityGetAttributeNamesAsyncShim
CREATE_FUNCTION_SHIM(ml_identity, MLResult, MLIdentityGetAttributeNamesWait)
#define MLIdentityGetAttributeNamesWait ::MLSDK_API::MLIdentityGetAttributeNamesWaitShim
CREATE_FUNCTION_SHIM(ml_identity, MLResult, MLIdentityRequestAttributeValues)
#define MLIdentityRequestAttributeValues ::MLSDK_API::MLIdentityRequestAttributeValuesShim
CREATE_FUNCTION_SHIM(ml_identity, MLResult, MLIdentityRequestAttributeValuesAsync)
#define MLIdentityRequestAttributeValuesAsync ::MLSDK_API::MLIdentityRequestAttributeValuesAsyncShim
CREATE_FUNCTION_SHIM(ml_identity, MLResult, MLIdentityRequestAttributeValuesWait)
#define MLIdentityRequestAttributeValuesWait ::MLSDK_API::MLIdentityRequestAttributeValuesWaitShim
CREATE_FUNCTION_SHIM(ml_identity, MLResult, MLIdentityReleaseUserProfile)
#define MLIdentityReleaseUserProfile ::MLSDK_API::MLIdentityReleaseUserProfileShim
CREATE_FUNCTION_SHIM(ml_identity, const char*, MLIdentityGetResultString)
#define MLIdentityGetResultString ::MLSDK_API::MLIdentityGetResultStringShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK

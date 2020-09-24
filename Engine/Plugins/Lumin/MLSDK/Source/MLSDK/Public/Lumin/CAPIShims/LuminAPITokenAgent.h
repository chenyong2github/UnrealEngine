// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_token_agent.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace LUMIN_MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_identity, MLResult, MLTokenAgentGetClientCredentials)
#define MLTokenAgentGetClientCredentials ::LUMIN_MLSDK_API::MLTokenAgentGetClientCredentialsShim
CREATE_FUNCTION_SHIM(ml_identity, MLResult, MLTokenAgentGetClientCredentialsAsync)
#define MLTokenAgentGetClientCredentialsAsync ::LUMIN_MLSDK_API::MLTokenAgentGetClientCredentialsAsyncShim
CREATE_FUNCTION_SHIM(ml_identity, MLResult, MLTokenAgentGetClientCredentialsWait)
#define MLTokenAgentGetClientCredentialsWait ::LUMIN_MLSDK_API::MLTokenAgentGetClientCredentialsWaitShim
CREATE_FUNCTION_SHIM(ml_identity, MLResult, MLTokenAgentReleaseClientCredentials)
#define MLTokenAgentReleaseClientCredentials ::LUMIN_MLSDK_API::MLTokenAgentReleaseClientCredentialsShim
CREATE_FUNCTION_SHIM(ml_identity, const char*, MLTokenAgentGetResultString)
#define MLTokenAgentGetResultString ::LUMIN_MLSDK_API::MLTokenAgentGetResultStringShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_token_agent.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_identity, MLResult, MLTokenAgentGetClientCredentials)
#define MLTokenAgentGetClientCredentials ::MLSDK_API::MLTokenAgentGetClientCredentialsShim
CREATE_FUNCTION_SHIM(ml_identity, MLResult, MLTokenAgentGetClientCredentialsAsync)
#define MLTokenAgentGetClientCredentialsAsync ::MLSDK_API::MLTokenAgentGetClientCredentialsAsyncShim
CREATE_FUNCTION_SHIM(ml_identity, MLResult, MLTokenAgentGetClientCredentialsWait)
#define MLTokenAgentGetClientCredentialsWait ::MLSDK_API::MLTokenAgentGetClientCredentialsWaitShim
CREATE_FUNCTION_SHIM(ml_identity, MLResult, MLTokenAgentReleaseClientCredentials)
#define MLTokenAgentReleaseClientCredentials ::MLSDK_API::MLTokenAgentReleaseClientCredentialsShim
CREATE_FUNCTION_SHIM(ml_identity, const char*, MLTokenAgentGetResultString)
#define MLTokenAgentGetResultString ::MLSDK_API::MLTokenAgentGetResultStringShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_connections.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_connections, const char*, MLConnectionsGetResultString)
#define MLConnectionsGetResultString ::MLSDK_API::MLConnectionsGetResultStringShim
CREATE_FUNCTION_SHIM(ml_connections, MLResult, MLConnectionsStartup)
#define MLConnectionsStartup ::MLSDK_API::MLConnectionsStartupShim
CREATE_FUNCTION_SHIM(ml_connections, MLResult, MLConnectionsShutdown)
#define MLConnectionsShutdown ::MLSDK_API::MLConnectionsShutdownShim
CREATE_FUNCTION_SHIM(ml_connections, MLResult, MLConnectionsRegistrationStartup)
#define MLConnectionsRegistrationStartup ::MLSDK_API::MLConnectionsRegistrationStartupShim
CREATE_FUNCTION_SHIM(ml_connections, MLResult, MLConnectionsRegistrationShutdown)
#define MLConnectionsRegistrationShutdown ::MLSDK_API::MLConnectionsRegistrationShutdownShim
CREATE_FUNCTION_SHIM(ml_connections, MLResult, MLConnectionsRegisterForInvite)
#define MLConnectionsRegisterForInvite ::MLSDK_API::MLConnectionsRegisterForInviteShim
CREATE_FUNCTION_SHIM(ml_connections, MLResult, MLConnectionsRequestInvite)
#define MLConnectionsRequestInvite ::MLSDK_API::MLConnectionsRequestInviteShim
CREATE_FUNCTION_SHIM(ml_connections, MLResult, MLConnectionsTryGetInviteStatus)
#define MLConnectionsTryGetInviteStatus ::MLSDK_API::MLConnectionsTryGetInviteStatusShim
CREATE_FUNCTION_SHIM(ml_connections, MLResult, MLConnectionsCancelInvite)
#define MLConnectionsCancelInvite ::MLSDK_API::MLConnectionsCancelInviteShim
CREATE_FUNCTION_SHIM(ml_connections, MLResult, MLConnectionsReleaseRequestResources)
#define MLConnectionsReleaseRequestResources ::MLSDK_API::MLConnectionsReleaseRequestResourcesShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK

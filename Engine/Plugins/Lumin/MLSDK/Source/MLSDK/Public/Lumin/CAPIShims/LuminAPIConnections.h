// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_connections.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace LUMIN_MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_connections, const char*, MLConnectionsGetResultString)
#define MLConnectionsGetResultString ::LUMIN_MLSDK_API::MLConnectionsGetResultStringShim
CREATE_FUNCTION_SHIM(ml_connections, MLResult, MLConnectionsStartup)
#define MLConnectionsStartup ::LUMIN_MLSDK_API::MLConnectionsStartupShim
CREATE_FUNCTION_SHIM(ml_connections, MLResult, MLConnectionsShutdown)
#define MLConnectionsShutdown ::LUMIN_MLSDK_API::MLConnectionsShutdownShim
CREATE_FUNCTION_SHIM(ml_connections, MLResult, MLConnectionsRegistrationStartup)
#define MLConnectionsRegistrationStartup ::LUMIN_MLSDK_API::MLConnectionsRegistrationStartupShim
CREATE_FUNCTION_SHIM(ml_connections, MLResult, MLConnectionsRegistrationShutdown)
#define MLConnectionsRegistrationShutdown ::LUMIN_MLSDK_API::MLConnectionsRegistrationShutdownShim
CREATE_FUNCTION_SHIM(ml_connections, MLResult, MLConnectionsRegisterForInvite)
#define MLConnectionsRegisterForInvite ::LUMIN_MLSDK_API::MLConnectionsRegisterForInviteShim
CREATE_FUNCTION_SHIM(ml_connections, MLResult, MLConnectionsRequestInvite)
#define MLConnectionsRequestInvite ::LUMIN_MLSDK_API::MLConnectionsRequestInviteShim
CREATE_FUNCTION_SHIM(ml_connections, MLResult, MLConnectionsTryGetInviteStatus)
#define MLConnectionsTryGetInviteStatus ::LUMIN_MLSDK_API::MLConnectionsTryGetInviteStatusShim
CREATE_FUNCTION_SHIM(ml_connections, MLResult, MLConnectionsCancelInvite)
#define MLConnectionsCancelInvite ::LUMIN_MLSDK_API::MLConnectionsCancelInviteShim
CREATE_FUNCTION_SHIM(ml_connections, MLResult, MLConnectionsRequestSelection)
#define MLConnectionsRequestSelection ::LUMIN_MLSDK_API::MLConnectionsRequestSelectionShim
CREATE_FUNCTION_SHIM(ml_connections, MLResult, MLConnectionsTryGetSelectionResult)
#define MLConnectionsTryGetSelectionResult ::LUMIN_MLSDK_API::MLConnectionsTryGetSelectionResultShim
CREATE_FUNCTION_SHIM(ml_connections, MLResult, MLConnectionsCancelSelection)
#define MLConnectionsCancelSelection ::LUMIN_MLSDK_API::MLConnectionsCancelSelectionShim
CREATE_FUNCTION_SHIM(ml_connections, MLResult, MLConnectionsReleaseRequestResources)
#define MLConnectionsReleaseRequestResources ::LUMIN_MLSDK_API::MLConnectionsReleaseRequestResourcesShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK

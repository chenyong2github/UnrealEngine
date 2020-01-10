// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_contacts.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_contacts, MLResult, MLContactsStartup)
#define MLContactsStartup ::MLSDK_API::MLContactsStartupShim
CREATE_FUNCTION_SHIM(ml_contacts, MLResult, MLContactsShutdown)
#define MLContactsShutdown ::MLSDK_API::MLContactsShutdownShim
CREATE_FUNCTION_SHIM(ml_contacts, MLResult, MLContactsRequestInsert)
#define MLContactsRequestInsert ::MLSDK_API::MLContactsRequestInsertShim
CREATE_FUNCTION_SHIM(ml_contacts, MLResult, MLContactsRequestUpdate)
#define MLContactsRequestUpdate ::MLSDK_API::MLContactsRequestUpdateShim
CREATE_FUNCTION_SHIM(ml_contacts, MLResult, MLContactsRequestDelete)
#define MLContactsRequestDelete ::MLSDK_API::MLContactsRequestDeleteShim
CREATE_FUNCTION_SHIM(ml_contacts, MLResult, MLContactsTryGetOperationResult)
#define MLContactsTryGetOperationResult ::MLSDK_API::MLContactsTryGetOperationResultShim
CREATE_FUNCTION_SHIM(ml_contacts, MLResult, MLContactsRequestList)
#define MLContactsRequestList ::MLSDK_API::MLContactsRequestListShim
CREATE_FUNCTION_SHIM(ml_contacts, MLResult, MLContactsRequestSearch)
#define MLContactsRequestSearch ::MLSDK_API::MLContactsRequestSearchShim
CREATE_FUNCTION_SHIM(ml_contacts, MLResult, MLContactsRequestSelection)
#define MLContactsRequestSelection ::MLSDK_API::MLContactsRequestSelectionShim
CREATE_FUNCTION_SHIM(ml_contacts, MLResult, MLContactsTryGetListResult)
#define MLContactsTryGetListResult ::MLSDK_API::MLContactsTryGetListResultShim
CREATE_FUNCTION_SHIM(ml_contacts, MLResult, MLContactsReleaseRequestResources)
#define MLContactsReleaseRequestResources ::MLSDK_API::MLContactsReleaseRequestResourcesShim
CREATE_FUNCTION_SHIM(ml_contacts, const char *, MLContactsGetResultString)
#define MLContactsGetResultString ::MLSDK_API::MLContactsGetResultStringShim
}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK

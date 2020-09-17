// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_contacts.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace LUMIN_MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_contacts, const char *, MLContactsGetResultString)
#define MLContactsGetResultString ::LUMIN_MLSDK_API::MLContactsGetResultStringShim
CREATE_FUNCTION_SHIM(ml_contacts, MLResult, MLContactsStartup)
#define MLContactsStartup ::LUMIN_MLSDK_API::MLContactsStartupShim
CREATE_FUNCTION_SHIM(ml_contacts, MLResult, MLContactsShutdown)
#define MLContactsShutdown ::LUMIN_MLSDK_API::MLContactsShutdownShim
CREATE_FUNCTION_SHIM(ml_contacts, MLResult, MLContactsRequestInsert)
#define MLContactsRequestInsert ::LUMIN_MLSDK_API::MLContactsRequestInsertShim
CREATE_FUNCTION_SHIM(ml_contacts, MLResult, MLContactsRequestUpdate)
#define MLContactsRequestUpdate ::LUMIN_MLSDK_API::MLContactsRequestUpdateShim
CREATE_FUNCTION_SHIM(ml_contacts, MLResult, MLContactsRequestDelete)
#define MLContactsRequestDelete ::LUMIN_MLSDK_API::MLContactsRequestDeleteShim
CREATE_FUNCTION_SHIM(ml_contacts, MLResult, MLContactsTryGetOperationResult)
#define MLContactsTryGetOperationResult ::LUMIN_MLSDK_API::MLContactsTryGetOperationResultShim
CREATE_FUNCTION_SHIM(ml_contacts, MLResult, MLContactsRequestList)
#define MLContactsRequestList ::LUMIN_MLSDK_API::MLContactsRequestListShim
CREATE_FUNCTION_SHIM(ml_contacts, MLResult, MLContactsRequestSearch)
#define MLContactsRequestSearch ::LUMIN_MLSDK_API::MLContactsRequestSearchShim
CREATE_FUNCTION_SHIM(ml_contacts, MLResult, MLContactsTryGetListResult)
#define MLContactsTryGetListResult ::LUMIN_MLSDK_API::MLContactsTryGetListResultShim
CREATE_FUNCTION_SHIM(ml_contacts, MLResult, MLContactsCancelRequest)
#define MLContactsCancelRequest ::LUMIN_MLSDK_API::MLContactsCancelRequestShim
CREATE_FUNCTION_SHIM(ml_contacts, MLResult, MLContactsReleaseRequestResources)
#define MLContactsReleaseRequestResources ::LUMIN_MLSDK_API::MLContactsReleaseRequestResourcesShim
CREATE_FUNCTION_SHIM(ml_contacts, MLResult, MLContactsRequestSelection)
#define MLContactsRequestSelection ::LUMIN_MLSDK_API::MLContactsRequestSelectionShim


}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK

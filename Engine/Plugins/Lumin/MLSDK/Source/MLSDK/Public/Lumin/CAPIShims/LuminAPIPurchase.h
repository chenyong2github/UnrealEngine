// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_purchase.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace LUMIN_MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_purchase, MLResult, MLPurchaseItemDetailsCreate)
#define MLPurchaseItemDetailsCreate ::LUMIN_MLSDK_API::MLPurchaseItemDetailsCreateShim
CREATE_FUNCTION_SHIM(ml_purchase, MLResult, MLPurchaseItemDetailsGet)
#define MLPurchaseItemDetailsGet ::LUMIN_MLSDK_API::MLPurchaseItemDetailsGetShim
CREATE_FUNCTION_SHIM(ml_purchase, MLResult, MLPurchaseItemDetailsGetResult)
#define MLPurchaseItemDetailsGetResult ::LUMIN_MLSDK_API::MLPurchaseItemDetailsGetResultShim
CREATE_FUNCTION_SHIM(ml_purchase, MLResult, MLPurchaseItemDetailsDestroy)
#define MLPurchaseItemDetailsDestroy ::LUMIN_MLSDK_API::MLPurchaseItemDetailsDestroyShim
CREATE_FUNCTION_SHIM(ml_purchase, MLResult, MLPurchaseCreate)
#define MLPurchaseCreate ::LUMIN_MLSDK_API::MLPurchaseCreateShim
CREATE_FUNCTION_SHIM(ml_purchase, MLResult, MLPurchaseSubmit)
#define MLPurchaseSubmit ::LUMIN_MLSDK_API::MLPurchaseSubmitShim
CREATE_FUNCTION_SHIM(ml_purchase, MLResult, MLPurchaseGetResult)
#define MLPurchaseGetResult ::LUMIN_MLSDK_API::MLPurchaseGetResultShim
CREATE_FUNCTION_SHIM(ml_purchase, MLResult, MLPurchaseDestroy)
#define MLPurchaseDestroy ::LUMIN_MLSDK_API::MLPurchaseDestroyShim
CREATE_FUNCTION_SHIM(ml_purchase, MLResult, MLPurchaseHistoryQueryCreate)
#define MLPurchaseHistoryQueryCreate ::LUMIN_MLSDK_API::MLPurchaseHistoryQueryCreateShim
CREATE_FUNCTION_SHIM(ml_purchase, MLResult, MLPurchaseHistoryQueryGetPage)
#define MLPurchaseHistoryQueryGetPage ::LUMIN_MLSDK_API::MLPurchaseHistoryQueryGetPageShim
CREATE_FUNCTION_SHIM(ml_purchase, MLResult, MLPurchaseHistoryQueryGetPageResult)
#define MLPurchaseHistoryQueryGetPageResult ::LUMIN_MLSDK_API::MLPurchaseHistoryQueryGetPageResultShim
CREATE_FUNCTION_SHIM(ml_purchase, MLResult, MLPurchaseHistoryQueryDestroy)
#define MLPurchaseHistoryQueryDestroy ::LUMIN_MLSDK_API::MLPurchaseHistoryQueryDestroyShim
CREATE_FUNCTION_SHIM(ml_purchase, const char*, MLPurchaseGetResultString)
#define MLPurchaseGetResultString ::LUMIN_MLSDK_API::MLPurchaseGetResultStringShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK

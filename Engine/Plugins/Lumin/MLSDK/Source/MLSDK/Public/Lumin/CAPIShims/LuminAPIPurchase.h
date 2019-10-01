// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_purchase.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_purchase, MLResult, MLPurchaseItemDetailsCreate)
#define MLPurchaseItemDetailsCreate ::MLSDK_API::MLPurchaseItemDetailsCreateShim
CREATE_FUNCTION_SHIM(ml_purchase, MLResult, MLPurchaseItemDetailsGet)
#define MLPurchaseItemDetailsGet ::MLSDK_API::MLPurchaseItemDetailsGetShim
CREATE_FUNCTION_SHIM(ml_purchase, MLResult, MLPurchaseItemDetailsGetResult)
#define MLPurchaseItemDetailsGetResult ::MLSDK_API::MLPurchaseItemDetailsGetResultShim
CREATE_FUNCTION_SHIM(ml_purchase, MLResult, MLPurchaseItemDetailsDestroy)
#define MLPurchaseItemDetailsDestroy ::MLSDK_API::MLPurchaseItemDetailsDestroyShim
CREATE_FUNCTION_SHIM(ml_purchase, MLResult, MLPurchaseCreate)
#define MLPurchaseCreate ::MLSDK_API::MLPurchaseCreateShim
CREATE_FUNCTION_SHIM(ml_purchase, MLResult, MLPurchaseSubmit)
#define MLPurchaseSubmit ::MLSDK_API::MLPurchaseSubmitShim
CREATE_FUNCTION_SHIM(ml_purchase, MLResult, MLPurchaseGetResult)
#define MLPurchaseGetResult ::MLSDK_API::MLPurchaseGetResultShim
CREATE_FUNCTION_SHIM(ml_purchase, MLResult, MLPurchaseDestroy)
#define MLPurchaseDestroy ::MLSDK_API::MLPurchaseDestroyShim
CREATE_FUNCTION_SHIM(ml_purchase, MLResult, MLPurchaseHistoryQueryCreate)
#define MLPurchaseHistoryQueryCreate ::MLSDK_API::MLPurchaseHistoryQueryCreateShim
CREATE_FUNCTION_SHIM(ml_purchase, MLResult, MLPurchaseHistoryQueryGetPage)
#define MLPurchaseHistoryQueryGetPage ::MLSDK_API::MLPurchaseHistoryQueryGetPageShim
CREATE_FUNCTION_SHIM(ml_purchase, MLResult, MLPurchaseHistoryQueryGetPageResult)
#define MLPurchaseHistoryQueryGetPageResult ::MLSDK_API::MLPurchaseHistoryQueryGetPageResultShim
CREATE_FUNCTION_SHIM(ml_purchase, MLResult, MLPurchaseHistoryQueryDestroy)
#define MLPurchaseHistoryQueryDestroy ::MLSDK_API::MLPurchaseHistoryQueryDestroyShim
CREATE_FUNCTION_SHIM(ml_purchase, const char*, MLPurchaseGetResultString)
#define MLPurchaseGetResultString ::MLSDK_API::MLPurchaseGetResultStringShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK

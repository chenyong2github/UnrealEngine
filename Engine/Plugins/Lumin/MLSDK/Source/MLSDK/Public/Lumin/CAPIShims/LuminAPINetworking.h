// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_networking.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace LUMIN_MLSDK_API
{
    
CREATE_FUNCTION_SHIM(ml_networking, const char*, MLNetworkingGetResultString)
#define MLNetworkingGetResultString ::LUMIN_MLSDK_API::MLNetworkingGetResultStringShim
CREATE_FUNCTION_SHIM(ml_networking, MLResult, MLNetworkingIsInternetConnected)
#define MLNetworkingIsInternetConnected ::LUMIN_MLSDK_API::MLNetworkingIsInternetConnectedShim
CREATE_FUNCTION_SHIM(ml_networking, MLResult, MLNetworkingGetWiFiData)
#define MLNetworkingGetWiFiData ::LUMIN_MLSDK_API::MLNetworkingGetWiFiDataShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK

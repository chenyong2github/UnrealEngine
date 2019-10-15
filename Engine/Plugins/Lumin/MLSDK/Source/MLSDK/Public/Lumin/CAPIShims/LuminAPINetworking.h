// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_networking.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_networking, MLResult, MLNetworkingIsInternetConnected)
#define MLNetworkingIsInternetConnected ::MLSDK_API::MLNetworkingIsInternetConnectedShim
CREATE_FUNCTION_SHIM(ml_networking, MLResult, MLNetworkingGetWiFiData)
#define MLNetworkingGetWiFiData ::MLSDK_API::MLNetworkingGetWiFiDataShim
CREATE_FUNCTION_SHIM(ml_networking, const char*, MLNetworkingGetResultString)
#define MLNetworkingGetResultString ::MLSDK_API::MLNetworkingGetResultStringShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK

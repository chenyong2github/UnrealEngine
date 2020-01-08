// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPIInput.h"

namespace MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_input, MLResult, MLInputGetConnectedDevices)
#define MLInputGetConnectedDevices ::MLSDK_API::MLInputGetConnectedDevicesShim
CREATE_FUNCTION_SHIM(ml_input, MLResult, MLInputReleaseConnectedDevicesList)
#define MLInputReleaseConnectedDevicesList ::MLSDK_API::MLInputReleaseConnectedDevicesListShim
CREATE_FUNCTION_SHIM(ml_input, MLResult, MLInputSetTabletDeviceCallbacks)
#define MLInputSetTabletDeviceCallbacks ::MLSDK_API::MLInputSetTabletDeviceCallbacksShim
CREATE_FUNCTION_SHIM(ml_input, MLResult, MLInputGetTabletDeviceStates)
#define MLInputGetTabletDeviceStates ::MLSDK_API::MLInputGetTabletDeviceStatesShim
CREATE_FUNCTION_SHIM(ml_input, MLResult, MLInputReleaseTabletDeviceStates)
#define MLInputReleaseTabletDeviceStates ::MLSDK_API::MLInputReleaseTabletDeviceStatesShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK

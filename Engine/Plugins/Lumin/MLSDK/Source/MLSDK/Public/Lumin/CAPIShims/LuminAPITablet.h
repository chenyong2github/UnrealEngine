// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPIInput.h"

namespace LUMIN_MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_input, MLResult, MLInputGetConnectedDevices)
#define MLInputGetConnectedDevices ::LUMIN_MLSDK_API::MLInputGetConnectedDevicesShim
CREATE_FUNCTION_SHIM(ml_input, MLResult, MLInputReleaseConnectedDevicesList)
#define MLInputReleaseConnectedDevicesList ::LUMIN_MLSDK_API::MLInputReleaseConnectedDevicesListShim
CREATE_FUNCTION_SHIM(ml_input, MLResult, MLInputSetTabletDeviceCallbacks)
#define MLInputSetTabletDeviceCallbacks ::LUMIN_MLSDK_API::MLInputSetTabletDeviceCallbacksShim
CREATE_FUNCTION_SHIM(ml_input, MLResult, MLInputGetTabletDeviceStates)
#define MLInputGetTabletDeviceStates ::LUMIN_MLSDK_API::MLInputGetTabletDeviceStatesShim
CREATE_FUNCTION_SHIM(ml_input, MLResult, MLInputReleaseTabletDeviceStates)
#define MLInputReleaseTabletDeviceStates ::LUMIN_MLSDK_API::MLInputReleaseTabletDeviceStatesShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK

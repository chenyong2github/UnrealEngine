// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_camera.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace LUMIN_MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_camera, MLResult, MLCameraConnect)
#define MLCameraConnect ::LUMIN_MLSDK_API::MLCameraConnectShim
CREATE_FUNCTION_SHIM(ml_camera, MLResult, MLCameraDisconnect)
#define MLCameraDisconnect ::LUMIN_MLSDK_API::MLCameraDisconnectShim
CREATE_FUNCTION_SHIM(ml_camera, MLResult, MLCameraPrepareCapture)
#define MLCameraPrepareCapture ::LUMIN_MLSDK_API::MLCameraPrepareCaptureShim
CREATE_FUNCTION_SHIM(ml_camera, MLResult, MLCameraSetDeviceStatusCallbacks)
#define MLCameraSetDeviceStatusCallbacks ::LUMIN_MLSDK_API::MLCameraSetDeviceStatusCallbacksShim
CREATE_DEPRECATED_MSG_SHIM(ml_camera, MLResult, MLCameraSetCaptureCallbacks, "Replaced by MLCameraSetCaptureCallbacksEx.")
#define MLCameraSetCaptureCallbacks ::LUMIN_MLSDK_API::MLCameraSetCaptureCallbacksShim
CREATE_FUNCTION_SHIM(ml_camera, MLResult, MLCameraSetCaptureCallbacksEx)
#define MLCameraSetCaptureCallbacksEx ::LUMIN_MLSDK_API::MLCameraSetCaptureCallbacksExShim
CREATE_FUNCTION_SHIM(ml_camera, MLResult, MLCameraSetOutputFormat)
#define MLCameraSetOutputFormat ::LUMIN_MLSDK_API::MLCameraSetOutputFormatShim
CREATE_FUNCTION_SHIM(ml_camera, MLResult, MLCameraCaptureImage)
#define MLCameraCaptureImage ::LUMIN_MLSDK_API::MLCameraCaptureImageShim
CREATE_FUNCTION_SHIM(ml_camera, MLResult, MLCameraCaptureImageRaw)
#define MLCameraCaptureImageRaw ::LUMIN_MLSDK_API::MLCameraCaptureImageRawShim
CREATE_FUNCTION_SHIM(ml_camera, MLResult, MLCameraCaptureVideoStart)
#define MLCameraCaptureVideoStart ::LUMIN_MLSDK_API::MLCameraCaptureVideoStartShim
CREATE_FUNCTION_SHIM(ml_camera, MLResult, MLCameraCaptureRawVideoStart)
#define MLCameraCaptureRawVideoStart ::LUMIN_MLSDK_API::MLCameraCaptureRawVideoStartShim
CREATE_FUNCTION_SHIM(ml_camera, MLResult, MLCameraCaptureVideoStop)
#define MLCameraCaptureVideoStop ::LUMIN_MLSDK_API::MLCameraCaptureVideoStopShim
CREATE_FUNCTION_SHIM(ml_camera, MLResult, MLCameraGetDeviceStatus)
#define MLCameraGetDeviceStatus ::LUMIN_MLSDK_API::MLCameraGetDeviceStatusShim
CREATE_FUNCTION_SHIM(ml_camera, MLResult, MLCameraGetCaptureStatus)
#define MLCameraGetCaptureStatus ::LUMIN_MLSDK_API::MLCameraGetCaptureStatusShim
CREATE_FUNCTION_SHIM(ml_camera, MLResult, MLCameraGetErrorCode)
#define MLCameraGetErrorCode ::LUMIN_MLSDK_API::MLCameraGetErrorCodeShim
CREATE_FUNCTION_SHIM(ml_camera, MLResult, MLCameraGetPreviewStream)
#define MLCameraGetPreviewStream ::LUMIN_MLSDK_API::MLCameraGetPreviewStreamShim
CREATE_FUNCTION_SHIM(ml_camera, MLResult, MLCameraGetImageStream)
#define MLCameraGetImageStream ::LUMIN_MLSDK_API::MLCameraGetImageStreamShim
CREATE_FUNCTION_SHIM(ml_camera, MLResult, MLCameraGetCaptureResultExtras)
#define MLCameraGetCaptureResultExtras ::LUMIN_MLSDK_API::MLCameraGetCaptureResultExtrasShim
CREATE_FUNCTION_SHIM(ml_camera, MLResult, MLCameraGetCameraCharacteristics)
#define MLCameraGetCameraCharacteristics ::LUMIN_MLSDK_API::MLCameraGetCameraCharacteristicsShim
CREATE_FUNCTION_SHIM(ml_camera, MLResult, MLCameraGetResultMetadata)
#define MLCameraGetResultMetadata ::LUMIN_MLSDK_API::MLCameraGetResultMetadataShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK

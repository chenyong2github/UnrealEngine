// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_camera.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_camera, MLResult, MLCameraConnect)
#define MLCameraConnect ::MLSDK_API::MLCameraConnectShim
CREATE_FUNCTION_SHIM(ml_camera, MLResult, MLCameraDisconnect)
#define MLCameraDisconnect ::MLSDK_API::MLCameraDisconnectShim
CREATE_FUNCTION_SHIM(ml_camera, MLResult, MLCameraPrepareCapture)
#define MLCameraPrepareCapture ::MLSDK_API::MLCameraPrepareCaptureShim
CREATE_FUNCTION_SHIM(ml_camera, MLResult, MLCameraSetDeviceStatusCallbacks)
#define MLCameraSetDeviceStatusCallbacks ::MLSDK_API::MLCameraSetDeviceStatusCallbacksShim
CREATE_FUNCTION_SHIM(ml_camera, MLResult, MLCameraSetCaptureCallbacks)
#define MLCameraSetCaptureCallbacks ::MLSDK_API::MLCameraSetCaptureCallbacksShim
CREATE_FUNCTION_SHIM(ml_camera, MLResult, MLCameraSetOutputFormat)
#define MLCameraSetOutputFormat ::MLSDK_API::MLCameraSetOutputFormatShim
CREATE_FUNCTION_SHIM(ml_camera, MLResult, MLCameraCaptureImage)
#define MLCameraCaptureImage ::MLSDK_API::MLCameraCaptureImageShim
CREATE_FUNCTION_SHIM(ml_camera, MLResult, MLCameraCaptureImageRaw)
#define MLCameraCaptureImageRaw ::MLSDK_API::MLCameraCaptureImageRawShim
CREATE_FUNCTION_SHIM(ml_camera, MLResult, MLCameraCaptureVideoStart)
#define MLCameraCaptureVideoStart ::MLSDK_API::MLCameraCaptureVideoStartShim
CREATE_FUNCTION_SHIM(ml_camera, MLResult, MLCameraCaptureVideoStop)
#define MLCameraCaptureVideoStop ::MLSDK_API::MLCameraCaptureVideoStopShim
CREATE_FUNCTION_SHIM(ml_camera, MLResult, MLCameraGetDeviceStatus)
#define MLCameraGetDeviceStatus ::MLSDK_API::MLCameraGetDeviceStatusShim
CREATE_FUNCTION_SHIM(ml_camera, MLResult, MLCameraGetCaptureStatus)
#define MLCameraGetCaptureStatus ::MLSDK_API::MLCameraGetCaptureStatusShim
CREATE_FUNCTION_SHIM(ml_camera, MLResult, MLCameraGetErrorCode)
#define MLCameraGetErrorCode ::MLSDK_API::MLCameraGetErrorCodeShim
CREATE_FUNCTION_SHIM(ml_camera, MLResult, MLCameraGetPreviewStream)
#define MLCameraGetPreviewStream ::MLSDK_API::MLCameraGetPreviewStreamShim
CREATE_FUNCTION_SHIM(ml_camera, MLResult, MLCameraGetImageStream)
#define MLCameraGetImageStream ::MLSDK_API::MLCameraGetImageStreamShim
CREATE_FUNCTION_SHIM(ml_camera, MLResult, MLCameraGetCaptureResultExtras)
#define MLCameraGetCaptureResultExtras ::MLSDK_API::MLCameraGetCaptureResultExtrasShim
CREATE_FUNCTION_SHIM(ml_camera, MLResult, MLCameraGetCameraCharacteristics)
#define MLCameraGetCameraCharacteristics ::MLSDK_API::MLCameraGetCameraCharacteristicsShim
CREATE_FUNCTION_SHIM(ml_camera, MLResult, MLCameraGetResultMetadata)
#define MLCameraGetResultMetadata ::MLSDK_API::MLCameraGetResultMetadataShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK

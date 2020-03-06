// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_cv_camera.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLCVCameraTrackingCreate)
#define MLCVCameraTrackingCreate ::MLSDK_API::MLCVCameraTrackingCreateShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLCVCameraGetIntrinsicCalibrationParameters)
#define MLCVCameraGetIntrinsicCalibrationParameters ::MLSDK_API::MLCVCameraGetIntrinsicCalibrationParametersShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLCVCameraGetFramePose)
#define MLCVCameraGetFramePose ::MLSDK_API::MLCVCameraGetFramePoseShim
CREATE_FUNCTION_SHIM(ml_perception_client, MLResult, MLCVCameraTrackingDestroy)
#define MLCVCameraTrackingDestroy ::MLSDK_API::MLCVCameraTrackingDestroyShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK

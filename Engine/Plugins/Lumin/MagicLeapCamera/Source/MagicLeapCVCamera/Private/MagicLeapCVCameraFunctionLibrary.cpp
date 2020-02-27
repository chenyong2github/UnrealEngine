// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapCVCameraFunctionLibrary.h"
#include "MagicLeapCVCameraModule.h"

bool UMagicLeapCVCameraFunctionLibrary::EnableAsync(const FMagicLeapCVCameraEnable& OnEnabled)
{
	FMagicLeapCVCameraEnableMulti OnEnabledMulti;
	OnEnabledMulti.Add(OnEnabled);
	return GetMagicLeapCVCameraModule().EnableAsync(OnEnabledMulti);
}

bool UMagicLeapCVCameraFunctionLibrary::DisableAsync(const FMagicLeapCVCameraDisable& OnDisabled)
{
	FMagicLeapCVCameraDisableMulti OnDisabledMulti;
	OnDisabledMulti.Add(OnDisabled);
	return GetMagicLeapCVCameraModule().DisableAsync(OnDisabledMulti);
}

bool UMagicLeapCVCameraFunctionLibrary::GetIntrinsicCalibrationParameters(FMagicLeapCVCameraIntrinsicCalibrationParameters& OutParams)
{
	return GetMagicLeapCVCameraModule().GetIntrinsicCalibrationParameters(OutParams);
}

bool UMagicLeapCVCameraFunctionLibrary::GetFramePose(FTransform& OutFramePose)
{
	return GetMagicLeapCVCameraModule().GetFramePose(OutFramePose);
}

bool UMagicLeapCVCameraFunctionLibrary::GetCameraOutput(FMagicLeapCameraOutput& OutCameraOutput)
{
	return GetMagicLeapCVCameraModule().GetCameraOutput(OutCameraOutput);
}

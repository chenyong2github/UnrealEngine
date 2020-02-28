// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapCVCameraComponent.h"
#include "MagicLeapCVCameraModule.h"

void UMagicLeapCVCameraComponent::BeginPlay()
{
	Super::BeginPlay();
	GetMagicLeapCVCameraModule().SetLogDelegate(OnLogMessage);
	GetMagicLeapCVCameraModule().EnableAsync(OnEnabled);
}

void UMagicLeapCVCameraComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetMagicLeapCVCameraModule().DisableAsync(OnDisabled);
	Super::EndPlay(EndPlayReason);
}

bool UMagicLeapCVCameraComponent::GetIntrinsicCalibrationParameters(FMagicLeapCVCameraIntrinsicCalibrationParameters& OutParams)
{
	return GetMagicLeapCVCameraModule().GetIntrinsicCalibrationParameters(OutParams);
}

bool UMagicLeapCVCameraComponent::GetFramePose(FTransform& OutFramePose)
{
	return GetMagicLeapCVCameraModule().GetFramePose(OutFramePose);
}

bool UMagicLeapCVCameraComponent::GetCameraOutput(FMagicLeapCameraOutput& OutCameraOutput)
{
	return GetMagicLeapCVCameraModule().GetCameraOutput(OutCameraOutput);
}

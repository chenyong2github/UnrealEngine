// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Engine/Engine.h"
#include "MagicLeapCVCameraTypes.generated.h"

USTRUCT(BlueprintType)
struct MAGICLEAPCVCAMERA_API FMagicLeapCVCameraIntrinsicCalibrationParameters
{
	GENERATED_BODY();

	/*! Structure version. */
	uint32_t Version;
	/*! Camera width. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CVCamera|MagicLeap")
	int32 Width;
	/*! Camera height. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CVCamera|MagicLeap")
	int32 Height;
	/*! Camera focal length. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CVCamera|MagicLeap")
	FVector2D FocalLength;
	/*! Camera principle point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CVCamera|MagicLeap")
	FVector2D PrincipalPoint;
	/*! Field of view. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CVCamera|MagicLeap")
	float FOV;
	/*! Distortion vector. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CVCamera|MagicLeap")
	TArray<float> Distortion;
};

/**
	Delegate used to notify the initiating blueprint of the result of a request to enable the computer vision camera.
	@note Although this signals the task as complete, it may have failed or been cancelled.
	@param bSuccess True if the task succeeded, false otherwise.
*/
DECLARE_DELEGATE_OneParam(FMagicLeapCVCameraEnableStatic, const bool);
DECLARE_DYNAMIC_DELEGATE_OneParam(FMagicLeapCVCameraEnable, const bool, bSuccess);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMagicLeapCVCameraEnableMulti, const bool, bSuccess);

/**
	Delegate used to notify the initiating blueprint of the result of a request to stop the computer vision camera.
	@note Although this signals the task as complete, it may have failed or been cancelled.
	@param bSuccess True if the task succeeded, false otherwise.
	@param FilePath A string containing the path to the newly created mp4.
*/
DECLARE_DELEGATE_OneParam(FMagicLeapCVCameraDisableStatic, const bool);
DECLARE_DYNAMIC_DELEGATE_OneParam(FMagicLeapCVCameraDisable, const bool, bSuccess);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMagicLeapCVCameraDisableMulti, const bool, bSuccess);

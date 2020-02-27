// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Engine/Engine.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MagicLeapCameraTypes.h"
#include "MagicLeapCVCameraTypes.h"
#include "MagicLeapCVCameraFunctionLibrary.generated.h"

/**
  The MagicLeapCVCameraLibrary provides access to and maintains state for computer vision
  camera capture functionality.  The connection to the device's camera is managed internally.
  Users of this function library are able to retrieve various computer vision data for processing.
*/
UCLASS(ClassGroup = MagicLeap)
class MAGICLEAPCVCAMERA_API UMagicLeapCVCameraFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Initializes the computer vision stream. */
	UFUNCTION(BlueprintPure, Category = "CVCamera Function Library | MagicLeap")
	static bool EnableAsync(const FMagicLeapCVCameraEnable& OnEnable);

	/** Closes the computer vision stream. */
	UFUNCTION(BlueprintPure, Category = "CVCamera Function Library | MagicLeap")
	static bool DisableAsync(const FMagicLeapCVCameraDisable& OnDisable);

	/**
		Gets the intrinsic calibration parameters of the camera.  Requires the camera to be connected.
		@param OutParam Contains the returned intrinsic calibration parameters if the call is successful.
		@return True if the parameters were successfully retrieved, false otherwise.
	*/
	UFUNCTION(BlueprintPure, Category = "CVCamera Function Library | MagicLeap")
	static bool GetIntrinsicCalibrationParameters(FMagicLeapCVCameraIntrinsicCalibrationParameters& OutParams);

	/**
		Gets the latest transform of the camera.
		@param OutFramePose Contains the returned transform of the camera if the call is successful.
		@return True if the transform was successfully retrieved, false otherwise.
	*/
	UFUNCTION(BlueprintPure, Category = "CVCamera Function Library | MagicLeap")
	static bool GetFramePose(FTransform& OutFramePose);

	/**
		Gets the latest transform of the camera.
		@param OutFramePose Contains the returned transform of the camera if the call is successful.
		@return True if the transform was successfully retrieved, false otherwise.
	*/
	UFUNCTION(BlueprintPure, Category = "CVCamera Function Library | MagicLeap")
	static bool GetCameraOutput(FMagicLeapCameraOutput& OutCameraOutput);
};

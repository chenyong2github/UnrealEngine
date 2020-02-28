// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/Engine.h"
#include "Components/ActorComponent.h"
#include "MagicLeapCameraTypes.h"
#include "MagicLeapCVCameraTypes.h"
#include "MagicLeapCVCameraComponent.generated.h"

/**
  The MagicLeapCVCameraComponent provides access to and maintains state for computer vision
  camera capture functionality.  The connection to the device's camera is managed internally.
  Users of this component are able to retrieve various computer vision data for processing.
*/
UCLASS(ClassGroup = MagicLeap, BlueprintType, Blueprintable, EditInlineNew, meta = (BlueprintSpawnableComponent))
class MAGICLEAPCVCAMERA_API UMagicLeapCVCameraComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** Initializes the computer vision stream. */
	void BeginPlay() override;

	/** Closes the computer vision stream. */
	void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/**
		Gets the intrinsic calibration parameters of the camera.  Requires the camera to be connected.
		@param OutParam Contains the returned intrinsic calibration parameters if the call is successful.
		@return True if the parameters were successfully retrieved, false otherwise.
	*/
	UFUNCTION(BlueprintPure, Category = "CVCamera | MagicLeap")
	bool GetIntrinsicCalibrationParameters(FMagicLeapCVCameraIntrinsicCalibrationParameters& OutParams);

	/**
		Gets the latest transform of the camera.
		@param OutFramePose Contains the returned transform of the camera if the call is successful.
		@return True if the transform was successfully retrieved, false otherwise.
	*/
	UFUNCTION(BlueprintPure, Category = "CVCamera | MagicLeap")
	bool GetFramePose(FTransform& OutFramePose);

	/**
		Gets the latest transform of the camera.
		@param OutFramePose Contains the returned transform of the camera if the call is successful.
		@return True if the transform was successfully retrieved, false otherwise.
	*/
	UFUNCTION(BlueprintPure, Category = "CVCamera | MagicLeap")
	bool GetCameraOutput(FMagicLeapCameraOutput& OutCameraOutput);

private:
	FMagicLeapCVCameraEnableMulti OnEnabled;
	UPROPERTY(BlueprintAssignable, Category = "CVCamera | MagicLeap", meta = (AllowPrivateAccess = true))
	FMagicLeapCVCameraDisableMulti OnDisabled;
	UPROPERTY(BlueprintAssignable, Category = "CVCamera | MagicLeap", meta = (AllowPrivateAccess = true))
	FMagicLeapCameraLogMessageMulti OnLogMessage;
};

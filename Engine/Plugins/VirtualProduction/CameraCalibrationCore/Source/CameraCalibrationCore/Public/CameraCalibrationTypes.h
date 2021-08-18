// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CineCameraComponent.h"
#include "LensDistortionModelHandlerBase.h"
#include "Misc/Guid.h"

#include "CameraCalibrationTypes.generated.h"

/** Utility structure for selecting a distortion handler from the camera calibration subsystem */
USTRUCT(BlueprintType)
struct CAMERACALIBRATIONCORE_API FDistortionHandlerPicker
{
	GENERATED_BODY()

public:
	/** CineCameraComponent with which the desired distortion handler is associated */
	UPROPERTY(BlueprintReadWrite, Category = "Distortion", Transient)
	UCineCameraComponent* TargetCameraComponent = nullptr;

	/** UObject that produces the distortion state for the desired distortion handler */
	UPROPERTY(BlueprintReadWrite, Category = "Distortion")
	FGuid DistortionProducerID;

	/** Display name of the desired distortion handler */
	UPROPERTY(BlueprintReadWrite, Category = "Distortion")
	FString HandlerDisplayName;
};
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"


class FCameraCalibrationToolkit;
class SWidget;
class UCameraCalibrationStep;

/**
 * Controller for SCameraCalibationStepsWidget, where the calibration steps are hosted in.
 */
class FCameraCalibationStepsController
{
public:

	FCameraCalibationStepsController(TWeakPtr<FCameraCalibrationToolkit> InCameraCalibrationToolkit);
	~FCameraCalibationStepsController();

	/** Returns the UI that this object controls */
	TSharedPtr<SWidget> BuildUI();

private:

	/** Pointer to the camera calibration toolkit */
	TWeakPtr<FCameraCalibrationToolkit> CameraCalibrationToolkit;

	/** Array of the calibration steps that this controller is managing */
	TArray<TWeakObjectPtr<UCameraCalibrationStep>> CalibrationSteps;
};

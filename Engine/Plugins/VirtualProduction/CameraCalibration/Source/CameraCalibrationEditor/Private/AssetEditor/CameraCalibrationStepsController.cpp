// Copyright Epic Games, Inc. All Rights Reserved.

#include "CameraCalibrationStepsController.h"

#include "CameraCalibrationStep.h"
#include "CameraCalibrationSubsystem.h"
#include "CameraCalibrationToolkit.h"
#include "Widgets/SWidget.h"

FCameraCalibationStepsController::FCameraCalibationStepsController(TWeakPtr<FCameraCalibrationToolkit> InCameraCalibrationToolkit)
	: CameraCalibrationToolkit(InCameraCalibrationToolkit)
{
	check(CameraCalibrationToolkit.IsValid());
}

FCameraCalibationStepsController::~FCameraCalibationStepsController()
{

}

TSharedPtr<SWidget> FCameraCalibationStepsController::BuildUI()
{
	return nullptr;
}

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CameraCalibrationStep.h"

#include "LensData.h"
#include "UObject/StructOnScope.h"

#include "LensInfoStep.generated.h"

/**
 * ULensInfoStep is used as the initial step to provide information about the lens you are going to calibrate
 */
UCLASS()
class ULensInfoStep : public UCameraCalibrationStep
{
	GENERATED_BODY()

public:

	//~ Begin UCameraCalibrationStep interface
	virtual void Initialize(TWeakPtr<FCameraCalibrationStepsController> InCameraCalibrationStepController) override;
	virtual TSharedRef<SWidget> BuildUI() override;
	virtual FName FriendlyName() const  override { return TEXT("Lens Information"); };
	virtual bool DependsOnStep(UCameraCalibrationStep* Step) const override;
	virtual void Activate() override;
	virtual void Deactivate() override;
	virtual bool IsActive() const override;
	//~ End UCameraCalibrationStep interface

public:

	/** Triggered when user click save data button */
	void OnSaveLensInformation() const;

	/** Reapplied current value from LensFile in our temporary copy displayed in UI */
	void ResetToDefault() const;

	/** Returns true if our LensInfo differs from the one in the LensFile being edited */
	bool DiffersFromDefault() const;

private:

	/** Pointer to the calibration steps controller */
	TWeakPtr<FCameraCalibrationStepsController> CameraCalibrationStepsController;

	/** True if this tool is the active one in the panel */
	bool bIsActive = false;

	/** Lens information being edited in the panel */
	TSharedPtr<TStructOnScope<FLensInfo>> EditedLensInfo;
};

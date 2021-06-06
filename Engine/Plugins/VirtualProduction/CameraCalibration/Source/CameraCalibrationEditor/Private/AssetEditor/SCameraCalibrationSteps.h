// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "CameraCalibrationStepsController.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "UObject/StrongObjectPtr.h"

class FString;
class SCheckBox;

template<typename OptionType>
class SComboBox;

class SWidgetSwitcher;

struct FArguments;

/**
 * UI for the nodal offset calibration.
 * It also holds the UI given by the selected nodal offset algorithm.
 */
class SCameraCalibrationSteps : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SCameraCalibrationSteps) {}
	SLATE_END_ARGS()

public:
	void Construct(const FArguments& InArgs, TWeakPtr<FCameraCalibrationStepsController> InCalibrationStepsController);

private:

	/** Builds the UI used to pick the camera used for the CG layer of the comp */
	TSharedRef<SWidget> BuildCameraPickerWidget();

	/** Builds the UI for the simulcam wiper */
	TSharedRef<SWidget> BuildSimulcamWiperWidget();

	/** Builds the UI for the media source picker */
	TSharedRef<SWidget> BuildMediaSourceWidget();

	/** Builds the UI for the calibration step selection */
	TSharedRef<SWidget> BuildStepSelectionWidget();

	/** Refreshes the list of available media sources shown in the MediaSourcesComboBox */
	void UpdateMediaSourcesOptions();

	/** Expected to be called when user selects a new step via the UI */
	void SelectStep(const FName& StepName);

private:

	/** The controller object */
	TWeakPtr<class FCameraCalibrationStepsController> CalibrationStepsController;
	
	/** Options source for the MediaSourcesComboBox. Lists the currently available media sources */
	TArray<TSharedPtr<FString>> CurrentMediaSources;

	/** The combobox that presents the available media sources */
	TSharedPtr<SComboBox<TSharedPtr<FString>>> MediaSourcesComboBox;

	/** Widget switcher to only display the UI of the selected step */
	TSharedPtr<SWidgetSwitcher> StepWidgetSwitcher;

	/** Step selection buttons */
	TMap<FName, TSharedPtr<SCheckBox>> StepToggles;
};

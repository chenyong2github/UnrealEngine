// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "TimedDataMonitorCalibration.h"

#include "TimedDataMonitorEditorSettings.generated.h"


UENUM()
enum class ETimedDataMonitorEditorCalibrationType
{
	CalibrateWithTimecode = 0,
	TimeCorrection = 1,
	Max = 2
};


UCLASS(config = EditorPerProjectUserSettings, MinimalAPI)
class UTimedDataMonitorEditorSettings : public UObject
{
	GENERATED_BODY()

public:
	/** Option to use when calibrating from the UI. */
	UPROPERTY(Config, EditAnywhere, Category = "Calibration")
	FTimedDataMonitorCalibrationParameters CalibrationSettings;

	/** Option to use when calibrating from the UI. */
	UPROPERTY(Config, EditAnywhere, Category = "Calibration")
	FTimedDataMonitorTimeCorrectionParameters TimeCorrectionSettings;

	UPROPERTY(Config, EditAnywhere, Category ="UI", meta=(ClampMin=0.0f))
	float RefreshRate = 0.2f;

	UPROPERTY(Config, AdvancedDisplay, EditAnywhere, Category = "UI", meta = (InlineEditConditionToggle = true))
	bool bOverrideNumberOfStandardDeviationToShow;

	UPROPERTY(Config, AdvancedDisplay, EditAnywhere, Category = "UI", meta = (ClampMin = 0, ClampMax = 5, EditCondition = "bOverrideNumberOfStandardDeviationToShow"))
	int32 OverrideNumberOfStandardDeviationToShow = 5;

	UPROPERTY(Config)
	ETimedDataMonitorEditorCalibrationType LastCalibrationType = ETimedDataMonitorEditorCalibrationType::CalibrateWithTimecode;

public:
	int32 GetNumberOfStandardDeviationForUI() const
	{
		if (bOverrideNumberOfStandardDeviationToShow)
		{
			return OverrideNumberOfStandardDeviationToShow;
		}
		else if (LastCalibrationType == ETimedDataMonitorEditorCalibrationType::CalibrateWithTimecode && CalibrationSettings.bUseStandardDeviation)
		{
			return CalibrationSettings.NumberOfStandardDeviation;
		}
		else if (LastCalibrationType == ETimedDataMonitorEditorCalibrationType::TimeCorrection && TimeCorrectionSettings.bUseStandardDeviation)
		{
			return TimeCorrectionSettings.NumberOfStandardDeviation;
		}
		return 0;
	}
};

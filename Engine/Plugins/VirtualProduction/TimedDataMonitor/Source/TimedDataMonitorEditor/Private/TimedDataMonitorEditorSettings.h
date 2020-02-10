// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

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
	/** Number of standard deviation for the newest sample to used when calibration. */
	UPROPERTY(Config, EditAnywhere, Category = "Calibration", meta=(ClampMin=0, ClampMax=5))
	int32 NumberOfSampleStandardDeviation = 3;

	UPROPERTY(Config, EditAnywhere, Category ="UI", meta=(ClampMin=0.0f))
	float RefreshRate = 0.2f;

	UPROPERTY(Config)
	ETimedDataMonitorEditorCalibrationType LastCalibrationType = ETimedDataMonitorEditorCalibrationType::CalibrateWithTimecode;
};

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
	JamWithTimecode = 1,
	JamWithPlatformTime = 2,
	Max = 3
};


UCLASS(config = EditorPerProjectUserSettings, MinimalAPI)
class UTimedDataMonitorEditorSettings : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(Config, EditAnywhere, Category ="Timed Data Monitor")
	float RefreshRate = 0.2f;

	UPROPERTY(Config)
	ETimedDataMonitorEditorCalibrationType LastCalibrationType = ETimedDataMonitorEditorCalibrationType::CalibrateWithTimecode;
};

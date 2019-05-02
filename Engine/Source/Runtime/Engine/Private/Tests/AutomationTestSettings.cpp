// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#include "Tests/AutomationTestSettings.h"


UAutomationTestSettings::UAutomationTestSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bTreatLogErrorsAsTestErrors(true)
	, bTreatLogWarningsAsTestErrors(false)
{
	DefaultScreenshotResolution = FIntPoint(1920, 1080);
}

// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "FunctionalTestBase.h"
#include "Tests/AutomationTestSettings.h"


FFunctionalTestBase::FFunctionalTestBase(const FString& InName, const bool bInComplexTask)
	: FAutomationTestBase(InName, bInComplexTask)
{
	bSuppressLogs = false;
	bIsFunctionalTestRunning = false;
	// CDO not available at this point
	bTreatLogErrorsAsErrors = true;
	bTreatLogErrorsAsErrors = false;
}

void FFunctionalTestBase::SetLogErrorAndWarningHandlingToDefault()
{
	// Set to project defaults
	UAutomationTestSettings* Settings = UAutomationTestSettings::StaticClass()->GetDefaultObject<UAutomationTestSettings>();
	bTreatLogErrorsAsErrors = Settings->bTreatLogErrorsAsTestErrors;
	bTreatLogWarningsAsErrors = Settings->bTreatLogWarningsAsTestErrors;
}

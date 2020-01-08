// Copyright Epic Games, Inc. All Rights Reserved.

#include "FunctionalTestBase.h"
#include "AutomationControllerSettings.h"


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
	UAutomationControllerSettings* Settings = UAutomationControllerSettings::StaticClass()->GetDefaultObject<UAutomationControllerSettings>();
	bTreatLogErrorsAsErrors = Settings->bTreatLogErrorsAsTestErrors;
	bTreatLogWarningsAsErrors = Settings->bTreatLogWarningsAsTestErrors;
}

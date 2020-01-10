// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

/**
 * Base class for Functional test cases.  
 */
class FUNCTIONALTESTING_API FFunctionalTestBase : public FAutomationTestBase
{
public:
	FFunctionalTestBase(const FString& InName, const bool bInComplexTask);

	/**
	 * If true logs will not be included in test events
	 *
	 * @return true to suppress logs
	 */
	virtual bool SuppressLogs()
	{
		return bSuppressLogs || !IsFunctionalTestRunning();
	}

	/**
	 * Specify how log errors & warnings should be handled during tests. If values are not set then the project
	 * defaults will be used.
	 */
	void SetLogErrorAndWarningHandling(TOptional<bool> LogErrorsAreErrors, TOptional<bool> LogWarningsAreErrors)
	{
		SetLogErrorAndWarningHandlingToDefault();
	}
	

	/**
	 * Determines if Error logs should be considered test errors
	 */
	virtual bool TreatLogErrorsAsErrors() override
	{
		return bTreatLogErrorsAsErrors;
	}

	/**
	 * Determines if Warning logs should be considered test errors
	 */
	virtual bool TreatLogWarningsAsErrors() override
	{
		return bTreatLogWarningsAsErrors;
	}

	/**
	 * Returns true if a functional test is running (does not include map setup)
	 */
	bool IsFunctionalTestRunning()
	{
		return bIsFunctionalTestRunning;
	}

	/**
	 * Marks us as actively running a functional test
	 */
	void SetFunctionalTestRunning(bool bIsRunning)
	{
		bIsFunctionalTestRunning = bIsRunning;
	}

protected:

	void SetLogErrorAndWarningHandlingToDefault();


	bool bTreatLogErrorsAsErrors;
	bool bTreatLogWarningsAsErrors;
	bool bSuppressLogs;
	bool bIsFunctionalTestRunning;
};

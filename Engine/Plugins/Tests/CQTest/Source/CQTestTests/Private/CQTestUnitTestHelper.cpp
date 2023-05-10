// Copyright Epic Games, Inc. All Rights Reserved.

#include "CQTestUnitTestHelper.h"

void ClearExpectedError(FAutomationTestBase& TestRunner, const FString& ExpectedError)
{
	FAutomationTestExecutionInfo testInfo;
	TestRunner.GetExecutionInfo(testInfo);
	if (testInfo.GetErrorTotal() != 1)
	{
		return;
	}
	testInfo.RemoveAllEvents([&ExpectedError](FAutomationEvent& event) {
		return event.Message.Equals(ExpectedError);
		});
	if (testInfo.GetErrorTotal() == 0)
	{
		TestRunner.ClearExecutionInfo();
	}
}

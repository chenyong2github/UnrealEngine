// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Misc/AutomationTest.h"

// When testing the framework, occasionally we need the error to live on the test framework
// for short-circuiting logic.  We use this method to manually clear the error
// (Normally from AFTER_EACH) to cause the test to pass
void ClearExpectedError(FAutomationTestBase& TestRunner, const FString& ExpectedError);

// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "CoreMinimal.h"
#include "LensFile.h"
#include "LensInterpolationUtils.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTestCameraCalibrationCore, "Plugins.CameraCalibrationCore", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)


namespace CameraCalibrationTestUtil
{
	
}


bool FTestCameraCalibrationCore::RunTest(const FString& Parameters)
{
    return true;
}
#endif // WITH_DEV_AUTOMATION_TESTS




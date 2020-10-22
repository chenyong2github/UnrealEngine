// Copyright Epic Games, Inc. All Rights Reserved.

#include "CameraShakeTestObjects.h"
#include "DefaultCameraShakeBase.h"
#include "WaveOscillatorCameraShakePattern.h"
#include "Misc/AutomationTest.h"

#define LOCTEXT_NAMESPACE "CameraShakeTests"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCameraShakeNullTest, 
		"System.Engine.Cameras.NullCameraShake", 
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCameraShakeNullTest::RunTest(const FString& Parameters)
{
	FMinimalViewInfo ViewInfo;
	UConstantCameraShake* TestShake = NewObject<UConstantCameraShake>();
	TestShake->Duration = 2.f;
	TestShake->StartShake(nullptr, 1.f, ECameraShakePlaySpace::CameraLocal);
	TestShake->UpdateAndApplyCameraShake(1.f, 1.f, ViewInfo);
	UTEST_EQUAL("Location offset", ViewInfo.Location, FVector::ZeroVector);
	UTEST_EQUAL("Rotation offset", ViewInfo.Rotation, FRotator::ZeroRotator);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCameraShakeLocalOffsetTest,
	"System.Engine.Cameras.LocalOffsetCameraShake",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCameraShakeLocalOffsetTest::RunTest(const FString& Parameters)
{
	FMinimalViewInfo ViewInfo;
	ViewInfo.Location = FVector(100, 200, 50);
	ViewInfo.Rotation = FRotator(0, 90, 0);
	UConstantCameraShake* TestShake = NewObject<UConstantCameraShake>();
	TestShake->Duration = 2.f;
	TestShake->LocationOffset = { 10, 0, 0 };
	TestShake->StartShake(nullptr, 1.f, ECameraShakePlaySpace::CameraLocal);
	TestShake->UpdateAndApplyCameraShake(1.f, 1.f, ViewInfo);
	UTEST_EQUAL("Location offset", ViewInfo.Location, FVector(100, 210, 50));
	UTEST_EQUAL("Rotation offset", ViewInfo.Rotation, FRotator(0, 90, 0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCameraShakeWorldOffsetTest,
	"System.Engine.Cameras.WorldOffsetCameraShake",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
	bool FCameraShakeWorldOffsetTest::RunTest(const FString& Parameters)
{
	FMinimalViewInfo ViewInfo;
	ViewInfo.Location = FVector(100, 200, 50);
	ViewInfo.Rotation = FRotator(0, 90, 0);
	UConstantCameraShake* TestShake = NewObject<UConstantCameraShake>();
	TestShake->Duration = 2.f;
	TestShake->LocationOffset = { 10, 0, 0 };
	TestShake->StartShake(nullptr, 1.f, ECameraShakePlaySpace::World);
	TestShake->UpdateAndApplyCameraShake(1.f, 1.f, ViewInfo);
	UTEST_EQUAL("Location offset", ViewInfo.Location, FVector(110, 200, 50));
	UTEST_EQUAL("Rotation offset", ViewInfo.Rotation, FRotator(0, 90, 0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCameraShakeUserDefinedOffsetTest,
	"System.Engine.Cameras.UserDefinedOffsetCameraShake",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCameraShakeUserDefinedOffsetTest::RunTest(const FString& Parameters)
{
	FMinimalViewInfo ViewInfo;
	ViewInfo.Location = FVector(100, 200, 50);
	ViewInfo.Rotation = FRotator(0, 90, 0);
	UConstantCameraShake* TestShake = NewObject<UConstantCameraShake>();
	TestShake->Duration = 2.f;
	TestShake->LocationOffset = { 10, 0, 0 };
	FRotator UserPlaySpaceRot(90, 0, 0);
	TestShake->StartShake(nullptr, 1.f, ECameraShakePlaySpace::UserDefined, UserPlaySpaceRot);
	TestShake->UpdateAndApplyCameraShake(1.f, 1.f, ViewInfo);
	UTEST_EQUAL("Location offset", ViewInfo.Location, FVector(100, 200, 60));
	UTEST_EQUAL("Rotation offset", ViewInfo.Rotation, FRotator(0, 90, 0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCameraShakeSingleInstanceRestartTest,
	"System.Engine.Cameras.SingleInstanceShakeRestart",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCameraShakeSingleInstanceRestartTest::RunTest(const FString& Parameters)
{
	UCameraShakeBase* TestShake = NewObject<UDefaultCameraShakeBase>();
	UWaveOscillatorCameraShakePattern* OscPattern = TestShake->ChangeRootShakePattern<UWaveOscillatorCameraShakePattern>();
	OscPattern->BlendInTime = 1.f;
	OscPattern->BlendOutTime = 2.f;
	OscPattern->Duration = 5.f;
	OscPattern->X.Amplitude = 8.f;
	OscPattern->X.Frequency = 1.f;
	OscPattern->X.InitialOffsetType = EInitialWaveOscillatorOffsetType::Zero;
	TestShake->bSingleInstance = true;

	// Frequency is one oscillation per second, so:
	//  0 at 0sec (0)
	//  1 at 0.25sec (PI/2)
	//  0 at 0.5sec (PI)
	// -1 at 0.75sec (3*PI/2)
	//  0 at 1sec (2*PI)

	FMinimalViewInfo ViewInfo;
	TestShake->StartShake(nullptr, 1.f, ECameraShakePlaySpace::CameraLocal);

	const float Tolerance =	KINDA_SMALL_NUMBER;

	// Go to 0.25sec.
	ViewInfo.Location = FVector::ZeroVector;
	ViewInfo.Rotation = FRotator::ZeroRotator;
	TestShake->UpdateAndApplyCameraShake(0.25f, 1.f, ViewInfo);
	UTEST_EQUAL_TOLERANCE("First update", ViewInfo.Location.X, 0.25f * 8.f * FMath::Sin(PI / 2.f), Tolerance);

	// Go to 0.5sec.
	ViewInfo.Location = FVector::ZeroVector;
	ViewInfo.Rotation = FRotator::ZeroRotator;
	TestShake->UpdateAndApplyCameraShake(0.25f, 1.f, ViewInfo);
	UTEST_EQUAL_TOLERANCE("Second update", ViewInfo.Location.X, 0.5f * 8.f * FMath::Sin(PI), Tolerance);

	// Go to 1sec.
	ViewInfo.Location = FVector::ZeroVector;
	ViewInfo.Rotation = FRotator::ZeroRotator;
	TestShake->UpdateAndApplyCameraShake(0.5f, 1.f, ViewInfo);
	UTEST_EQUAL_TOLERANCE("Third update", ViewInfo.Location.X, 8.f * FMath::Sin(2.f * PI), Tolerance);

	// Go to 4sec.
	ViewInfo.Location = FVector::ZeroVector;
	ViewInfo.Rotation = FRotator::ZeroRotator;
	TestShake->UpdateAndApplyCameraShake(3.f, 1.f, ViewInfo);
	UTEST_EQUAL_TOLERANCE("Fourth update", ViewInfo.Location.X, 0.5f * 8.f * FMath::Sin(8.f * PI), Tolerance);

	// Restart in the middle of the blend-out... we were at 50% so it should reset us
	// at the equivalent point in the blend-in.
	TestShake->StartShake(nullptr, 1.f, ECameraShakePlaySpace::CameraLocal);
	
	// Go to 0.25sec (but blend-in started at 50% this time, so it will be at 75%).
	ViewInfo.Location = FVector::ZeroVector;
	ViewInfo.Rotation = FRotator::ZeroRotator;
	TestShake->UpdateAndApplyCameraShake(0.25f, 1.f, ViewInfo);
	UTEST_EQUAL_TOLERANCE("Fifth update", ViewInfo.Location.X, 0.75f * 8.f * FMath::Sin(PI / 2.f), Tolerance);

	// Go to 0.5sec (but now the blend-in is finished).
	ViewInfo.Location = FVector::ZeroVector;
	ViewInfo.Rotation = FRotator::ZeroRotator;
	TestShake->UpdateAndApplyCameraShake(0.25f, 1.f, ViewInfo);
	UTEST_EQUAL_TOLERANCE("Sixth update", ViewInfo.Location.X, 8.f * FMath::Sin(PI), Tolerance);

	return true;
}

#undef LOCTEXT_NAMESPACE

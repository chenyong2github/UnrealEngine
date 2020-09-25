// Copyright Epic Games, Inc. All Rights Reserved.

#include "CameraShakeTestObjects.h"
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

#undef LOCTEXT_NAMESPACE

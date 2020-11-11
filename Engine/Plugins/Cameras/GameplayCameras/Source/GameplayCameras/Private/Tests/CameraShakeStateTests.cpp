// Copyright Epic Games, Inc. All Rights Reserved.

#include "Camera/CameraShakeBase.h"
#include "Misc/AutomationTest.h"

#define LOCTEXT_NAMESPACE "CameraShakeStateTests"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCameraShakeStateUpdate, 
		"System.Engine.Cameras.ShakeStateUpdate", 
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCameraShakeStateUpdate::RunTest(const FString& Parameters)
{
	FCameraShakeInfo Info;
	Info.Duration = 1.f;
	Info.BlendIn = 0.2f;
	Info.BlendOut = 0.2f;

	FCameraShakeState State;
	State.Initialize(Info);
	UTEST_EQUAL("Update 1", State.Update(0.1f), 0.5f);
	UTEST_EQUAL("Update 2", State.Update(0.1f), 1.f);
	UTEST_EQUAL("Update 3", State.Update(0.6f), 1.f);
	UTEST_EQUAL("Update 4", State.Update(0.1f), 0.5f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCameraShakeStateRestart, 
		"System.Engine.Cameras.ShakeStateRestart", 
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCameraShakeStateRestart::RunTest(const FString& Parameters)
{
	// Restarting a shake with no blending.
	{
		FCameraShakeInfo Info;
		Info.Duration = 1.f;

		FCameraShakeState State;
		State.Initialize(Info);
		State.Update(0.5f);
		State.Initialize(Info);
		UTEST_EQUAL("ElapsedTime", State.GetElapsedTime(), 0.f);
	}

	// Restarting a shake with blending.
	{
		FCameraShakeInfo Info;
		Info.Duration = 1.f;
		Info.BlendIn = 0.2f;
		Info.BlendOut = 0.3f;

		FCameraShakeState State;
		State.Initialize(Info);
		UTEST_EQUAL("Update", State.Update(0.85f), 0.5f);
		// We were half-way into the blend-out, so we should be half-way
		// into the blend-in as we restart. And the duration would be extended
		// by that lead-in time so that the shake still lasts 1 second overall.
		State.Initialize(Info);
		UTEST_EQUAL("ElapsedTime", State.GetElapsedTime(), 0.1f);
		UTEST_EQUAL("Duration", State.GetDuration(), 1.1f);
	}

	return true;
}

#undef LOCTEXT_NAMESPACE

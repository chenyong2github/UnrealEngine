// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "InputTestFramework.h"
#include "InputTriggers.h"
#include "InputModifiers.h"
#include "Misc/AutomationTest.h"

// Tests focused on individual triggers



constexpr auto BasicTriggerTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;	// TODO: Run as Smoke/Client? No world on RunSmokeTests startup...

// Dumping ground for local trigger tests
static UInputTrigger* TestTrigger = nullptr;
static ETriggerState LastTestTriggerState = ETriggerState::None;

// This will be cleared out by GC as soon as it ticks
template<typename T>
T* ATrigger()
{
	return Cast<T>(TestTrigger = NewObject<T>());
}

void TriggerGetsValue(FInputActionValue Value, float DeltaTime = 0.f)
{
	LastTestTriggerState = ETriggerState::None;

	if (TestTrigger)
	{
		// TODO: Provide an EnhancedPlayerInput
		LastTestTriggerState = TestTrigger->UpdateState(nullptr, Value, DeltaTime);
		TestTrigger->LastValue = Value;
	}
}

// Must declare one of these around a subtest to use TriggerStateIs
#define TRIGGER_SUBTEST(DESC) \
	for(FString ScopedSubTestDescription = TEXT(DESC);ScopedSubTestDescription.Len();ScopedSubTestDescription = "")	// Bodge to create a scoped test description. Usage: TRIGGER_SUBTEST("My Test Description") { TestCode... TriggerStateIs(ETriggerState::Triggered); }

// Forced to true to stop multiple errors from the THEN() TestTrueExpr wrapper
#define TriggerStateIs(STATE) \
	(TestEqual(ScopedSubTestDescription, *UEnum::GetValueAsString(TEXT("EnhancedInput.ETriggerState"), LastTestTriggerState), *UEnum::GetValueAsString(TEXT("EnhancedInput.ETriggerState"), STATE)) || true)


// ******************************
// Delegate firing (notification) tests for device (FKey) based triggers
// ******************************

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInputTriggerPressedTest, "Input.Triggers.Pressed", BasicTriggerTestFlags)

bool FInputTriggerPressedTest::RunTest(const FString& Parameters)
{
	TRIGGER_SUBTEST("1 - Instant trigger on press")
	{
		GIVEN(ATrigger<UInputTriggerPressed>());
		WHEN(TriggerGetsValue(true));
		THEN(TriggerStateIs(ETriggerState::Triggered));
	}

	TRIGGER_SUBTEST("2 - Trigger stops on release")
	{
		GIVEN(ATrigger<UInputTriggerPressed>());
		WHEN(TriggerGetsValue(true));
		AND(TriggerGetsValue(false));
		THEN(TriggerStateIs(ETriggerState::None));
	}

	TRIGGER_SUBTEST("3 - Trigger stops on hold")
	{
		GIVEN(ATrigger<UInputTriggerPressed>());
		WHEN(TriggerGetsValue(true));
		AND(TriggerGetsValue(true));
		THEN(TriggerStateIs(ETriggerState::None));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInputTriggerDownTest, "Input.Triggers.Down", BasicTriggerTestFlags)

bool FInputTriggerDownTest::RunTest(const FString& Parameters)
{
	TRIGGER_SUBTEST("Instant trigger on press")
	{
		GIVEN(ATrigger<UInputTriggerDown>());
		WHEN(TriggerGetsValue(true));
		THEN(TriggerStateIs(ETriggerState::Triggered));
	}

	TRIGGER_SUBTEST("Trigger stops on release")
	{
		GIVEN(ATrigger<UInputTriggerDown>());
		WHEN(TriggerGetsValue(true));
		AND(TriggerGetsValue(false));
		THEN(TriggerStateIs(ETriggerState::None));
	}

	TRIGGER_SUBTEST("Trigger retained on hold")
	{
		GIVEN(ATrigger<UInputTriggerDown>());
		WHEN(TriggerGetsValue(true));
		AND(TriggerGetsValue(true));
		THEN(TriggerStateIs(ETriggerState::Triggered));

		// Then lost on release 
		WHEN(TriggerGetsValue(false));
		THEN(TriggerStateIs(ETriggerState::None));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInputTriggerReleasedTest, "Input.Triggers.Released", BasicTriggerTestFlags)

bool FInputTriggerReleasedTest::RunTest(const FString& Parameters)
{
	TRIGGER_SUBTEST("No trigger on press")
	{
		GIVEN(ATrigger<UInputTriggerReleased>());
		WHEN(TriggerGetsValue(true));
		THEN(TriggerStateIs(ETriggerState::Ongoing));
	}

	TRIGGER_SUBTEST("No trigger on hold")
	{
		WHEN(TriggerGetsValue(true));
		THEN(TriggerStateIs(ETriggerState::Ongoing));
	}

	TRIGGER_SUBTEST("Trigger on release")
	{
		WHEN(TriggerGetsValue(false));
		THEN(TriggerStateIs(ETriggerState::Triggered));
		// But only once
		WHEN(TriggerGetsValue(false));
		THEN(TriggerStateIs(ETriggerState::None));
	}

	TRIGGER_SUBTEST("No trigger for no input")
	{
		GIVEN(ATrigger<UInputTriggerReleased>());
		WHEN(TriggerGetsValue(false));
		THEN(TriggerStateIs(ETriggerState::None));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInputTriggerHoldTest, "Input.Triggers.Hold", BasicTriggerTestFlags)

bool FInputTriggerHoldTest::RunTest(const FString& Parameters)
{
	const float FrameTime = 1.f / 60.f;
	const int HoldFrames = 30;	// Half second hold

	TRIGGER_SUBTEST("Release before threshold frame cancels")
	{
		GIVEN(ATrigger<UInputTriggerHold>())->HoldTimeThreshold = FrameTime * HoldFrames;
		WHEN(TriggerGetsValue(true, FrameTime));
		THEN(TriggerStateIs(ETriggerState::Ongoing));
		WHEN(TriggerGetsValue(false, FrameTime));
		THEN(TriggerStateIs(ETriggerState::None));
	}


	TRIGGER_SUBTEST("Holding to threshold fires trigger")
	{
		GIVEN(ATrigger<UInputTriggerHold>())->HoldTimeThreshold = FrameTime * HoldFrames;
		WHEN(TriggerGetsValue(true, FrameTime));
		for (int HoldFrame = 1; HoldFrame < HoldFrames - 1; ++HoldFrame)
		{
			AND(TriggerGetsValue(true, FrameTime));
			THEN(TriggerStateIs(ETriggerState::Ongoing));
		}
		WHEN(TriggerGetsValue(true, FrameTime));
		THEN(TriggerStateIs(ETriggerState::Triggered));

		// Continues to fire
		WHEN(TriggerGetsValue(true, FrameTime));
		THEN(TriggerStateIs(ETriggerState::Triggered));

		// Release stops fire
		WHEN(TriggerGetsValue(false, FrameTime));
		THEN(TriggerStateIs(ETriggerState::None));
	}

	TRIGGER_SUBTEST("One shot trigger")
	{
		UInputTriggerHold* Trigger =
		GIVEN(ATrigger<UInputTriggerHold>());
		Trigger->HoldTimeThreshold = FrameTime * HoldFrames;
		Trigger->bIsOneShot = true;
		for (int HoldFrame = 0; HoldFrame < HoldFrames - 1; ++HoldFrame)
		{
			AND(TriggerGetsValue(true, FrameTime));
		}
		WHEN(TriggerGetsValue(true, FrameTime));
		THEN(TriggerStateIs(ETriggerState::Triggered));

		// Stops firing
		WHEN(TriggerGetsValue(true, FrameTime));
		THEN(TriggerStateIs(ETriggerState::None));
	}

	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInputTriggerHoldAndReleaseTest, "Input.Triggers.HoldAndRelease", BasicTriggerTestFlags)

bool FInputTriggerHoldAndReleaseTest::RunTest(const FString& Parameters)
{
	const float FrameTime = 1.f / 60.f;
	const int HoldFrames = 30;	// Half second hold

	TRIGGER_SUBTEST("Release before threshold frame does not trigger")
	{
		GIVEN(ATrigger<UInputTriggerHoldAndRelease>())->HoldTimeThreshold = FrameTime * HoldFrames;
		WHEN(TriggerGetsValue(true, FrameTime));
		THEN(TriggerStateIs(ETriggerState::Ongoing));
		WHEN(TriggerGetsValue(false, FrameTime));
		THEN(TriggerStateIs(ETriggerState::None));
	}

	TRIGGER_SUBTEST("Holding to threshold frame triggers")
	{
		// Hold to frame 29, release frame 30

		GIVEN(ATrigger<UInputTriggerHoldAndRelease>())->HoldTimeThreshold = FrameTime * HoldFrames;
		for (int HoldFrame = 0; HoldFrame < HoldFrames - 1; ++HoldFrame)
		{
			WHEN(TriggerGetsValue(true, FrameTime));
			THEN(TriggerStateIs(ETriggerState::Ongoing));
		}
		WHEN(TriggerGetsValue(false, FrameTime));
		THEN(TriggerStateIs(ETriggerState::Triggered));

		// Ticking a further frame resets the trigger
		WHEN(TriggerGetsValue(false, FrameTime));
		THEN(TriggerStateIs(ETriggerState::None));
	}


	TRIGGER_SUBTEST("Holding beyond threshold frame triggers")
	{
		// Hold to frame 30, release frame 31.

		GIVEN(ATrigger<UInputTriggerHoldAndRelease>())->HoldTimeThreshold = FrameTime * HoldFrames;
		for (int HoldFrame = 0; HoldFrame < HoldFrames; ++HoldFrame)
		{
			WHEN(TriggerGetsValue(true, FrameTime));
			THEN(TriggerStateIs(ETriggerState::Ongoing));
		}
		WHEN(TriggerGetsValue(false, FrameTime));
		THEN(TriggerStateIs(ETriggerState::Triggered));

		// Ticking a further frame resets the trigger
		WHEN(TriggerGetsValue(false, FrameTime));
		THEN(TriggerStateIs(ETriggerState::None));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInputTriggerTapTest, "Input.Triggers.Tap", BasicTriggerTestFlags)

bool FInputTriggerTapTest::RunTest(const FString& Parameters)
{
	const float FrameTime = 1.f / 60.f;
	const int MaxTapFrames = 10;

	TRIGGER_SUBTEST("Releasing on first frame fires trigger")
	{
		GIVEN(ATrigger<UInputTriggerTap>()->TapReleaseTimeThreshold = FrameTime * MaxTapFrames);

		// Pressing
		WHEN(TriggerGetsValue(true, FrameTime));
		THEN(TriggerStateIs(ETriggerState::Ongoing));

		// Releasing immediately
		WHEN(TriggerGetsValue(false, FrameTime));
		THEN(TriggerStateIs(ETriggerState::Triggered));

		// Ticking a further frame resets the trigger
		WHEN(TriggerGetsValue(false, FrameTime));
		THEN(TriggerStateIs(ETriggerState::None));
	}

	TRIGGER_SUBTEST("Releasing on final frame fires trigger")
	{
		// Hold to frame 9, release on frame 10 = trigger.

		GIVEN(ATrigger<UInputTriggerTap>()->TapReleaseTimeThreshold = FrameTime * MaxTapFrames);
		// Holding until last trigger frame
		for (int HoldFrame = 0; HoldFrame < MaxTapFrames - 1; ++HoldFrame)
		{
			WHEN(TriggerGetsValue(true, FrameTime));
			THEN(TriggerStateIs(ETriggerState::Ongoing));
		}
		// Releasing
		WHEN(TriggerGetsValue(false, FrameTime));
		THEN(TriggerStateIs(ETriggerState::Triggered));

		// Ticking a further frame resets the trigger
		WHEN(TriggerGetsValue(false, FrameTime));
		THEN(TriggerStateIs(ETriggerState::None));
	}

	TRIGGER_SUBTEST("Holding beyond final frame cancels trigger")
	{
		//Hold to frame 9, canceled on frame 10 as still actuated.

		GIVEN(ATrigger<UInputTriggerTap>()->TapReleaseTimeThreshold = FrameTime * MaxTapFrames);
		// Holding until last trigger frame
		for (int HoldFrame = 0; HoldFrame < MaxTapFrames - 1; ++HoldFrame)
		{
			WHEN(TriggerGetsValue(true, FrameTime));
			THEN(TriggerStateIs(ETriggerState::Ongoing));
		}
		// Holding past threshold
		WHEN(TriggerGetsValue(true, FrameTime));
		THEN(TriggerStateIs(ETriggerState::None));

		// Doesn't transition back to Ongoing
		WHEN(TriggerGetsValue(true, FrameTime));
		THEN(TriggerStateIs(ETriggerState::None));

		// Releasing doesn't trigger
		WHEN(TriggerGetsValue(false, FrameTime));
		THEN(TriggerStateIs(ETriggerState::None));
	}

	TRIGGER_SUBTEST("Releasing immediately after final frame doesn't tick")
	{
		//Hold to frame 10, release frame 11.

		GIVEN(ATrigger<UInputTriggerTap>()->TapReleaseTimeThreshold = FrameTime * MaxTapFrames);
		// Holding until last trigger frame
		for (int HoldFrame = 0; HoldFrame < MaxTapFrames - 1 ; ++HoldFrame)
		{
			WHEN(TriggerGetsValue(true, FrameTime));
			THEN(TriggerStateIs(ETriggerState::Ongoing));
		}

		// Holding past threshold
		WHEN(TriggerGetsValue(true, FrameTime));
		THEN(TriggerStateIs(ETriggerState::None));

		// Releasing doesn't trigger
		WHEN(TriggerGetsValue(false, FrameTime));
		THEN(TriggerStateIs(ETriggerState::None));
	}

	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInputTriggerChordedActionsTest, "Input.Triggers.ChordedActions", BasicTriggerTestFlags)

bool FInputTriggerChordedActionsTest::RunTest(const FString& Parameters)
{
	// Test chording changing the triggered action

	FKey ChordKey = TestKey2;
	FName BaseAction = TEXT("BaseAction");				// Base action e.g. Jump
	FName ChordedAction = TEXT("ChordedAction");		// Special case action  e.g. Back flip
	FName ChordingAction = TEXT("ChordingAction");		// Chording action driving special case e.g. ShiftDown/AcrobaticModifier

	UWorld* World =
	GIVEN(AnEmptyWorld());

	// Initialise
	UControllablePlayer& Data =
	AND(AControllablePlayer(World));

	FName BaseContext = TEXT("BaseContext"), ChordContext = TEXT("ChordContext");
	AND(AnInputContextIsAppliedToAPlayer(Data, BaseContext, 1));
	AND(AnInputContextIsAppliedToAPlayer(Data, ChordContext, 100));

	// Set up actions
	AND(AnInputAction(Data, BaseAction, EInputActionValueType::Boolean));
	AND(AnInputAction(Data, ChordedAction, EInputActionValueType::Boolean));

	// Set up the chording action (modifier key action)
	AND(UInputAction * ChordingActionPtr = AnInputAction(Data, ChordingAction, EInputActionValueType::Boolean));

	// Apply a chord action trigger to the chorded action
	UInputTriggerChordAction* Trigger = NewObject<UInputTriggerChordAction>();
	Trigger->ChordAction = ChordingActionPtr;
	AND(ATriggerIsAppliedToAnAction(Data, Trigger, ChordedAction));

	// Bind the chording modifier
	AND(AnActionIsMappedToAKey(Data, ChordContext, ChordingAction, ChordKey));

	// Bind both actions to the same key
	AND(AnActionIsMappedToAKey(Data, BaseContext, BaseAction, TestKey));
	AND(AnActionIsMappedToAKey(Data, ChordContext, ChordedAction, TestKey));

	// With chord key pressed no main actions trigger, but chording action does
	WHEN(AKeyIsActuated(Data, ChordKey));
	AND(InputIsTicked(Data));
	THEN(PressingKeyDoesNotTrigger(Data, BaseAction));
	ANDALSO(PressingKeyTriggersAction(Data, ChordingAction));
	ANDALSO(PressingKeyDoesNotTrigger(Data, ChordedAction));

	// Switching to test key the base action only triggers
	WHEN(AKeyIsReleased(Data, ChordKey));
	AND(AKeyIsActuated(Data, TestKey));
	AND(InputIsTicked(Data));
	THEN(PressingKeyTriggersAction(Data, BaseAction));
	ANDALSO(ReleasingKeyTriggersCompleted(Data, ChordingAction));
	ANDALSO(ReleasingKeyDoesNotTrigger(Data, ChordedAction));

	// Depressing chord key triggers chorded action, and ends base action
	WHEN(AKeyIsActuated(Data, ChordKey));
	AND(InputIsTicked(Data));
	THEN(PressingKeyTriggersCompleted(Data, BaseAction));
	ANDALSO(PressingKeyTriggersAction(Data, ChordingAction));
	ANDALSO(PressingKeyTriggersAction(Data, ChordedAction));

	// Releasing chord key returns to to base only
	WHEN(AKeyIsReleased(Data, ChordKey));
	AND(InputIsTicked(Data));
	THEN(ReleasingKeyTriggersAction(Data, BaseAction));
	ANDALSO(ReleasingKeyTriggersCompleted(Data, ChordingAction));
	ANDALSO(ReleasingKeyTriggersCompleted(Data, ChordedAction));

	WHEN(AKeyIsReleased(Data, TestKey));
	AND(InputIsTicked(Data));
	THEN(ReleasingKeyTriggersCompleted(Data, BaseAction));
	ANDALSO(ReleasingKeyDoesNotTrigger(Data, ChordingAction));
	ANDALSO(ReleasingKeyDoesNotTrigger(Data, ChordedAction));


	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInputTriggerChordedModifiersTest, "Input.Triggers.ChordedModifiers", BasicTriggerTestFlags)

bool FInputTriggerChordedModifiersTest::RunTest(const FString& Parameters)
{
	// Test applying a different set of modifiers to an action based on chords
	// Unchorded mapping with no modifier
	// Chorded mapping with negate modifiers

	auto GetActionValue = [](UControllablePlayer & Data, FName ActionName)
	{
		return FInputTestHelper::GetActionData(Data, ActionName).GetValue().Get<float>();
	};

	FKey ChordKey = TestKey;
	FName BaseAction = TEXT("BaseAction");				// Base action
	FName ChordingAction = TEXT("ChordingAction");		// Chording action driving special case e.g. ShiftDown/AcrobaticModifier

	UWorld* World =
	GIVEN(AnEmptyWorld());

	// Initialise
	UControllablePlayer& Data =
	AND(AControllablePlayer(World));

	FName BaseContext = TEXT("BaseContext"), ChordContext = TEXT("ChordContext");
	AND(AnInputContextIsAppliedToAPlayer(Data, BaseContext, 1));
	AND(AnInputContextIsAppliedToAPlayer(Data, ChordContext, 100));

	// Set up action
	AND(AnInputAction(Data, BaseAction, EInputActionValueType::Axis1D));

	// Set up the chording action (modifier key action)
	AND(UInputAction * ChordingActionPtr = AnInputAction(Data, ChordingAction, EInputActionValueType::Boolean));

	// Bind the chording modifier in the high priority context
	AND(AnActionIsMappedToAKey(Data, ChordContext, ChordingAction, ChordKey));

	// Bind the action to the same key in both contexts
	AND(AnActionIsMappedToAKey(Data, BaseContext, BaseAction, TestAxis));
	AND(AnActionIsMappedToAKey(Data, ChordContext, BaseAction, TestAxis));

	// But the chorded version inverts the result
	AND(AModifierIsAppliedToAnActionMapping(Data, NewObject<UInputModifierNegate>(), ChordContext, BaseAction, TestAxis));

	// Apply a chord action trigger to the chorded mapping
	UInputTriggerChordAction* Trigger = NewObject<UInputTriggerChordAction>();
	Trigger->ChordAction = ChordingActionPtr;
	AND(ATriggerIsAppliedToAnActionMapping(Data, Trigger, ChordContext, BaseAction, TestAxis));


	// With chord key pressed main action does not trigger, but chording action does
	WHEN(AKeyIsActuated(Data, ChordKey));
	AND(InputIsTicked(Data));
	THEN(PressingKeyDoesNotTrigger(Data, BaseAction));
	ANDALSO(PressingKeyTriggersAction(Data, ChordingAction));

	const float AxisValue = 0.5f;

	// Switching to test key the action supplies the unmodified value
	WHEN(AKeyIsReleased(Data, ChordKey));
	AND(AKeyIsActuated(Data, TestAxis, AxisValue));
	AND(InputIsTicked(Data));
	THEN(PressingKeyTriggersAction(Data, BaseAction));
	ANDALSO(ReleasingKeyTriggersCompleted(Data, ChordingAction));
	AND(TestEqual(TEXT("BaseAction"), FInputTestHelper::GetTriggered<float>(Data, BaseAction), AxisValue));

	// Depressing chord key triggers chorded action modified value
	WHEN(AKeyIsActuated(Data, ChordKey));
	AND(InputIsTicked(Data));
	THEN(PressingKeyTriggersAction(Data, BaseAction));
	ANDALSO(PressingKeyTriggersAction(Data, ChordingAction));
	AND(TestEqual(TEXT("BaseAction"), FInputTestHelper::GetTriggered<float>(Data, BaseAction), -AxisValue));

	return true;
}




// TODO: Action level triggers (simple repeat of device level tests)
// TODO: Variable frame delta tests
// TODO: ActionEventData tests (timing, summed values, etc)
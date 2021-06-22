// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Library/DMXLibrary.h"
#include "Library/DMXEntityFixturePatch.h"
#include "DMXEditorUtils.h"

namespace
{
	struct FFixtureTestSetup
	{
		UDMXLibrary*				Library;
		UDMXEntityFixtureType*		DummyFixtureType;
		UDMXEntityFixturePatch*		FirstManualFixture;
		UDMXEntityFixturePatch*		SecondManualFixture;
		UDMXEntityFixturePatch*		FirstAutoFixture;
		UDMXEntityFixturePatch*		SecondAutoFixture;

		FFixtureTestSetup(const int32 FixtureChannelLength)
		{
			Library							= NewObject<UDMXLibrary>();
			DummyFixtureType				= NewObject<UDMXEntityFixtureType>();
			FirstManualFixture				= NewObject<UDMXEntityFixturePatch>();
			SecondManualFixture				= NewObject<UDMXEntityFixturePatch>();
			FirstAutoFixture				= NewObject<UDMXEntityFixturePatch>();
			SecondAutoFixture				= NewObject<UDMXEntityFixturePatch>();

			DummyFixtureType->Modes.Add([FixtureChannelLength]()
				{
					FDMXFixtureMode Result;
					Result.ChannelSpan = FixtureChannelLength;
					return Result;
				}());

			FirstManualFixture->SetAutoAssignAddressUnsafe(false);
			FirstManualFixture->SetManualStartingAddress(1);
			FirstManualFixture->SetUniverseID(1);
			SecondManualFixture->SetAutoAssignAddressUnsafe(false);
			SecondManualFixture->SetManualStartingAddress(9);
			SecondManualFixture->SetUniverseID(1);

			FirstAutoFixture->SetAutoAssignAddressUnsafe(true);
			FirstAutoFixture->SetUniverseID(1);
			SecondAutoFixture->SetAutoAssignAddressUnsafe(true);
			SecondAutoFixture->SetUniverseID(1);

			Library->AddEntity(DummyFixtureType);
			for(UDMXEntityFixturePatch* Patch : PatchesAsArray())
			{
				Patch->SetFixtureType(DummyFixtureType);
				Library->AddEntity(Patch);
			}
		}

		TArray<UDMXEntityFixturePatch*> PatchesAsArray() const
		{
			return { FirstManualFixture, SecondManualFixture, FirstAutoFixture, SecondAutoFixture };
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaceInSingleUniverseTest, "VirtualProduction.DMX.Engine.Editor.AutoAssignAddresses.PlaceInSingleUniverse", (EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter));
bool FPlaceInSingleUniverseTest::RunTest(const FString& Parameters)
{
	FFixtureTestSetup TestSetup(4);

	FDMXEditorUtils::AutoAssignedAddresses({ TestSetup.FirstManualFixture, TestSetup.FirstAutoFixture, TestSetup.SecondAutoFixture }, 1, true);

	TestEqual("Manual fixture was not modified", TestSetup.FirstManualFixture->GetStartingChannel(), 1 );
	TestEqual("Patch was placed in gap", TestSetup.FirstAutoFixture->GetStartingChannel(), 5);
	TestEqual("Patch was placed in after manual", TestSetup.SecondAutoFixture->GetStartingChannel(), 13);

	TestEqual("Universe of first manual patch", TestSetup.FirstManualFixture->GetUniverseID(), 1);
	TestEqual("Universe of first auto patch", TestSetup.FirstAutoFixture->GetUniverseID(), 1);
	TestEqual("Universe of second auto patch", TestSetup.SecondAutoFixture->GetUniverseID(), 1);
	
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRespectsMinimumAddressTest, "VirtualProduction.DMX.Engine.Editor.AutoAssignAddresses.RespectsMinimumAddress", (EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter));
bool FRespectsMinimumAddressTest::RunTest(const FString& Parameters)
{
	FFixtureTestSetup TestSetup(4);

	// SecondManualFixture starts at 9 and ends at 12: MinimumAddress=10 is an edge case.
	FDMXEditorUtils::AutoAssignedAddresses({ TestSetup.FirstAutoFixture, TestSetup.SecondAutoFixture }, 10, true);
	TestEqual("Starting channel of first auto patch", TestSetup.FirstAutoFixture->GetStartingChannel(), 13);
	TestEqual("Starting channel of second auto patch", TestSetup.SecondAutoFixture->GetStartingChannel(), 17);

	FDMXEditorUtils::AutoAssignedAddresses({ TestSetup.SecondAutoFixture }, 125, true);
	TestEqual("Starting channel with available MinimumAddress", TestSetup.SecondAutoFixture->GetStartingChannel(), 125);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShiftToNextUniverseTest, "VirtualProduction.DMX.Engine.Editor.AutoAssignAddresses.ExceedUniverseBounds", (EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter));
bool FShiftToNextUniverseTest::RunTest(const FString& Parameters)
{
	FFixtureTestSetup TestSetup(4);
	TestSetup.FirstManualFixture->SetManualStartingAddress(DMX_UNIVERSE_SIZE - 10);
	// The following is to test for regression bug: If patch A needed to be moved to universe 2, in which patch B resided, patch A's universe would not be updated to 2.
	TestSetup.SecondManualFixture->SetUniverseID(2);
	TestSetup.SecondManualFixture->SetManualStartingAddress(5);

	FDMXEditorUtils::AutoAssignedAddresses({ TestSetup.FirstAutoFixture, TestSetup.SecondAutoFixture }, TestSetup.FirstManualFixture->GetStartingChannel(), true);
	TestEqual("Starting channel of first auto patch", TestSetup.FirstAutoFixture->GetStartingChannel(), DMX_UNIVERSE_SIZE - 6); 
	TestEqual("Starting channel of second auto patch", TestSetup.SecondAutoFixture->GetStartingChannel(), 1);

	TestEqual("Universe of first auto patch", TestSetup.FirstAutoFixture->GetUniverseID(),1 );
	TestEqual("Universe of second auto patch", TestSetup.SecondAutoFixture->GetUniverseID(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFindsGapAtUniverseStart, "VirtualProduction.DMX.Engine.Editor.AutoAssignAddresses.FindsGapAtUniverseStart", (EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter));
bool FFindsGapAtUniverseStart::RunTest(const FString& Parameters)
{
	FFixtureTestSetup TestSetup(1);
	TestSetup.FirstManualFixture->SetManualStartingAddress(2);

	FDMXEditorUtils::AutoAssignedAddresses({ TestSetup.FirstAutoFixture, TestSetup.SecondAutoFixture}, 1, true);

	TestEqual("Starting channel of first auto patch", TestSetup.FirstAutoFixture->GetStartingChannel(), 1); 
	
	return true;
}


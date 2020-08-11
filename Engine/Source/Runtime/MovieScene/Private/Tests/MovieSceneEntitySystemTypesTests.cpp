// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "EntitySystem/MovieSceneEntitySystemTypes.h"
#include "Containers/ArrayView.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#define LOCTEXT_NAMESPACE "MovieSceneEntitySystemTypesTests"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMovieSceneEntityComponentFilterTest, 
		"System.Engine.Sequencer.EntitySystem.EntityComponentFilter", 
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMovieSceneEntityComponentFilterTest::RunTest(const FString& Parameters)
{
	using namespace UE::MovieScene;

	FComponentTypeID ComponentTypes[] = {
		FComponentTypeID::FromBitIndex(0),
		FComponentTypeID::FromBitIndex(1),
		FComponentTypeID::FromBitIndex(2),
		FComponentTypeID::FromBitIndex(3),
		FComponentTypeID::FromBitIndex(4),
		FComponentTypeID::FromBitIndex(5),
		FComponentTypeID::FromBitIndex(6),
		FComponentTypeID::FromBitIndex(7),
	};

	FEntityComponentFilter Filters[4];

	Filters[0].Reset();
	Filters[1].All(FComponentMask({ ComponentTypes[0], ComponentTypes[2] }));
	Filters[2].None(FComponentMask({ ComponentTypes[0], ComponentTypes[2] }));
	Filters[3].Any(FComponentMask({ ComponentTypes[0], ComponentTypes[2] }));
	
	{
		UTEST_TRUE("Filter 1.1", Filters[1].Match(FComponentMask({ ComponentTypes[0], ComponentTypes[2] })));
		UTEST_TRUE("Filter 1.2", Filters[1].Match(FComponentMask({ ComponentTypes[0], ComponentTypes[2], ComponentTypes[3] })));
		UTEST_FALSE("Filter 1.3", Filters[1].Match(FComponentMask({ ComponentTypes[0] })));
		UTEST_FALSE("Filter 1.4", Filters[1].Match(FComponentMask()));
	}

	{
		UTEST_TRUE("Filter 2.1", Filters[2].Match(FComponentMask()));
		UTEST_TRUE("Filter 2.2", Filters[2].Match(FComponentMask({ ComponentTypes[1] })));
		UTEST_TRUE("Filter 2.3", Filters[2].Match(FComponentMask({ ComponentTypes[1], ComponentTypes[3] })));
		UTEST_FALSE("Filter 2.4", Filters[2].Match(FComponentMask({ ComponentTypes[0] })));
		UTEST_FALSE("Filter 2.5", Filters[2].Match(FComponentMask({ ComponentTypes[2], ComponentTypes[3] })));
	}

	{
		UTEST_FALSE("Filter 3.1", Filters[3].Match(FComponentMask()));
		UTEST_FALSE("Filter 3.2", Filters[3].Match(FComponentMask({ ComponentTypes[1] })));
		UTEST_FALSE("Filter 3.3", Filters[3].Match(FComponentMask({ ComponentTypes[1], ComponentTypes[3] })));
		UTEST_TRUE("Filter 3.4", Filters[3].Match(FComponentMask({ ComponentTypes[0] })));
		UTEST_TRUE("Filter 3.5", Filters[3].Match(FComponentMask({ ComponentTypes[2], ComponentTypes[3] })));
	}

	return true;
}

#undef LOCTEXT_NAMESPACE

#endif // WITH_DEV_AUTOMATION_TESTS


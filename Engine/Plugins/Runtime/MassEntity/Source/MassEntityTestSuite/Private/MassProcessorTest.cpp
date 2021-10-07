// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "AITestsCommon.h"

#include "Engine/World.h"
#include "MassEntitySubsystem.h"
#include "MassProcessingTypes.h"
#include "MassEntityTestTypes.h"
#include "MassExecutor.h"

#define LOCTEXT_NAMESPACE "MassTest"

PRAGMA_DISABLE_OPTIMIZATION

namespace FMassProcessorTest
{

template<typename TProcessor>
int32 SimpleProcessorRun(UMassEntitySubsystem& EntitySubsystem)
{
	int32 EntityProcessedCount = 0;
	TProcessor* Processor = NewObject<TProcessor>(&EntitySubsystem);
	Processor->ExecutionFunction = [Processor, &EntityProcessedCount](UMassEntitySubsystem& InEntitySubsystem, FMassExecutionContext& Context) {
		check(Processor);
		Processor->TestGetQuery().ForEachEntityChunk(InEntitySubsystem, Context, [Processor, &EntityProcessedCount](FMassExecutionContext& Context)
			{
				EntityProcessedCount += Context.GetNumEntities();
			});
	};

	FMassProcessingContext ProcessingContext(EntitySubsystem, /*DeltaSeconds=*/0.f);
	UE::Mass::Executor::Run(*Processor, ProcessingContext);

	return EntityProcessedCount;
}

struct FProcessorTest_NoEntities : FEntityTestBase
{
	virtual bool InstantTest() override
	{
		CA_ASSUME(EntitySubsystem);
		
		int32 EntityProcessedCount = SimpleProcessorRun<UMassTestProcessor_Floats>(*EntitySubsystem);
		AITEST_EQUAL("No entities have been created yet so the processor shouldn't do any work.", EntityProcessedCount, 0);
				
		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FProcessorTest_NoEntities, "System.Mass.Processor.NoEntities");


struct FProcessorTest_NoMatchingEntities : FEntityTestBase
{
	virtual bool InstantTest() override
	{
		CA_ASSUME(EntitySubsystem);

		TArray<FMassEntityHandle> EntitiesCreated;
		EntitySubsystem->BatchCreateEntities(IntsArchetype, 100, EntitiesCreated);
		int32 EntityProcessedCount = SimpleProcessorRun<UMassTestProcessor_Floats>(*EntitySubsystem);
		AITEST_EQUAL("No matching entities have been created yet so the processor shouldn't do any work.", EntityProcessedCount, 0);

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FProcessorTest_NoMatchingEntities, "System.Mass.Processor.NoMatchingEntities");

struct FProcessorTest_OneMatchingArchetype : FEntityTestBase
{
	virtual bool InstantTest() override
	{
		CA_ASSUME(EntitySubsystem);

		const int32 EntitiesToCreate = 100;
		TArray<FMassEntityHandle> EntitiesCreated;
		EntitySubsystem->BatchCreateEntities(FloatsArchetype, EntitiesToCreate, EntitiesCreated);
		int32 EntityProcessedCount = SimpleProcessorRun<UMassTestProcessor_Floats>(*EntitySubsystem);
		AITEST_EQUAL("No matching entities have been created yet so the processor shouldn't do any work.", EntityProcessedCount, EntitiesToCreate);

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FProcessorTest_OneMatchingArchetype, "System.Mass.Processor.OneMatchingArchetype");


struct FProcessorTest_MultipleMatchingArchetype : FEntityTestBase
{
	virtual bool InstantTest() override
	{
		CA_ASSUME(EntitySubsystem);

		const int32 EntitiesToCreate = 100;
		TArray<FMassEntityHandle> EntitiesCreated;
		EntitySubsystem->BatchCreateEntities(FloatsArchetype, EntitiesToCreate, EntitiesCreated);
		EntitySubsystem->BatchCreateEntities(FloatsIntsArchetype, EntitiesToCreate, EntitiesCreated);
		EntitySubsystem->BatchCreateEntities(IntsArchetype, EntitiesToCreate, EntitiesCreated);
		// note that only two of these archetypes match 
		int32 EntityProcessedCount = SimpleProcessorRun<UMassTestProcessor_Floats>(*EntitySubsystem);
		AITEST_EQUAL("Two of given three archetypes should match.", EntityProcessedCount, EntitiesToCreate * 2);

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FProcessorTest_MultipleMatchingArchetype, "System.Mass.Processor.MultipleMatchingArchetype");


struct FProcessorTest_Requirements : FEntityTestBase
{
	virtual bool InstantTest() override
	{
		// add an array to the configurable ExecutionFunction an

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FProcessorTest_Requirements, "System.Mass.Processor.Requirements");


} // FMassProcessorTestTest

PRAGMA_ENABLE_OPTIMIZATION

#undef LOCTEXT_NAMESPACE


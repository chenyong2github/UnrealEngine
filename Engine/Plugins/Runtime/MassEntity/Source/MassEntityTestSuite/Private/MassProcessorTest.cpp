// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "AITestsCommon.h"

#include "Engine/World.h"
#include "MassEntitySubsystem.h"
#include "MassEntityTypes.h"
#include "MassEntityTestTypes.h"
#include "MassExecutor.h"

#define LOCTEXT_NAMESPACE "PipeTest"

PRAGMA_DISABLE_OPTIMIZATION

namespace FPipeProcessorTest
{

template<typename TProcessor>
int32 SimpleProcessorRun(UMassEntitySubsystem& EntitySubsystem)
{
	int32 EntityProcessedCount = 0;
	TProcessor* Processor = NewObject<TProcessor>(&EntitySubsystem);
	Processor->ExecutionFunction = [Processor, &EntityProcessedCount](UMassEntitySubsystem& InEntitySubsystem, FLWComponentSystemExecutionContext& Context) {
		check(Processor);
		Processor->TestGetQuery().ForEachEntityChunk(InEntitySubsystem, Context, [Processor, &EntityProcessedCount](FLWComponentSystemExecutionContext& Context)
			{
				EntityProcessedCount += Context.GetEntitiesNum();
			});
	};

	FPipeContext PipeContext(EntitySubsystem, /*DeltaSeconds=*/0.f);
	UE::Pipe::Executor::Run(*Processor, PipeContext);

	return EntityProcessedCount;
}

struct FProcessorTest_NoEntities : FEntityTestBase
{
	virtual bool InstantTest() override
	{
		CA_ASSUME(EntitySubsystem);
		
		int32 EntityProcessedCount = SimpleProcessorRun<UPipeTestProcessor_Floats>(*EntitySubsystem);
		AITEST_EQUAL("No entities have been created yet so the processor shouldn't do any work.", EntityProcessedCount, 0);
				
		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FProcessorTest_NoEntities, "System.Pipe.Processor.NoEntities");


struct FProcessorTest_NoMatchingEntities : FEntityTestBase
{
	virtual bool InstantTest() override
	{
		CA_ASSUME(EntitySubsystem);

		TArray<FLWEntity> EntitiesCreated;
		EntitySubsystem->BatchCreateEntities(IntsArchetype, 100, EntitiesCreated);
		int32 EntityProcessedCount = SimpleProcessorRun<UPipeTestProcessor_Floats>(*EntitySubsystem);
		AITEST_EQUAL("No matching entities have been created yet so the processor shouldn't do any work.", EntityProcessedCount, 0);

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FProcessorTest_NoMatchingEntities, "System.Pipe.Processor.NoMatchingEntities");

struct FProcessorTest_OneMatchingArchetype : FEntityTestBase
{
	virtual bool InstantTest() override
	{
		CA_ASSUME(EntitySubsystem);

		const int32 EntitiesToCreate = 100;
		TArray<FLWEntity> EntitiesCreated;
		EntitySubsystem->BatchCreateEntities(FloatsArchetype, EntitiesToCreate, EntitiesCreated);
		int32 EntityProcessedCount = SimpleProcessorRun<UPipeTestProcessor_Floats>(*EntitySubsystem);
		AITEST_EQUAL("No matching entities have been created yet so the processor shouldn't do any work.", EntityProcessedCount, EntitiesToCreate);

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FProcessorTest_OneMatchingArchetype, "System.Pipe.Processor.OneMatchingArchetype");


struct FProcessorTest_MultipleMatchingArchetype : FEntityTestBase
{
	virtual bool InstantTest() override
	{
		CA_ASSUME(EntitySubsystem);

		const int32 EntitiesToCreate = 100;
		TArray<FLWEntity> EntitiesCreated;
		EntitySubsystem->BatchCreateEntities(FloatsArchetype, EntitiesToCreate, EntitiesCreated);
		EntitySubsystem->BatchCreateEntities(FloatsIntsArchetype, EntitiesToCreate, EntitiesCreated);
		EntitySubsystem->BatchCreateEntities(IntsArchetype, EntitiesToCreate, EntitiesCreated);
		// note that only two of these archetypes match 
		int32 EntityProcessedCount = SimpleProcessorRun<UPipeTestProcessor_Floats>(*EntitySubsystem);
		AITEST_EQUAL("Two of given three archetypes should match.", EntityProcessedCount, EntitiesToCreate * 2);

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FProcessorTest_MultipleMatchingArchetype, "System.Pipe.Processor.MultipleMatchingArchetype");


struct FProcessorTest_Requirements : FEntityTestBase
{
	virtual bool InstantTest() override
	{
		// add an array to the configurable ExecutionFunction an

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FProcessorTest_Requirements, "System.Pipe.Processor.Requirements");


} // FPipeProcessorTestTest

PRAGMA_ENABLE_OPTIMIZATION

#undef LOCTEXT_NAMESPACE


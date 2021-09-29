// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Engine/World.h"
#include "MassEntitySubsystem.h"
#include "MassEntityTypes.h"
#include "MassEntityTestTypes.h"
#include "MassExecutor.h"

#define LOCTEXT_NAMESPACE "PipeTest"

PRAGMA_DISABLE_OPTIMIZATION

//----------------------------------------------------------------------//
// tests 
//----------------------------------------------------------------------//
namespace FPipeExecutionTest
{

struct FExecution_Setup : FExecutionTestBase
{
	virtual bool InstantTest() override
	{
		AITEST_NOT_NULL("World needs to exist for the test to be performed", World);
		AITEST_NOT_NULL("EntitySubsystem needs to be created for the test to be performed", EntitySubsystem);

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FExecution_Setup, "System.Pipe.Execution.Setup");


struct FExecution_EmptyArray : FExecutionTestBase
{
	virtual bool InstantTest() override
	{
		CA_ASSUME(EntitySubsystem); // if EntitySubsystem null InstantTest won't be called at all
		const float DeltaSeconds = 0.f;
		FPipeContext PipeContext(*EntitySubsystem, DeltaSeconds);
		// no test performed, let's just see if it results in errors/warnings
		UE::Pipe::Executor::RunProcessorsView(TArrayView<UPipeProcessor*>(), PipeContext);
		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FExecution_EmptyArray, "System.Pipe.Execution.EmptyArray");


struct FExecution_EmptyPipeline : FExecutionTestBase
{
	virtual bool InstantTest() override
	{
		CA_ASSUME(EntitySubsystem); // if EntitySubsystem null InstantTest won't be called at all
		const float DeltaSeconds = 0.f;
		FPipeContext PipeContext(*EntitySubsystem, DeltaSeconds);
		FRuntimePipeline Pipeline;
		// no test performed, let's just see if it results in errors/warnings
		UE::Pipe::Executor::Run(Pipeline, PipeContext);
		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FExecution_EmptyPipeline, "System.Pipe.Execution.EmptyPipeline");


struct FExecution_InvalidPipeContext : FExecutionTestBase
{
	virtual bool InstantTest() override
	{
		CA_ASSUME(EntitySubsystem);
		const float DeltaSeconds = 0.f;
		FPipeContext PipeContext;
		// test assumption
		AITEST_NULL("FPipeContext\'s default constructor is expected to set FPipeContext.EntitySubsystem to null", PipeContext.EntitySubsystem);
		
		GetTestRunner().AddExpectedError(TEXT("PipeContext.EntitySubsystem is null"), EAutomationExpectedErrorFlags::Contains, 1);		
		// note that using RunProcessorsView is to bypass reasonable tests UE::Pipe::Executor::Run(Pipeline,...) does that are 
		// reported via ensures which are not handled by the automation framework
		UE::Pipe::Executor::RunProcessorsView(TArrayView<UPipeProcessor*>(), PipeContext);
		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FExecution_InvalidPipeContext, "System.Pipe.Execution.InvalidPipeContext");


#if WITH_PIPE_DEBUG
struct FExecution_SingleNullProcessor : FExecutionTestBase
{
	virtual bool InstantTest() override
	{
		CA_ASSUME(EntitySubsystem);
		const float DeltaSeconds = 0.f;
		FPipeContext PipeContext(*EntitySubsystem, DeltaSeconds);
		TArray<UPipeProcessor*> Processors;
		Processors.Add(nullptr);
		

		GetTestRunner().AddExpectedError(TEXT("Processors contains nullptr"), EAutomationExpectedErrorFlags::Contains, 1);
		// note that using RunProcessorsView is to bypass reasonable tests UE::Pipe::Executor::Run(Pipeline,...) does that are 
		// reported via ensures which are not handled by the automation framework
		UE::Pipe::Executor::RunProcessorsView(Processors, PipeContext);
		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FExecution_SingleNullProcessor, "System.Pipe.Execution.SingleNullProcessor");


struct FExecution_SingleValidProcessor : FExecutionTestBase
{
	virtual bool InstantTest() override
	{
		CA_ASSUME(EntitySubsystem);
		const float DeltaSeconds = 0.f;
		FPipeContext PipeContext(*EntitySubsystem, DeltaSeconds);
		UPipeTestProcessorBase* Processor = NewObject<UPipeTestProcessorBase>(EntitySubsystem);
		check(Processor);

		// nothing should break. The actual result of processing is getting tested in PipeProcessorTests.cpp
		UE::Pipe::Executor::Run(*Processor, PipeContext);
		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FExecution_SingleValidProcessor, "System.Pipe.Execution.SingleValidProcessor");


struct FExecution_MultipleNullProcessors : FExecutionTestBase
{
	virtual bool InstantTest() override
	{
		CA_ASSUME(EntitySubsystem);
		const float DeltaSeconds = 0.f;
		FPipeContext PipeContext(*EntitySubsystem, DeltaSeconds);
		TArray<UPipeProcessor*> Processors;
		Processors.Add(nullptr);
		Processors.Add(nullptr);
		Processors.Add(nullptr);

		GetTestRunner().AddExpectedError(TEXT("Processors contains nullptr"), EAutomationExpectedErrorFlags::Contains, 1);
		// note that using RunProcessorsView is to bypass reasonable tests UE::Pipe::Executor::Run(Pipeline,...) does that are 
		// reported via ensures which are not handled by the automation framework
		UE::Pipe::Executor::RunProcessorsView(Processors, PipeContext);
		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FExecution_MultipleNullProcessors, "System.Pipe.Execution.MultipleNullProcessors");
#endif // WITH_PIPE_DEBUG


struct FExecution_Sparse : FEntityTestBase
{
	virtual bool InstantTest() override
	{
		CA_ASSUME(EntitySubsystem);
		const float DeltaSeconds = 0.f;
		FPipeContext PipeContext(*EntitySubsystem, DeltaSeconds);
		UPipeTestProcessorBase* Processor = NewObject<UPipeTestProcessorBase>(EntitySubsystem);
		check(Processor);

		FRuntimePipeline Pipeline;
		{
			TArray<UPipeProcessor*> Processors;
			Processors.Add(Processor);
			Pipeline.SetProcessors(MoveTemp(Processors));
		}

		FArchetypeChunkCollection ChunkCollection(FloatsArchetype);
		// nothing should break. The actual result of processing is getting tested in PipeProcessorTests.cpp
		
		UE::Pipe::Executor::RunSparse(Pipeline, PipeContext, ChunkCollection);

		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FExecution_Sparse, "System.Pipe.Execution.Sparse");
} // FPipeExecutionTest

PRAGMA_ENABLE_OPTIMIZATION

#undef LOCTEXT_NAMESPACE

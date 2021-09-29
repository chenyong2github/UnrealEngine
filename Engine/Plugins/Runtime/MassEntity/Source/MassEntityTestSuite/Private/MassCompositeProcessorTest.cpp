// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "AITestsCommon.h"

#include "Engine/World.h"
#include "MassEntitySystem.h"
#include "MassEntityTypes.h"
#include "MassEntityTestTypes.h"
#include "MassExecutor.h"

#define LOCTEXT_NAMESPACE "PipeTest"

PRAGMA_DISABLE_OPTIMIZATION

namespace FPipeCompositeProcessorTest
{

struct FCompositeProcessorTest_Empty : FEntityTestBase
{
	virtual bool InstantTest() override
	{
		CA_ASSUME(EntitySubsystem);

		UPipeCompositeProcessor* CompositeProcessor = NewObject<UPipeCompositeProcessor>(EntitySubsystem);
		check(CompositeProcessor);
		FPipeContext PipeContext(*EntitySubsystem, /*DeltaSeconds=*/0.f);
		// it should just run, no warnings
		UE::Pipe::Executor::Run(*CompositeProcessor, PipeContext);
		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FCompositeProcessorTest_Empty, "System.Pipe.Processor.Composite.Empty");


struct FCompositeProcessorTest_MultipleSubProcessors : FEntityTestBase
{
	virtual bool InstantTest() override
	{
		CA_ASSUME(EntitySubsystem);

		UPipeCompositeProcessor* CompositeProcessor = NewObject<UPipeCompositeProcessor>(EntitySubsystem);
		check(CompositeProcessor);

		int Result = 0;
		{
			TArray<UPipeProcessor*> Processors;
			for (int i = 0; i < 3; ++i)
			{
				UPipeTestProcessorBase* Processor = NewObject<UPipeTestProcessorBase>(EntitySubsystem);
				Processor->ExecutionFunction = [Processor, &Result, i](UEntitySubsystem& InEntitySubsystem, FLWComponentSystemExecutionContext& Context) {
						check(Processor);
						Result += FMath::Pow(10.f, float(i));
					};
				Processors.Add(Processor);
			}

			CompositeProcessor->SetChildProcessors(MoveTemp(Processors));
		}

		FPipeContext PipeContext(*EntitySubsystem, /*DeltaSeconds=*/0.f);
		UE::Pipe::Executor::Run(*CompositeProcessor, PipeContext);
		AITEST_EQUAL("All of the child processors should get run", Result, 111);
		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FCompositeProcessorTest_MultipleSubProcessors, "System.Pipe.Processor.Composite.MultipleSubProcessors");

} // FPipeCompositeProcessorTest

PRAGMA_ENABLE_OPTIMIZATION

#undef LOCTEXT_NAMESPACE


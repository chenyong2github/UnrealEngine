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

namespace FPipeCompositeProcessorTest
{

struct FCompositeProcessorTest_Empty : FEntityTestBase
{
	virtual bool InstantTest() override
	{
		CA_ASSUME(EntitySubsystem);

		UMassCompositeProcessor* CompositeProcessor = NewObject<UMassCompositeProcessor>(EntitySubsystem);
		check(CompositeProcessor);
		FMassProcessingContext ProcessingContext(*EntitySubsystem, /*DeltaSeconds=*/0.f);
		// it should just run, no warnings
		UE::Pipe::Executor::Run(*CompositeProcessor, ProcessingContext);
		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FCompositeProcessorTest_Empty, "System.Mass.Processor.Composite.Empty");


struct FCompositeProcessorTest_MultipleSubProcessors : FEntityTestBase
{
	virtual bool InstantTest() override
	{
		CA_ASSUME(EntitySubsystem);

		UMassCompositeProcessor* CompositeProcessor = NewObject<UMassCompositeProcessor>(EntitySubsystem);
		check(CompositeProcessor);

		int Result = 0;
		{
			TArray<UMassProcessor*> Processors;
			for (int i = 0; i < 3; ++i)
			{
				UPipeTestProcessorBase* Processor = NewObject<UPipeTestProcessorBase>(EntitySubsystem);
				Processor->ExecutionFunction = [Processor, &Result, i](UMassEntitySubsystem& InEntitySubsystem, FMassExecutionContext& Context) {
						check(Processor);
						Result += FMath::Pow(10.f, float(i));
					};
				Processors.Add(Processor);
			}

			CompositeProcessor->SetChildProcessors(MoveTemp(Processors));
		}

		FMassProcessingContext ProcessingContext(*EntitySubsystem, /*DeltaSeconds=*/0.f);
		UE::Pipe::Executor::Run(*CompositeProcessor, ProcessingContext);
		AITEST_EQUAL("All of the child processors should get run", Result, 111);
		return true;
	}
};
IMPLEMENT_AI_INSTANT_TEST(FCompositeProcessorTest_MultipleSubProcessors, "System.Mass.Processor.Composite.MultipleSubProcessors");

} // FPipeCompositeProcessorTest

PRAGMA_ENABLE_OPTIMIZATION

#undef LOCTEXT_NAMESPACE


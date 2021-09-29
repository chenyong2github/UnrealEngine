// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassEntityTypes.h"
#include "MassProcessor.h"
#include "MassSchematic.h"
#include "Misc/OutputDevice.h"
#include "MassEntityUtils.h"
#include "Engine/EngineBaseTypes.h"
#include "VisualLogger/VisualLogger.h"
#include "MassEntityDebug.h"

DEFINE_LOG_CATEGORY(LogPipe);

//----------------------------------------------------------------------//
//  FPipeContext
//----------------------------------------------------------------------//
FPipeContext::FPipeContext(UEntitySubsystem& InEntities, const float InDeltaSeconds)
	: EntitySubsystem(&InEntities), DeltaSeconds(InDeltaSeconds)
{

}

//----------------------------------------------------------------------//
//  FRuntimePipeline
//----------------------------------------------------------------------//
void FRuntimePipeline::Reset()
{
	Processors.Reset();
}

void FRuntimePipeline::Initialize(UObject& Owner)
{
	for (UPipeProcessor* Proc : Processors)
	{
		if (Proc)
		{
			REDIRECT_OBJECT_TO_VLOG(Proc, &Owner);
			Proc->Initialize(Owner);
		}
	}
}

void FRuntimePipeline::SetProcessors(TArray<UPipeProcessor*>&& InProcessors)
{
	Processors = InProcessors;
}

void FRuntimePipeline::InitializeFromSchematics(TConstArrayView<TSoftObjectPtr<UPipeSchematic>> Schematics, UObject& InOwner)
{
	Reset();
	
	// @todo we'll sometimes end up with duplicated PipeProcessors in the resulting array. We need to come up with a consistent policy for handling that 
	for (const TSoftObjectPtr<UPipeSchematic>& Schematic : Schematics)
	{
		UPipeSchematic* Pipe = Schematic.LoadSynchronous();
		if (Pipe)
		{
			AppendOrOverrideRuntimeProcessorCopies(Pipe->GetProcessors(), InOwner);
		}
		else
		{
			UE_LOG(LogPipe, Error, TEXT("Unable to resolve PipeSchematic %s while creating FRuntimePipeline"), *Schematic.GetLongPackageName());
		}
	}

	Initialize(InOwner);
}

void FRuntimePipeline::CreateFromArray(TConstArrayView<const UPipeProcessor*> InProcessors, UObject& InOwner)
{
	Reset();
	AppendOrOverrideRuntimeProcessorCopies(InProcessors, InOwner);
}

void FRuntimePipeline::InitializeFromArray(TConstArrayView<const UPipeProcessor*> InProcessors, UObject& InOwner)
{
	CreateFromArray(InProcessors, InOwner);
	Initialize(InOwner);
}

void FRuntimePipeline::InitializeFromClassArray(TConstArrayView<TSubclassOf<UPipeProcessor>> InProcessorClasses, UObject& InOwner)
{
	Reset();

	const UWorld* World = InOwner.GetWorld();
	const EProcessorExecutionFlags WorldExecutionFlags = World ? UE::Pipe::Utils::GetProcessorExecutionFlagsForWold(*World) : EProcessorExecutionFlags::All;

	for (const TSubclassOf<UPipeProcessor>& ProcessorClass : InProcessorClasses)
	{
		if (ProcessorClass)
		{
			UPipeProcessor* CDO = ProcessorClass.GetDefaultObject();
			if (CDO && CDO->ShouldExecute(WorldExecutionFlags))
			{
				UPipeProcessor* ProcInstance = NewObject<UPipeProcessor>(&InOwner, ProcessorClass);
				Processors.Add(ProcInstance);
			}
			else
			{
				UE_CVLOG(CDO, &InOwner, LogPipe, Log, TEXT("Skipping %s due to ExecutionFlags"), *CDO->GetName());
			}
		}
	}

	Initialize(InOwner);
}

bool FRuntimePipeline::HasProcessorOfExactClass(TSubclassOf<UPipeProcessor> InClass) const
{
	UClass* TestClass = InClass.Get();
	return Processors.FindByPredicate([TestClass](const UPipeProcessor* Proc){ return Proc != nullptr && Proc->GetClass() == TestClass; }) != nullptr;
}

void FRuntimePipeline::AppendUniqueRuntimeProcessorCopies(TConstArrayView<const UPipeProcessor*> InProcessors, UObject& InOwner)
{
	const UWorld* World = InOwner.GetWorld();
	const EProcessorExecutionFlags WorldExecutionFlags = World ? UE::Pipe::Utils::GetProcessorExecutionFlagsForWold(*World) : EProcessorExecutionFlags::All;
	const int32 StartingCount = Processors.Num();
		
	for (const UPipeProcessor* Proc : InProcessors)
	{
		if (Proc && Proc->ShouldExecute(WorldExecutionFlags)
			&& (Proc->AllowDuplicates() || (HasProcessorOfExactClass(Proc->GetClass()) == false)))
		{
			// unfortunately the const cast is required since NewObject doesn't support const Template object
			UPipeProcessor* ProcCopy = NewObject<UPipeProcessor>(&InOwner, Proc->GetClass(), FName(), RF_NoFlags, const_cast<UPipeProcessor*>(Proc));
			Processors.Add(ProcCopy);
		}
#if WITH_PIPE_DEBUG
		else if (Proc)
		{
			if (Proc->ShouldExecute(WorldExecutionFlags) == false)
			{
				UE_VLOG(&InOwner, LogPipe, Log, TEXT("Skipping %s due to ExecutionFlags"), *Proc->GetName());
			}
			else if (Proc->AllowDuplicates() == false)
			{
				UE_VLOG(&InOwner, LogPipe, Log, TEXT("Skipping %s due to it being a duplicate"), *Proc->GetName());
			}
		}
#endif // WITH_PIPE_DEBUG
	}

	for (int32 NewProcIndex = StartingCount; NewProcIndex < Processors.Num(); ++NewProcIndex)
	{
		UPipeProcessor* Proc = Processors[NewProcIndex];
		check(Proc);
		REDIRECT_OBJECT_TO_VLOG(Proc, &InOwner);
		Proc->Initialize(InOwner);
	}
}

void FRuntimePipeline::AppendOrOverrideRuntimeProcessorCopies(TConstArrayView<const UPipeProcessor*> InProcessors, UObject& InOwner)
{
	const UWorld* World = InOwner.GetWorld();
	const EProcessorExecutionFlags WorldExecutionFlags = World ? UE::Pipe::Utils::GetProcessorExecutionFlagsForWold(*World) : EProcessorExecutionFlags::All;
	
	for (const UPipeProcessor* Proc : InProcessors)
	{
		if (Proc && Proc->ShouldExecute(WorldExecutionFlags))
		{
			// unfortunately the const cast is required since NewObject doesn't support const Template object
			UPipeProcessor* ProcCopy = NewObject<UPipeProcessor>(&InOwner, Proc->GetClass(), FName(), RF_NoFlags, const_cast<UPipeProcessor*>(Proc));
			check(ProcCopy);

			if (ProcCopy->AllowDuplicates())
			{
				// we don't care if there are instances of this class in Processors already
				Processors.Add(ProcCopy);
			}
			else 
			{
				const UClass* TestClass = Proc->GetClass();
				UPipeProcessor** PrevProcessor = Processors.FindByPredicate([TestClass, ProcCopy](const UPipeProcessor* Proc) {
					return Proc != nullptr && Proc->GetClass() == TestClass;
				});

				if (PrevProcessor)
				{
					*PrevProcessor = ProcCopy;
				}
				else
				{
					Processors.Add(ProcCopy);
				}
			}
		}
		else
		{
			UE_CVLOG(Proc, &InOwner, LogPipe, Log, TEXT("Skipping %s due to ExecutionFlags"), *Proc->GetName());
		}
	}
}

void FRuntimePipeline::AppendProcessor(UPipeProcessor& Processor)
{
	Processors.Add(&Processor);
}

UPipeCompositeProcessor* FRuntimePipeline::FindTopLevelGroupByName(FName GroupName)
{
	for (UPipeProcessor* Processor : Processors)
	{
		UPipeCompositeProcessor* CompositeProcessor = Cast<UPipeCompositeProcessor>(Processor);
		if (CompositeProcessor && CompositeProcessor->GetGroupName() == GroupName)
		{
			return CompositeProcessor;
		}
	}
	return nullptr;
}

void FRuntimePipeline::DebugOutputDescription(FOutputDevice& Ar) const
{
	UE::Pipe::Debug::DebugOutputDescription(Processors, Ar);
}

uint32 GetTypeHash(const FRuntimePipeline& Instance)
{ 
	uint32 Hash = 0;
	for (const UPipeProcessor* Proc : Instance.Processors)
	{
		Hash = HashCombine(Hash, PointerHash(Proc));
	}
	return Hash;
}

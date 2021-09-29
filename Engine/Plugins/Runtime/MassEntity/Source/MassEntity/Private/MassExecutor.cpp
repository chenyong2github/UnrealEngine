// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassExecutor.h"
#include "MassEntityTypes.h"
#include "MassProcessor.h"
#include "LWCCommandBuffer.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace UE::Pipe::Executor
{
void Run(FRuntimePipeline& RuntimePipeline, FPipeContext& PipeContext)
{
	if (!ensure(PipeContext.EntitySubsystem) || 
		!ensure(PipeContext.DeltaSeconds >= 0.f) ||
		!ensure(RuntimePipeline.Processors.Find(nullptr) == INDEX_NONE))
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE_STR("PipeExecutor Run Pipeline")
	RunProcessorsView(RuntimePipeline.Processors, PipeContext);
}

void RunSparse(FRuntimePipeline& RuntimePipeline, FPipeContext& PipeContext, FArchetypeHandle Archetype, TConstArrayView<FLWEntity> Entities)
{
	if (!ensure(PipeContext.EntitySubsystem) ||
		!ensure(RuntimePipeline.Processors.Find(nullptr) == INDEX_NONE) ||
		RuntimePipeline.Processors.Num() == 0 ||
		!ensureMsgf(Archetype.IsValid(), TEXT("The Archetype passed in to UE::Pipe::Executor::RunSparse is invalid")))
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE_STR("PipeExecutor RunSparseEntities");

	const FArchetypeChunkCollection ChunkCollection(Archetype, Entities);
	RunProcessorsView(RuntimePipeline.Processors, PipeContext, &ChunkCollection);
}

void RunSparse(FRuntimePipeline& RuntimePipeline, FPipeContext& PipeContext, const FArchetypeChunkCollection& ChunkCollection)
{
	if (!ensure(PipeContext.EntitySubsystem) ||
		!ensure(RuntimePipeline.Processors.Find(nullptr) == INDEX_NONE) ||
		RuntimePipeline.Processors.Num() == 0 ||
		!ensureMsgf(ChunkCollection.GetArchetype().IsValid(), TEXT("The Archetype of ChunkCollection passed in to UE::Pipe::Executor::RunSparse is invalid")))
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE_STR("PipeExecutor RunSparse");

	RunProcessorsView(RuntimePipeline.Processors, PipeContext, &ChunkCollection);
}

void Run(UPipeProcessor& Processor, FPipeContext& PipeContext)
{
	if (!ensure(PipeContext.EntitySubsystem) || !ensure(PipeContext.DeltaSeconds >= 0.f))
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE_STR("PipeExecutor Run")

	UPipeProcessor* ProcPtr = &Processor;
	RunProcessorsView(MakeArrayView(&ProcPtr, 1), PipeContext);
}

void RunProcessorsView(TArrayView<UPipeProcessor*> Processors, FPipeContext& PipeContext, const FArchetypeChunkCollection* ChunkCollection)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(RunProcessorsView);

	if (PipeContext.EntitySubsystem == nullptr)
	{
		UE_LOG(LogPipe, Error, TEXT("%s PipeContext.EntitySubsystem is null. Baling out."), ANSI_TO_TCHAR(__FUNCTION__));
		return;
	}
#if WITH_PIPE_DEBUG
	if (Processors.Find(nullptr) != INDEX_NONE)
	{
		UE_LOG(LogPipe, Error, TEXT("%s input Processors contains nullptr. Baling out."), ANSI_TO_TCHAR(__FUNCTION__));
		return;
	}
#endif // WITH_PIPE_DEBUG

	TRACE_CPUPROFILER_EVENT_SCOPE_STR("PipeExecutor RunProcessorsView")

	FLWComponentSystemExecutionContext ExecutionContext(PipeContext.DeltaSeconds);
	if (ChunkCollection)
	{
		ExecutionContext.SetChunkCollection(*ChunkCollection);
	}
	// manually creating a new command buffer to let the default one still be used by code unaware of pipe processing
	ExecutionContext.SetDeferredCommandBuffer(MakeShareable(new FLWCCommandBuffer()));
	ExecutionContext.SetFlushDeferredCommands(false);
	ExecutionContext.SetAuxData(PipeContext.AuxData);

	{
		TRACE_CPUPROFILER_EVENT_SCOPE_STR("Execute Processors")
		
		UEntitySubsystem::FScopedProcessing ProcessingScope = PipeContext.EntitySubsystem->NewProcessingScope();

		for (UPipeProcessor* Proc : Processors)
		{
			Proc->CallExecute(*PipeContext.EntitySubsystem, ExecutionContext);
		}
	}
	
	{		
		TRACE_CPUPROFILER_EVENT_SCOPE_STR("Flush Deferred Commands")
		
		ExecutionContext.SetFlushDeferredCommands(true);
		// append the commands added from other, non-processor sources (like MassAgentSubsystem)
		ExecutionContext.Defer().MoveAppend(PipeContext.EntitySubsystem->Defer());
		ExecutionContext.FlushDeferred(*PipeContext.EntitySubsystem);
	}
}

struct FPipeExecutorDoneTask
{
	FPipeExecutorDoneTask(const FLWComponentSystemExecutionContext& InExecutionContext, UEntitySubsystem& InEntitySubsystem, TFunction<void()> InOnDoneNotification, const FString& InDebugName)
		: ExecutionContext(InExecutionContext)
		, EntitySubsystem(InEntitySubsystem)
		, OnDoneNotification(InOnDoneNotification)
		, DebugName(InDebugName)
	{
	}
	static TStatId GetStatId()
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FPipeExecutorDoneTask, STATGROUP_TaskGraphTasks);
	}

	static ENamedThreads::Type GetDesiredThread() { return ENamedThreads::GameThread; }
	static ESubsequentsMode::Type GetSubsequentsMode() { return ESubsequentsMode::TrackSubsequents; }
	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE_STR("Flush Deferred Commands Parallel");

		if (&ExecutionContext.Defer() != &EntitySubsystem.Defer())
		{
			ExecutionContext.Defer().MoveAppend(EntitySubsystem.Defer());
		}

		UE_LOG(LogPipe, Log, TEXT("PipeExecutor %s tasks DONE"), *DebugName);
		ExecutionContext.SetFlushDeferredCommands(true);
		ExecutionContext.FlushDeferred(EntitySubsystem);

		OnDoneNotification();
	}
private:
	FLWComponentSystemExecutionContext ExecutionContext;
	UEntitySubsystem& EntitySubsystem;
	TFunction<void()> OnDoneNotification;
	FString DebugName;
};

FGraphEventRef TriggerParallelTasks(UPipeProcessor& Processor, FPipeContext& PipeContext, TFunction<void()> OnDoneNotification)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(RunProcessorsView);

	if (PipeContext.EntitySubsystem == nullptr)
	{
		UE_LOG(LogPipe, Error, TEXT("%s PipeContext.EntitySubsystem is null. Baling out."), ANSI_TO_TCHAR(__FUNCTION__));
		return FGraphEventRef();
	}

	TRACE_CPUPROFILER_EVENT_SCOPE_STR("PipeExecutor RunParallel")

	// not going through UEntitySubsystem::CreateExecutionContext on purpose - we do need a separate command buffer
	FLWComponentSystemExecutionContext ExecutionContext(PipeContext.DeltaSeconds);
	ExecutionContext.SetDeferredCommandBuffer(MakeShareable(new FLWCCommandBuffer()));
	ExecutionContext.SetFlushDeferredCommands(false);
	ExecutionContext.SetAuxData(PipeContext.AuxData);

	FGraphEventRef CompletionEvent;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE_STR("Dispatch Processors")
		CompletionEvent = Processor.DispatchProcessorTasks(*PipeContext.EntitySubsystem, ExecutionContext, {});
	}

	if (CompletionEvent.IsValid())
	{
		const FGraphEventArray Prerequisites = { CompletionEvent };
		CompletionEvent = TGraphTask<FPipeExecutorDoneTask>::CreateTask(&Prerequisites)
			.ConstructAndDispatchWhenReady(ExecutionContext, *PipeContext.EntitySubsystem, OnDoneNotification, Processor.GetName());
	}

	return CompletionEvent;
}

} // namespace UE::Pipe::Executor

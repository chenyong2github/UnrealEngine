// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGGraphExecutor.h"
#include "PCGGraph.h"
#include "PCGData.h"
#include "PCGSubgraph.h"
#include "PCGGraphCompiler.h"
#include "PCGComponent.h"

// World partition support for in-editor workflows needs these includes
#if WITH_EDITOR
#include "WorldPartition/WorldPartition.h"
#include "FileHelpers.h"
#endif

static TAutoConsoleVariable<int32> CVarMaxNumTasks(
	TEXT("pcg.MaxConcurrentTasks"),
	4096,
	TEXT("Maximum number of concurrent tasks for PCG processing"));

FPCGGraphExecutor::FPCGGraphExecutor(UObject* InOwner)
	: GraphCompiler(MakeUnique<FPCGGraphCompiler>())
	, GraphCache(InOwner)
{
}

FPCGGraphExecutor::~FPCGGraphExecutor() = default;

void FPCGGraphExecutor::Compile(UPCGGraph* Graph)
{
	GraphCompiler->Compile(Graph);
}

FPCGTaskId FPCGGraphExecutor::Schedule(UPCGComponent* Component, const TArray<FPCGTaskId>& ExternalDependencies)
{
	check(Component);
	UPCGGraph* Graph = Component->GetGraph();

	return Schedule(Graph, Component, GetFetchInputElement(), ExternalDependencies);
}

FPCGTaskId FPCGGraphExecutor::Schedule(UPCGGraph* Graph, UPCGComponent* SourceComponent, FPCGElementPtr InputElement, const TArray<FPCGTaskId>& ExternalDependencies)
{
	FPCGTaskId ScheduledId = InvalidTaskId;

	// Get compiled tasks from compiler
	TArray<FPCGGraphTask> CompiledTasks = GraphCompiler->GetCompiledTasks(Graph);

	// Assign this component to the tasks
	for (FPCGGraphTask& Task : CompiledTasks)
	{
		Task.SourceComponent = SourceComponent;
	}

	// Prepare scheduled task that will be promoted in the next Execute call.
	if (CompiledTasks.Num() > 0)
	{
		check(CompiledTasks[0].Node == Graph->GetInputNode());

		// Setup fetch task on input node
		CompiledTasks[0].Element = InputElement;

		ScheduleLock.Lock();

		FPCGGraphScheduleTask& ScheduledTask = ScheduledTasks.Emplace_GetRef();
		ScheduledTask.Tasks = MoveTemp(CompiledTasks);

		// Offset task node ids
		FPCGGraphCompiler::OffsetNodeIds(ScheduledTask.Tasks, NextTaskId);
		NextTaskId += ScheduledTask.Tasks.Num();
		ScheduledId = NextTaskId - 1; // This is true because the last task is from the output node or is the post-execute task

#if WITH_EDITOR
		// Push task (not data) dependencies on the pre-execute task
		// Note must be done after the offset ids, otherwise we'll break the dependencies
		check(ScheduledTask.Tasks.Num() >= 2 && ScheduledTask.Tasks[ScheduledTask.Tasks.Num() - 2].Node == nullptr);
		ScheduledTask.Tasks[ScheduledTask.Tasks.Num() - 2].Inputs.Append(ExternalDependencies);
#endif

		ScheduleLock.Unlock();
	}

	return ScheduledId;
}

FPCGTaskId FPCGGraphExecutor::ScheduleGeneric(TFunction<bool()> InOperation, const TArray<FPCGTaskId>& TaskDependencies)
{
	// Build task & element to hold the operation to perform
	FPCGGraphTask Task;
	Task.Inputs.Append(TaskDependencies);
	Task.Element = MakeShared<FPCGGenericElement>(InOperation);

	ScheduleLock.Lock();

	// Assign task id
	Task.NodeId = NextTaskId++;

	FPCGGraphScheduleTask& ScheduledTask = ScheduledTasks.Emplace_GetRef();
	ScheduledTask.Tasks.Add(Task);

	ScheduleLock.Unlock();

	return Task.NodeId;
}

bool FPCGGraphExecutor::GetOutputData(FPCGTaskId TaskId, FPCGDataCollection& OutData)
{
	// TODO: this is not threadsafe - make threadsafe once we multithread execution
	if (OutputData.Contains(TaskId))
	{
		OutData = OutputData[TaskId];
		return true;
	}
	else
	{
		return false;
	}
}

void FPCGGraphExecutor::Execute()
{
	// TODO: change this so we can have time-slicing, priority and thread affinities

	// Process any newly scheduled graphs to execute
	ScheduleLock.Lock();

	for (FPCGGraphScheduleTask& ScheduledTask : ScheduledTasks)
	{
		check(ScheduledTask.Tasks.Num() > 0);

		// Finally, push the tasks to the master list
		Tasks.Append(ScheduledTask.Tasks);
	}

	if (ScheduledTasks.Num() > 0)
	{
		ScheduledTasks.Reset();
		// Kick off any of the newly added ready tasks
		QueueReadyTasks();
	}
	
	ScheduleLock.Unlock();

	// TODO: add optimization phase if we've added new graph(s)/tasks to be executed

	// This is a safeguard to check if we're in a stuck state
	check(Tasks.Num() == 0 || ReadyTasks.Num() > 0 || ActiveTasks.Num() > 0);

	// TODO: change this when we support time-slicing
	if(ReadyTasks.Num() > 0 || ActiveTasks.Num() > 0)
	{
		// First: if we have free resources, move ready tasks to the active tasks
		bool bCanStartNewTasks = true; // TODO implement this

		if (bCanStartNewTasks && ReadyTasks.Num() > 0)
		{
			// Sort by priority
			// TODO

			// Create active tasks according to resources & priority
			while (ReadyTasks.Num() > 0 && bCanStartNewTasks)
			{
				// TODO: peek at task properties to determine if we could start it (esp. thread affinity)
				FPCGGraphTask Task = ReadyTasks.Pop();

				// Build input
				FPCGDataCollection TaskInput;
				BuildTaskInput(Task, TaskInput);

				FPCGElementPtr Element = Task.Element;

				// If the task didn't have a fixed element, acquire one from the node/settings
				if (!Element)
				{
					// Get appropriate settings
					const UPCGSettings* Settings = TaskInput.GetSettings(Task.Node->DefaultSettings);

					if (!Settings)
					{
						// Can't execute; abort
						continue;
					}

					// Create element
					Element = Settings->GetElement();
				}

				if (!Element)
				{
					// Can't execute; abort
					continue;
				}
				
				FPCGContext* Context = Element->Initialize(TaskInput, Task.SourceComponent);
				Context->Node = Task.Node; // still needed?
				Context->TaskId = Task.NodeId;
				Context->Cache = &GraphCache;

				FPCGGraphActiveTask& ActiveTask = ActiveTasks.Emplace_GetRef();
				ActiveTask.Element = Element;
				ActiveTask.Context = TUniquePtr<FPCGContext>(Context);
				ActiveTask.NodeId = Task.NodeId;
				
				// TODO update bCanStartNewTasks
			}
		}

		// Perform execution
		// TODO: this is a naive single-threaded implementation, should be replaced by
		// the task graph or futures w/proper thread assingment
		for (int ExecutionIndex = 0; ExecutionIndex < ActiveTasks.Num(); ++ExecutionIndex)
		{
			FPCGGraphActiveTask& ActiveTask = ActiveTasks[ExecutionIndex];

			// Reassign resources to the context
			// TODO: change this when we support MT in the graph executor
			ActiveTask.Context->NumAvailableTasks = FMath::Max(1, FMath::Min(FPlatformMisc::NumberOfCoresIncludingHyperthreads() - 2, CVarMaxNumTasks.GetValueOnAnyThread()));

			if (!ActiveTask.Context->bIsPaused && ActiveTask.Element->Execute(ActiveTask.Context.Get()))
			{
				// Store output data in map
				StoreResults(ActiveTask.NodeId, ActiveTask.Context->OutputData);

				// Move successor nodes to the ready queue
				QueueReadyTasks(ActiveTask.NodeId);

				// Remove current active task from list
				ActiveTasks.RemoveAt(ExecutionIndex);
				--ExecutionIndex;
			}
		}

		// Nothing left to do; we'll release everything here.
		// TODO: this is fine and will make sure any intermediate data is properly
		// garbage collected, however, this goes a bit against our goals if we want to
		// keep a cache of intermediate results.
		if (ReadyTasks.Num() == 0 && ActiveTasks.Num() == 0 && Tasks.Num() == 0)
		{
			ClearResults();
		}

#if WITH_EDITOR
		// Save & release resources when running in-editor
		SaveDirtyActors();
		ReleaseUnusedActors();
#endif
	}
}

void FPCGGraphExecutor::QueueReadyTasks(FPCGTaskId FinishedTaskHint)
{
	for (int TaskIndex = Tasks.Num() - 1; TaskIndex >= 0; --TaskIndex)
	{
		bool bAllPrerequisitesMet = true;
		for (const FPCGTaskId& Id : Tasks[TaskIndex].Inputs)
		{
			bAllPrerequisitesMet &= OutputData.Contains(Id);
		}

		if (bAllPrerequisitesMet)
		{
			ReadyTasks.Emplace(MoveTemp(Tasks[TaskIndex]));
			Tasks.RemoveAtSwap(TaskIndex);
		}
	}
}

void FPCGGraphExecutor::BuildTaskInput(const FPCGGraphTask& Task, FPCGDataCollection& TaskInput)
{
	for (const FPCGTaskId& Id : Task.Inputs)
	{
		check(OutputData.Contains(Id));
		TaskInput.TaggedData.Append(OutputData[Id].TaggedData);
		TaskInput.bCancelExecution |= OutputData[Id].bCancelExecution;
	}
}

void FPCGGraphExecutor::StoreResults(FPCGTaskId InTaskId, const FPCGDataCollection& InTaskOutput)
{
	// Store output in map
	OutputData.Add(InTaskId, InTaskOutput);

	// Root any non-rooted results, otherwise they'll get garbage-collected
	InTaskOutput.RootUnrootedData(RootedData);
}

void FPCGGraphExecutor::ClearResults()
{
	ScheduleLock.Lock();
	
	NextTaskId = 0;
	OutputData.Reset();

	for (UObject* Data : RootedData)
	{
		Data->RemoveFromRoot();
	}
	RootedData.Reset();

	ScheduleLock.Unlock();
}

FPCGElementPtr FPCGGraphExecutor::GetFetchInputElement()
{
	if (!FetchInputElement)
	{
		FetchInputElement = MakeShared<FPCGFetchInputElement>();
	}

	return FetchInputElement;
}

#if WITH_EDITOR
void FPCGGraphExecutor::AddToDirtyActors(AActor* Actor)
{
	ActorsListLock.Lock();
	ActorsToSave.Add(Actor);
	ActorsListLock.Unlock();
}

void FPCGGraphExecutor::AddToUnusedActors(const TSet<FWorldPartitionReference>& UnusedActors)
{
	ActorsListLock.Lock();
	ActorsToRelease.Append(UnusedActors);
	ActorsListLock.Unlock();
}

void FPCGGraphExecutor::SaveDirtyActors()
{
	ActorsListLock.Lock();
	TSet<AActor*> ToSave = MoveTemp(ActorsToSave);
	ActorsToSave.Reset();
	ActorsListLock.Unlock();

	TSet<UPackage*> PackagesToSave;
	for (AActor* Actor : ToSave)
	{
		PackagesToSave.Add(Actor->GetExternalPackage());
	}

	if (PackagesToSave.Num() > 0)
	{
		UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave.Array(), true);
	}
}

void FPCGGraphExecutor::ReleaseUnusedActors()
{
	ActorsListLock.Lock();
	bool bRunGC = ActorsToRelease.Num() > 0;
	ActorsToRelease.Reset();
	ActorsListLock.Unlock();

	// TODO: maybe consider other checks
	if (bRunGC)
	{
		CollectGarbage(RF_NoFlags, true);
	}
}

void FPCGGraphExecutor::NotifyGraphChanged(UPCGGraph* InGraph)
{
	if (GraphCompiler)
	{
		GraphCompiler->NotifyGraphChanged(InGraph);
	}
}
#endif

bool FPCGFetchInputElement::ExecuteInternal(FPCGContext* Context) const
{
	// First: any input can be passed through to the output trivially
	Context->OutputData = Context->InputData;

	// Second: fetch the inputs provided by the component
	UPCGComponent* Component = Context->SourceComponent;
	check(Component);
	if(UPCGData* PCGData = Component->GetPCGData())
	{
		FPCGTaggedData& TaggedData = Context->OutputData.TaggedData.Emplace_GetRef();
		TaggedData.Data = PCGData;
	}

	return true;
}

FPCGGenericElement::FPCGGenericElement(TFunction<bool()> InOperation)
	: Operation(InOperation)
{
}

bool FPCGGenericElement::ExecuteInternal(FPCGContext* Context) const
{
	return Operation();
}

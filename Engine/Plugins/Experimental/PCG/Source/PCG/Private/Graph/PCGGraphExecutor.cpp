// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGGraphExecutor.h"
#include "PCGComponent.h"
#include "PCGData.h"
#include "PCGHelpers.h"
#include "PCGGraph.h"
#include "PCGGraphCompiler.h"
#include "PCGInputOutputSettings.h"
#include "PCGSubgraph.h"

#include "Async/Async.h"

#if WITH_EDITOR
#include "Editor.h"
#endif

// World partition support for in-editor workflows needs these includes
#if WITH_EDITOR
#include "WorldPartition/WorldPartition.h"
#include "FileHelpers.h"
#endif

static TAutoConsoleVariable<int32> CVarMaxNumTasks(
	TEXT("pcg.MaxConcurrentTasks"),
	4096,
	TEXT("Maximum number of concurrent tasks for PCG processing"));

static TAutoConsoleVariable<float> CVarTimePerFrame(
	TEXT("pcg.FrameTime"),
	1000.0f / 60.0f,
	TEXT("Allocated time in ms per frame"));

static TAutoConsoleVariable<bool> CVarGraphMultithreading(
	TEXT("pcg.GraphMultithreading"),
	false,
	TEXT("Controls whether the graph can dispatch multiple tasks at the same time"));

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
	FPCGTaskId ScheduledId = InvalidPCGTaskId;

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

		// Push task (not data) dependencies on the pre-execute task
		// Note must be done after the offset ids, otherwise we'll break the dependencies
		check(ScheduledTask.Tasks.Num() >= 2 && ScheduledTask.Tasks[ScheduledTask.Tasks.Num() - 2].Node == nullptr);
		for (FPCGTaskId ExternalDependency : ExternalDependencies)
		{
			ScheduledTask.Tasks[ScheduledTask.Tasks.Num() - 2].Inputs.Emplace(ExternalDependency, nullptr, nullptr);
		}

		ScheduleLock.Unlock();
	}

	return ScheduledId;
}

FPCGTaskId FPCGGraphExecutor::ScheduleGeneric(TFunction<bool()> InOperation, const TArray<FPCGTaskId>& TaskDependencies)
{
	// Build task & element to hold the operation to perform
	FPCGGraphTask Task;
	for (FPCGTaskId TaskDependency : TaskDependencies)
	{
		Task.Inputs.Emplace(TaskDependency, nullptr, nullptr);
	}
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
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGGraphExecutor::Execute);

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
	if (ReadyTasks.Num() == 0 && ActiveTasks.Num() == 0 && Tasks.Num() > 0)
	{
		UE_LOG(LogPCG, Error, TEXT("PCG Graph executor error: tasks are in a deadlocked state. Will drop all tasks."));
		Tasks.Reset();
	}

	// TODO: change this if we support tasks that are not framebound
	bool bAnyTaskEnded = false;

	const double StartTime = FPlatformTime::Seconds();
	const int32 MaxNumThreads = FMath::Max(0, FMath::Min(FPlatformMisc::NumberOfCoresIncludingHyperthreads() - 2, CVarMaxNumTasks.GetValueOnAnyThread() - 1));
	const bool bAllowMultiDispatch = CVarGraphMultithreading.GetValueOnAnyThread();

	while(ReadyTasks.Num() > 0 || ActiveTasks.Num() > 0)
	{
		// First: if we have free resources, move ready tasks to the active tasks
		bool bMainThreadAvailable = (ActiveTasks.Num() == 0 || !ActiveTasks.Last().Element->CanExecuteOnlyOnMainThread(ActiveTasks.Last().Context->GetInputSettings<UPCGSettings>()));
		int32 NumAvailableThreads = FMath::Max(0, MaxNumThreads - CurrentlyUsedThreads);

		const bool bMainThreadWasAvailable = bMainThreadAvailable;
		const int32 TasksToLaunchIndex = ActiveTasks.Num();

		bool bSomeTaskEndedInCurrentLoop = false;

		if (bMainThreadAvailable || NumAvailableThreads > 0)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(FPCGGraphExecutor::Execute::PrepareTasks);
			// Sort tasks by priority (highest priority should be at the end)
			// TODO

			for(int32 ReadyTaskIndex = ReadyTasks.Num() - 1; ReadyTaskIndex >= 0; --ReadyTaskIndex)
			{
				FPCGGraphTask& Task = ReadyTasks[ReadyTaskIndex];

				// Build input
				FPCGDataCollection TaskInput;
				BuildTaskInput(Task, TaskInput);

				// Initialize the element if needed (required to know whether it will run on the main thread or not)
				if (!Task.Element)
				{
					// Get appropriate settings
					check(Task.Node);
					const UPCGSettings* Settings = TaskInput.GetSettings(Task.Node->DefaultSettings);

					if (Settings)
					{
						Task.Element = Settings->GetElement();
					}
				}

				// At this point, if the task doesn't have an element, we will never be able to execute it, so we can drop it.
				if (!Task.Element)
				{
					ReadyTasks.RemoveAtSwap(ReadyTaskIndex);
					continue;
				}

				// If a task is cacheable and has been cached, then we don't need to create an active task for it unless
				// there is an execution mode that would prevent us from doing so.
				const UPCGSettings* TaskSettings = PCGContextHelpers::GetInputSettings<UPCGSettings>(Task.Node, TaskInput);
				FPCGDataCollection CachedOutput;
				const bool bResultAlreadyInCache = Task.Element->IsCacheable(TaskSettings) && GraphCache.GetFromCache(Task.Element.Get(), TaskInput, TaskSettings, CachedOutput);
#if WITH_EDITOR
				const bool bNeedsToCreateActiveTask = !bResultAlreadyInCache || TaskSettings->ExecutionMode == EPCGSettingsExecutionMode::Debug || TaskSettings->ExecutionMode == EPCGSettingsExecutionMode::Isolated;
#else
				const bool bNeedsToCreateActiveTask = !bResultAlreadyInCache;
#endif

				if (!bNeedsToCreateActiveTask)
				{
					// Fast-forward cached result to stored results
					StoreResults(Task.NodeId, CachedOutput);
					ReadyTasks.RemoveAtSwap(ReadyTaskIndex);
					bSomeTaskEndedInCurrentLoop = true;
#if WITH_EDITOR
					UPCGComponent* SourceComponent = Task.SourceComponent;
					if (SourceComponent && SourceComponent->IsInspecting())
					{
						SourceComponent->StoreInspectionData(Task.Node, CachedOutput);
					}
#endif
					
					continue;
				}

				// Validate that we can start this task now
				const bool bIsMainThreadTask = Task.Element->CanExecuteOnlyOnMainThread(TaskSettings);

				if (!bIsMainThreadTask || bMainThreadAvailable)
				{
					const bool bInsertLast = (bIsMainThreadTask || bMainThreadAvailable || ActiveTasks.Num() == 0);
					FPCGGraphActiveTask& ActiveTask = (bInsertLast ? ActiveTasks.Emplace_GetRef() : ActiveTasks.EmplaceAt_GetRef(ActiveTasks.Num() - 1));

					ActiveTask.Element = Task.Element;
					ActiveTask.NodeId = Task.NodeId;

					FPCGContext* Context = Task.Element->Initialize(TaskInput, Task.SourceComponent, Task.Node);
					Context->TaskId = Task.NodeId;
					Context->Cache = &GraphCache;
					ActiveTask.Context = TUniquePtr<FPCGContext>(Context);

#if WITH_EDITOR
					if (bResultAlreadyInCache)
					{
						ActiveTask.bIsBypassed = true;
						Context->OutputData = CachedOutput;
					}
#endif

					if (bIsMainThreadTask || NumAvailableThreads == 0)
					{
						bMainThreadAvailable = false;
					}
					else
					{
						--NumAvailableThreads;
					}

					ReadyTasks.RemoveAtSwap(ReadyTaskIndex);

					if (!bAllowMultiDispatch || (!bMainThreadAvailable && NumAvailableThreads == 0))
					{
						break;
					}
				}
			}
		}

		check(NumAvailableThreads >= 0);

		const int32 NumTasksToLaunch = ActiveTasks.Num() - TasksToLaunchIndex;

		// Re-launch time-sliced tasks & launch new tasks
		// TODO: currently we don't have any time-slicing so just launch tasks
		const int32 LastLaunchIndex = ActiveTasks.Num() - (bMainThreadWasAvailable ? 0 : 1);
		
		// Assign resources
		const int32 NumAdditionalThreads = ((NumTasksToLaunch > 0) ? (NumAvailableThreads / NumTasksToLaunch) : 0);
		check(NumAdditionalThreads >= 0);

		for (int32 ExecutionIndex = TasksToLaunchIndex; ExecutionIndex < LastLaunchIndex; ++ExecutionIndex)
		{
			FPCGGraphActiveTask& ActiveTask = ActiveTasks[ExecutionIndex];
			ActiveTask.Context->NumAvailableTasks = 1 + NumAdditionalThreads;
			CurrentlyUsedThreads += ActiveTask.Context->NumAvailableTasks;
		}

		// Dispatch async tasks
		TMap<int32, TFuture<bool>> Futures;
		
		for(int32 ExecutionIndex = 0; ExecutionIndex < ActiveTasks.Num() - 1; ++ExecutionIndex)
		{
			FPCGGraphActiveTask& ActiveTask = ActiveTasks[ExecutionIndex];
#if WITH_EDITOR
			if(!ActiveTask.bIsBypassed && !ActiveTask.Context->bIsPaused)
#else
			if(!ActiveTask.Context->bIsPaused)
#endif
			{
				Futures.Emplace(ExecutionIndex, Async(EAsyncExecution::ThreadPool, [&ActiveTask]()
				{
					return ActiveTask.Element->Execute(ActiveTask.Context.Get());
				}));
			}
		}

		auto PostTaskExecute = [this, &bSomeTaskEndedInCurrentLoop](int32 TaskIndex)
		{
			FPCGGraphActiveTask& ActiveTask = ActiveTasks[TaskIndex];

#if WITH_EDITOR
			if (!ActiveTask.bIsBypassed)
#endif
			{
				// Store result in cache as needed - done here because it needs to be done on the main thread
				const UPCGSettings* ActiveTaskSettings = ActiveTask.Context->GetInputSettings<UPCGSettings>();
				if (ActiveTaskSettings && ActiveTask.Element->IsCacheable(ActiveTaskSettings))
				{
					GraphCache.StoreInCache(ActiveTask.Element.Get(), ActiveTask.Context->InputData, ActiveTaskSettings, ActiveTask.Context->OutputData);
				}
			}

			CurrentlyUsedThreads -= ActiveTask.Context->NumAvailableTasks;

#if WITH_EDITOR
			// Execute debug display code as needed - done here because it needs to be done on the main thread
			// Additional note: this needs to be executed before the StoreResults since debugging might cancel further tasks
			ActiveTask.Element->DebugDisplay(ActiveTask.Context.Get());

			UPCGComponent* SourceComponent = ActiveTask.Context->SourceComponent;
			if (SourceComponent && SourceComponent->IsInspecting())
			{
				SourceComponent->StoreInspectionData(ActiveTask.Context->Node, ActiveTask.Context->OutputData);
			}
#endif

			// Store output in data map
			StoreResults(ActiveTask.NodeId, ActiveTask.Context->OutputData);

			// Book-keeping
			bSomeTaskEndedInCurrentLoop = true;

			// Remove current active task from list
			ActiveTasks.RemoveAtSwap(TaskIndex);
		};

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(FPCGGraphExecutor::Execute::ExecuteTasks);
			// Execute main thread task
			if (!ActiveTasks.IsEmpty())
			{
				FPCGGraphActiveTask& MainThreadTask = ActiveTasks.Last();
#if WITH_EDITOR
				if (MainThreadTask.bIsBypassed || (!MainThreadTask.Context->bIsPaused && MainThreadTask.Element->Execute(MainThreadTask.Context.Get())))
#else
				if (!MainThreadTask.Context->bIsPaused && MainThreadTask.Element->Execute(MainThreadTask.Context.Get()))
#endif
				{
					PostTaskExecute(ActiveTasks.Num() - 1);
				}
			}

			for (int32 ExecutionIndex = ActiveTasks.Num() - 1; ExecutionIndex >= 0; --ExecutionIndex)
			{
				bool bTaskDone = true;
				// Wait on the future if any
				if (TFuture<bool>* Future = Futures.Find(ExecutionIndex))
				{
					Future->Wait();
					bTaskDone = Future->Get();
				}

				if (bTaskDone)
				{
					PostTaskExecute(ExecutionIndex);
				}
			}
		}

		check(CurrentlyUsedThreads == 0);

		if (bSomeTaskEndedInCurrentLoop)
		{
			QueueReadyTasks();
			bAnyTaskEnded = true;
		}

		const double EndLoopTime = FPlatformTime::Seconds();
		if (static_cast<float>((EndLoopTime - StartTime) * 1000) > CVarTimePerFrame.GetValueOnAnyThread())
		{
			break;
		}
	}

	if (bAnyTaskEnded)
	{
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
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGGraphExecutor::QueueReadyTasks);
	for (int TaskIndex = Tasks.Num() - 1; TaskIndex >= 0; --TaskIndex)
	{
		bool bAllPrerequisitesMet = true;
		for(const FPCGGraphTaskInput& Input : Tasks[TaskIndex].Inputs)
		{
			bAllPrerequisitesMet &= OutputData.Contains(Input.TaskId);
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
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGGraphExecutor::BuildTaskInput);
	for (const FPCGGraphTaskInput& Input : Task.Inputs)
	{
		check(OutputData.Contains(Input.TaskId));
		const FPCGDataCollection& InputCollection = OutputData[Input.TaskId];

		TaskInput.bCancelExecution |= InputCollection.bCancelExecution;

		// Get input data at the given pin (or everything)
		const int32 TaggedDataOffset = TaskInput.TaggedData.Num();
		if (Input.InPin)
		{
			TaskInput.TaggedData.Append(InputCollection.GetInputsByPin(Input.InPin->Properties.Label));
		}
		else
		{
			TaskInput.TaggedData.Append(InputCollection.TaggedData);
		}

		if (TaskInput.TaggedData.Num() == TaggedDataOffset && InputCollection.bCancelExecutionOnEmpty)
		{
			TaskInput.bCancelExecution = true;
		}

		// Apply labelling on data; technically, we should ensure that we do this only for pass-through nodes,
		// Otherwise we could also null out the label on the input...
		if (Input.OutPin)
		{
			for (int32 TaggedDataIndex = TaggedDataOffset; TaggedDataIndex < TaskInput.TaggedData.Num(); ++TaggedDataIndex)
			{
				TaskInput.TaggedData[TaggedDataIndex].Pin = Input.OutPin->Properties.Label;
			}
		}
	}
}

void FPCGGraphExecutor::StoreResults(FPCGTaskId InTaskId, const FPCGDataCollection& InTaskOutput)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGGraphExecutor::StoreResults);

	// Store output in map
	OutputData.Add(InTaskId, InTaskOutput);

	// Root any non-rooted results, otherwise they'll get garbage-collected
	InTaskOutput.AddToRootSet(ResultsRootSet);
}

void FPCGGraphExecutor::ClearResults()
{
	ScheduleLock.Lock();
	
	NextTaskId = 0;
	OutputData.Reset();

	ResultsRootSet.Clear();

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

#if WITH_EDITOR
	if (bRunGC && !PCGHelpers::IsRuntimeOrPIE())
	{
		CollectGarbage(RF_NoFlags, true);
	}
#endif
}

void FPCGGraphExecutor::NotifyGraphChanged(UPCGGraph* InGraph)
{
	if (GraphCompiler)
	{
		GraphCompiler->NotifyGraphChanged(InGraph);
	}
}
#endif // WITH_EDITOR

bool FPCGFetchInputElement::ExecuteInternal(FPCGContext* Context) const
{
	// First: any input can be passed through to the output trivially
	Context->OutputData = Context->InputData;

	// Second: fetch the inputs provided by the component
	UPCGComponent* Component = Context->SourceComponent;
	check(Component);

	check(Context->Node);

	if (Context->Node->IsOutputPinConnected(PCGPinConstants::DefaultInputLabel))
	{
		if (UPCGData* PCGData = Component->GetPCGData())
		{
			FPCGTaggedData& TaggedData = Context->OutputData.TaggedData.Emplace_GetRef();
			TaggedData.Data = PCGData;
			TaggedData.Pin = PCGPinConstants::DefaultInputLabel;
		}
	}

	if (Context->Node->IsOutputPinConnected(PCGInputOutputConstants::DefaultInputLabel))
	{
		if (UPCGData* InputPCGData = Component->GetInputPCGData())
		{
			FPCGTaggedData& TaggedData = Context->OutputData.TaggedData.Emplace_GetRef();
			TaggedData.Data = InputPCGData;
			TaggedData.Pin = PCGInputOutputConstants::DefaultInputLabel;
		}
	}

	if (Context->Node->IsOutputPinConnected(PCGInputOutputConstants::DefaultActorLabel))
	{
		if (UPCGData* ActorPCGData = Component->GetActorPCGData())
		{
			FPCGTaggedData& TaggedData = Context->OutputData.TaggedData.Emplace_GetRef();
			TaggedData.Data = ActorPCGData;
			TaggedData.Pin = PCGInputOutputConstants::DefaultActorLabel;
		}
	}

	if (Context->Node->IsOutputPinConnected(PCGInputOutputConstants::DefaultLandscapeLabel))
	{
		if (UPCGData* LandscapePCGData = Component->GetLandscapePCGData())
		{
			FPCGTaggedData& TaggedData = Context->OutputData.TaggedData.Emplace_GetRef();
			TaggedData.Data = LandscapePCGData;
			TaggedData.Pin = PCGInputOutputConstants::DefaultLandscapeLabel;
		}
	}

	if (Context->Node->IsOutputPinConnected(PCGInputOutputConstants::DefaultLandscapeHeightLabel))
	{
		if (UPCGData* LandscapeHeightPCGData = Component->GetLandscapeHeightPCGData())
		{
			FPCGTaggedData& TaggedData = Context->OutputData.TaggedData.Emplace_GetRef();
			TaggedData.Data = LandscapeHeightPCGData;
			TaggedData.Pin = PCGInputOutputConstants::DefaultLandscapeHeightLabel;
		}
	}

	if (Context->Node->IsOutputPinConnected(PCGInputOutputConstants::DefaultOriginalActorLabel))
	{
		if (UPCGData* OriginalActorPCGData = Component->GetOriginalActorPCGData())
		{
			FPCGTaggedData& TaggedData = Context->OutputData.TaggedData.Emplace_GetRef();
			TaggedData.Data = OriginalActorPCGData;
			TaggedData.Pin = PCGInputOutputConstants::DefaultOriginalActorLabel;
		}
	}

	if (Context->Node->IsOutputPinConnected(PCGInputOutputConstants::DefaultExcludedActorsLabel))
	{
		TArray<UPCGData*> ExclusionsPCGData = Component->GetPCGExclusionData();
		for (UPCGData* ExclusionPCGData : ExclusionsPCGData)
		{
			FPCGTaggedData& TaggedData = Context->OutputData.TaggedData.Emplace_GetRef();
			TaggedData.Data = ExclusionPCGData;
			TaggedData.Pin = PCGInputOutputConstants::DefaultExcludedActorsLabel;
		}
	}

	return true;
}

FPCGGenericElement::FPCGGenericElement(TFunction<bool()> InOperation)
	: Operation(InOperation)
{
}

bool FPCGGenericElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGGenericElement::Execute);
	return Operation();
}

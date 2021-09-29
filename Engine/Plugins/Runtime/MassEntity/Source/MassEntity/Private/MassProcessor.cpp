// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassProcessor.h"
#include "MassEntitySettings.h"
#include "MassProcessorDependencySolver.h"
#include "VisualLogger/VisualLogger.h"
#include "Engine/World.h"
#include "LWCCommandBuffer.h"

DECLARE_CYCLE_STAT(TEXT("PipeProcessor Group Completed"), Pipe_GroupCompletedTask, STATGROUP_TaskGraphTasks);

#define PARALLELIZED_TRAFFIC_HACK 1

#if PARALLELIZED_TRAFFIC_HACK
namespace UE::MassTraffic
{
	int32 bParallelizeTraffic = 1;
	FAutoConsoleVariableRef CVarParallelizeTraffic(TEXT("ai.traffic.parallelize"), bParallelizeTraffic, TEXT("Whether to parallelize traffic or not"), ECVF_Cheat);
}
#endif // PARALLELIZED_TRAFFIC_HACK

#if WITH_PIPE_DEBUG
namespace UE::Mass::Debug
{
	bool bLogProcessingGraph = false;
	FAutoConsoleVariableRef CVarLogProcessingGraph(TEXT("pipe.LogProcessingGraph"), bLogProcessingGraph
		, TEXT("When enabled will log task graph tasks created while dispatching processors to other threads, along with their dependencies"), ECVF_Cheat);
}
#endif // WITH_PIPE_DEBUG

// change to && 1 to enable more detailed processing tasks logging
#if WITH_PIPE_DEBUG && 0
#define PROCESSOR_LOG(Fmt, ...) UE_LOG(LogPipe, Log, Fmt, ##__VA_ARGS__)
#else // WITH_PIPE_DEBUG
#define PROCESSOR_LOG(...) 
#endif // WITH_PIPE_DEBUG

namespace FPipeTweakables
{
	bool bParallelGroups = false;
	float PostponedTaskWaitTimeWarningLevel = 0.002f;

	FAutoConsoleVariableRef CVarsPipeProcessor[] = {
		{TEXT("pipe.ParallelGroups"), bParallelGroups, TEXT("Enables pipe processing groups distribution to all available threads (via the task graph)")},
		{TEXT("pipe.PostponedTaskWaitTimeWarningLevel"), PostponedTaskWaitTimeWarningLevel, TEXT("if waiting for postponed task\'s dependencies exceeds this number an error will be logged")},
	};
}

class FPipeProcessorTask
{
public:
	FPipeProcessorTask(UMassEntitySubsystem& InEntitySubsystem, const FLWComponentSystemExecutionContext& InExecutionContext, UPipeProcessor& InProc, bool bInManageCommandBuffer = true)
		: EntitySubsystem(&InEntitySubsystem)
		, ExecutionContext(InExecutionContext)
		, Processor(&InProc)
		, bManageCommandBuffer(bInManageCommandBuffer)
	{}

	static TStatId GetStatId()
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FPipeProcessorTask, STATGROUP_TaskGraphTasks);
	}

	static ESubsequentsMode::Type GetSubsequentsMode() { return ESubsequentsMode::TrackSubsequents; }

	static ENamedThreads::Type GetDesiredThread()
	{
		return ENamedThreads::AnyHiPriThreadHiPriTask;
	}

	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		checkf(Processor, TEXT("Expecting a valid processor to execute"));
		checkf(EntitySubsystem, TEXT("Expecting a valid entity subsystem to execute processor"));

		PROCESSOR_LOG(TEXT("+--+ Task %s started on %u"), *Processor->GetProcessorName(), FPlatformTLS::GetCurrentThreadId());

		UMassEntitySubsystem::FScopedProcessing ProcessingScope = EntitySubsystem->NewProcessingScope();

		TRACE_CPUPROFILER_EVENT_SCOPE_STR("Pipe Processor Task");
		
		if (bManageCommandBuffer)
		{
			TSharedPtr<FLWCCommandBuffer> MainSharedPtr = ExecutionContext.GetSharedDeferredCommandBuffer();
			ExecutionContext.SetDeferredCommandBuffer(MakeShareable(new FLWCCommandBuffer()));
			Processor->CallExecute(*EntitySubsystem, ExecutionContext);
			MainSharedPtr->MoveAppend(ExecutionContext.Defer());
		}
		else
		{
			Processor->CallExecute(*EntitySubsystem, ExecutionContext);
		}
		PROCESSOR_LOG(TEXT("+--+ Task %s finished"), *Processor->GetProcessorName());
	}

private:
	UMassEntitySubsystem* EntitySubsystem = nullptr;
	FLWComponentSystemExecutionContext ExecutionContext;
	UPipeProcessor* Processor = nullptr;
	/** 
	 * indicates whether this task is responsible for creation of a dedicated command buffer and transferring over the 
	 * commands after processor's execution;
	 */
	bool bManageCommandBuffer = true;
};

class FPipeProcessorsTask_GameThread : public FPipeProcessorTask
{
public:
	FPipeProcessorsTask_GameThread(UMassEntitySubsystem& InEntitySubsystem, const FLWComponentSystemExecutionContext& InExecutionContext, UPipeProcessor& InProc)
		: FPipeProcessorTask(InEntitySubsystem, InExecutionContext, InProc)
	{}

	static ENamedThreads::Type GetDesiredThread()
	{
		return ENamedThreads::GameThread;
	}
};

//----------------------------------------------------------------------//
// UPipeProcessor 
//----------------------------------------------------------------------//
UPipeProcessor::UPipeProcessor(const FObjectInitializer& ObjectInitializer)
	: ExecutionFlags((int32)(EProcessorExecutionFlags::Server | EProcessorExecutionFlags::Standalone))
{
}

void UPipeProcessor::SetShouldAutoRegisterWithGlobalList(const bool bAutoRegister)
{	
	if (ensureMsgf(HasAnyFlags(RF_ClassDefaultObject), TEXT("Setting bAutoRegisterWithProcessingPhases for non-CDOs has no effect")))
	{
		bAutoRegisterWithProcessingPhases = bAutoRegister;
#if WITH_EDITOR
		SaveConfig(CPF_Config, *GetDefaultConfigFilename());
#endif // WITH_EDITOR
	}
}

void UPipeProcessor::PostInitProperties()
{
	Super::PostInitProperties();
	if (HasAnyFlags(RF_ClassDefaultObject) == false) 
	{
		ConfigureQueries();
	}
#if CPUPROFILERTRACE_ENABLED
	StatId = GetProcessorName();
#endif
}

void UPipeProcessor::CallExecute(UMassEntitySubsystem& EntitySubsystem, FLWComponentSystemExecutionContext& Context)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*StatId);
#if WITH_PIPE_DEBUG
	Context.DebugSetExecutionDesc(FString::Printf(TEXT("%s (%s)"), *GetProcessorName(), *ToString(EntitySubsystem.GetWorld()->GetNetMode())));
#endif
	Execute(EntitySubsystem, Context);
}

FGraphEventRef UPipeProcessor::DispatchProcessorTasks(UMassEntitySubsystem& EntitySubsystem, FLWComponentSystemExecutionContext& ExecutionContext, const FGraphEventArray& Prerequisites)
{
	FGraphEventRef ReturnVal;
	if (bRequiresGameThreadExecution)
	{
		ReturnVal = TGraphTask<FPipeProcessorsTask_GameThread>::CreateTask(&Prerequisites).ConstructAndDispatchWhenReady(EntitySubsystem, ExecutionContext, *this);
	}
	else
	{
		ReturnVal = TGraphTask<FPipeProcessorTask>::CreateTask(&Prerequisites).ConstructAndDispatchWhenReady(EntitySubsystem, ExecutionContext, *this);
	}	
	return ReturnVal;
}

void UPipeProcessor::DebugOutputDescription(FOutputDevice& Ar, int32 Indent) const
{
#if WITH_PIPE_DEBUG
	Ar.Logf(TEXT("%*s%s"), Indent, TEXT(""), *GetProcessorName());
#endif // WITH_PIPE_DEBUG
}

#if WITH_EDITOR
void UPipeProcessor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		// this is here to make sure all the changes to CDOs we do via PipeSettings gets serialized to the ini file
		SaveConfig(CPF_Config, *GetDefaultConfigFilename());
	}
}
#endif // WITH_EDITOR

//----------------------------------------------------------------------//
//  UPipeCompositeProcessor
//----------------------------------------------------------------------//
UPipeCompositeProcessor::UPipeCompositeProcessor()
	: GroupName(TEXT("None"))
	, bRunInSeparateThread(false)
	, bHasOffThreadSubGroups(false)
{
	// not auto-registering composite processors since the idea of the global processors list is to indicate all 
	// the processors doing the work while composite processors are just containers. Having said that subclasses 
	// can change this behavior if need be.
	bAutoRegisterWithProcessingPhases = false;
}

void UPipeCompositeProcessor::SetChildProcessors(TArray<UPipeProcessor*>&& InProcessors)
{
	ChildPipeline.SetProcessors(MoveTemp(InProcessors));
}

void UPipeCompositeProcessor::ConfigureQueries()
{
	// nothing to do here since ConfigureQueries will get automatically called for all the processors in ChildPipeline
	// via their individual PostInitProperties call
}

FGraphEventRef UPipeCompositeProcessor::DispatchProcessorTasks(UMassEntitySubsystem& EntitySubsystem, FLWComponentSystemExecutionContext& ExecutionContext, const FGraphEventArray& InPrerequisites)
{
	FGraphEventArray Events;
	Events.Reserve(ProcessingFlatGraph.Num());
		
	for (FDependencyNode& ProcessingNode : ProcessingFlatGraph)
	{
		FGraphEventArray Prerequisites;
		for (const int32 DependencyIndex : ProcessingNode.Dependencies)
		{
			Prerequisites.Add(Events[DependencyIndex]);
		}

		if (ProcessingNode.Processor)
		{
			Events.Add(ProcessingNode.Processor->DispatchProcessorTasks(EntitySubsystem, ExecutionContext, Prerequisites));
		}
		else
		{
			Events.Add(FFunctionGraphTask::CreateAndDispatchWhenReady([=](){}
				, GET_STATID(Pipe_GroupCompletedTask), &Prerequisites, ENamedThreads::AnyHiPriThreadHiPriTask));
		}
	}


#if WITH_PIPE_DEBUG
	if (UE::Mass::Debug::bLogProcessingGraph)
	{
		for (int i = 0; i < ProcessingFlatGraph.Num(); ++i)
		{
			FDependencyNode& ProcessingNode = ProcessingFlatGraph[i];
			FString DependenciesDesc;
			for (const int32 DependencyIndex : ProcessingNode.Dependencies)
			{
				DependenciesDesc += FString::Printf(TEXT("%s, "), *ProcessingFlatGraph[DependencyIndex].Name.ToString());
			}

			if (ProcessingNode.Processor)
			{
				PROCESSOR_LOG(TEXT("Task %u %s%s%s"), Events[i]->GetTraceId(), *ProcessingNode.Processor->GetProcessorName()
					, DependenciesDesc.Len() > 0 ? TEXT(" depends on ") : TEXT(""), *DependenciesDesc);
			}
			else
			{
				PROCESSOR_LOG(TEXT("Group %u %s%s%s"), Events[i]->GetTraceId(), *ProcessingNode.Name.ToString()
					, DependenciesDesc.Len() > 0 ? TEXT(" depends on ") : TEXT(""), *DependenciesDesc);
			}
		}
	}
#endif // WITH_PIPE_DEBUG

	FGraphEventRef CompletionEvent = FFunctionGraphTask::CreateAndDispatchWhenReady([this](){}
		, GET_STATID(Pipe_GroupCompletedTask), &Events, ENamedThreads::AnyHiPriThreadHiPriTask);

	return CompletionEvent;
}

void UPipeCompositeProcessor::Execute(UMassEntitySubsystem& EntitySubsystem, FLWComponentSystemExecutionContext& Context)
{
#if PARALLELIZED_TRAFFIC_HACK
	if (FPipeTweakables::bParallelGroups == false
		&& UE::MassTraffic::bParallelizeTraffic && GetProcessingPhase() == EPipeProcessingPhase::PrePhysics)
	{
		static FName TrafficGroup(TEXT("Traffic"));
		FLWComponentSystemExecutionContext TrafficExecutionContext;
		FGraphEventRef TrafficCompletionEvent;

		for (UPipeProcessor* Proc : ChildPipeline.Processors)
		{
			check(Proc);
				if (GetProcessingPhase() == EPipeProcessingPhase::PrePhysics)
			{
				if (const UPipeCompositeProcessor* CompProcessor = Cast<UPipeCompositeProcessor>(Proc))
				{
					if (CompProcessor->GetGroupName() == TrafficGroup)
					{
						TrafficExecutionContext = Context;
						// Needs its own command buffer as it will be in it own thread, will be merged after this loop
						TrafficExecutionContext.SetDeferredCommandBuffer(MakeShareable(new FLWCCommandBuffer()));
						TrafficCompletionEvent = TGraphTask<FPipeProcessorTask>::CreateTask().ConstructAndDispatchWhenReady(EntitySubsystem, TrafficExecutionContext, *Proc, /*bManageCommandBuffer=*/false);
						continue;
					}
				}
			}
			Proc->CallExecute(EntitySubsystem, Context);
		}

		if (TrafficCompletionEvent)
		{
			// synchronize with the traffic "thread"
			TrafficCompletionEvent->Wait();
			Context.Defer().MoveAppend(TrafficExecutionContext.Defer());
		}

		return;
	}
#endif // PARALLELIZED_TRAFFIC_HACK

	if (FPipeTweakables::bParallelGroups && bHasOffThreadSubGroups)
	{
		CompletionStatus.Reset();
		CompletionStatus.AddDefaulted(ChildPipeline.Processors.Num());
		TArray<int32> PostponedProcessors;

		FLWComponentSystemExecutionContext SingleThreadContext = Context;
		SingleThreadContext.SetDeferredCommandBuffer(MakeShareable(new FLWCCommandBuffer()));

		for (int32 NodeIndex = 0; NodeIndex < ChildPipeline.Processors.Num(); ++NodeIndex)
		{
			UPipeProcessor* Proc = ChildPipeline.Processors[NodeIndex];
			check(Proc);
			UPipeCompositeProcessor* CompositeProc = Cast<UPipeCompositeProcessor>(Proc);
			if (CompositeProc == nullptr || CompositeProc->bRunInSeparateThread == false)
			{
				// check if all dependencies have been processed already
				Proc->TransientDependencyIndices.Reset();
				for (const int32 DependencyIndex : Proc->DependencyIndices)
				{
					if (CompletionStatus[DependencyIndex].IsDone() == false)
					{
						Proc->TransientDependencyIndices.Add(DependencyIndex);
					}
				}

				if (Proc->TransientDependencyIndices.Num() == 0)
				{
					PROCESSOR_LOG(TEXT("+--+ Instant Execution: %s.%s in %u")
						, CompositeProc ? TEXT("") : *Proc->GetExecutionOrder().ExecuteInGroup.ToString()
						, CompositeProc ? *CompositeProc->GetGroupName().ToString() : *Proc->GetProcessorName()
						, FPlatformTLS::GetCurrentThreadId());

					Proc->CallExecute(EntitySubsystem, SingleThreadContext);
					CompletionStatus[NodeIndex].Status = EProcessorCompletionStatus::Done;
				}
				else
				{
					PROCESSOR_LOG(TEXT("+--+ POSTPONED Execution: %s.%s in %u")
						, CompositeProc ? TEXT("") : *Proc->GetExecutionOrder().ExecuteInGroup.ToString()
						, CompositeProc ? *CompositeProc->GetGroupName().ToString() : *Proc->GetProcessorName()
						, FPlatformTLS::GetCurrentThreadId());

					// postpone
					// the graph event is needed in case we have off-thread processors depending on this one
					CompletionStatus[NodeIndex].CompletionEvent = FGraphEvent::CreateGraphEvent();
					CompletionStatus[NodeIndex].Status = EProcessorCompletionStatus::Postponed;
					PostponedProcessors.Add(NodeIndex);
				}
			}
			else
			{
				// gather prerequisites
				FString DependenciesDesc;
				FGraphEventArray Prerequisites;
				for (const int32 Index : Proc->GetPrerequisiteIndices())
				{
					if (Index != INDEX_NONE && CompletionStatus[Index].IsDone() == false)
					{
						Prerequisites.Add(CompletionStatus[Index].CompletionEvent);
						DependenciesDesc += FString::Printf(TEXT("%s, "), *ChildPipeline.Processors[Index]->GetProcessorName());
					}
				}

				PROCESSOR_LOG(TEXT("+--+ Task %s created. %s%s"), *Proc->GetProcessorName()
					, DependenciesDesc.Len() > 0 ? TEXT(" Dependencies: ") : TEXT(""), *DependenciesDesc);

				// send off to another thread
				CompletionStatus[NodeIndex].CompletionEvent = TGraphTask<FPipeProcessorTask>::CreateTask(&Prerequisites).ConstructAndDispatchWhenReady(EntitySubsystem, Context, *Proc);
				CompletionStatus[NodeIndex].Status = EProcessorCompletionStatus::Threaded;
			}
		}

		{
			TRACE_CPUPROFILER_EVENT_SCOPE_TEXT("Postponed PipeProcessors");

			for (int32 i = 0; i < PostponedProcessors.Num(); ++i)
			{
				const int32 PostponedIndex = PostponedProcessors[i];
				UPipeProcessor* Proc = ChildPipeline.Processors[PostponedIndex];
				check(Proc);
				for (int32 j = Proc->TransientDependencyIndices.Num() - 1; j >= 0; --j)
				{
					int32 DependencyIndex = Proc->TransientDependencyIndices[j];
					ensureAlways(CompletionStatus[DependencyIndex].IsDone() || CompletionStatus[DependencyIndex].Status == EProcessorCompletionStatus::Threaded);
						
					CompletionStatus[DependencyIndex].Wait();
					Proc->TransientDependencyIndices.RemoveAtSwap(j, 1, /*bAllowShrinking=*/false);
				}

				if (Proc->TransientDependencyIndices.Num() == 0)
				{
					Proc->CallExecute(EntitySubsystem, SingleThreadContext);
					CompletionStatus[PostponedIndex].Status = EProcessorCompletionStatus::Done;
					CompletionStatus[PostponedIndex].CompletionEvent->DispatchSubsequents();
					PostponedProcessors.RemoveAt(i--, 1, /*bAllowShrinking=*/false);
				}
			}
			ensureMsgf(PostponedProcessors.Num() == 0
				, TEXT("Failed to execute all processors in one intermittent sequence - this indicates an issue with depdendency ordering"));
		}

		// wait for all events to complete
		for (auto& Event : CompletionStatus)
		{
			if (Event.CompletionEvent.IsValid())
			{
				Event.CompletionEvent->Wait();
			}
		}
		Context.Defer().MoveAppend(SingleThreadContext.Defer());
	}
	else
	{
		for (UPipeProcessor* Proc : ChildPipeline.Processors)
		{
			check(Proc);
			Proc->CallExecute(EntitySubsystem, Context);
		}
	}
}

void UPipeCompositeProcessor::Initialize(UObject& Owner)
{
	// remove all nulls
	ChildPipeline.Processors.RemoveAll([](const UPipeProcessor* Proc) { return Proc == nullptr; });

	// from this point on we don't expect to have nulls in ChildPipeline.Processors
	for (UPipeProcessor* Proc : ChildPipeline.Processors)
	{
		REDIRECT_OBJECT_TO_VLOG(Proc, this);
		Proc->Initialize(Owner);
	}
}

void UPipeCompositeProcessor::CopyAndSort(const FPipeProcessingPhaseConfig& PhaseConfig, const FString& DependencyGraphFileName)
{
	check(GetOuter());
	
	// create processors via a temporary pipeline. This ensures consistency with other places we use FRuntimePipelines
	FRuntimePipeline TmpPipeline;
	TmpPipeline.CreateFromArray(PhaseConfig.ProcessorCDOs, *GetOuter());

	// figure out dependencies
	FProcessorDependencySolver Solver(TmpPipeline.Processors, GroupName, DependencyGraphFileName);
	TArray<FProcessorDependencySolver::FOrderInfo> SortedProcessorsAndGroups;
	Solver.ResolveDependencies(SortedProcessorsAndGroups, PhaseConfig.OffGameThreadGroupNames);

	Populate(SortedProcessorsAndGroups);

	// this part is creating an ordered, flat list of processors that can be executed in sequence
	// with subsequent task only depending on the elements prior on the list
	TMap<FName, int32> NameToDependencyIndex;
	NameToDependencyIndex.Reserve(SortedProcessorsAndGroups.Num());
	for (FProcessorDependencySolver::FOrderInfo& Element : SortedProcessorsAndGroups)
	{
		NameToDependencyIndex.Add(Element.Name, NameToDependencyIndex.Num());

		FDependencyNode& Node = ProcessingFlatGraph.Add_GetRef({ Element.Name, Element.Processor });
		Node.Dependencies.Reserve(Element.Dependencies.Num());
		for (FName DependencyName : Element.Dependencies)
		{
			Node.Dependencies.Add(NameToDependencyIndex.FindChecked(DependencyName));
		}
	}
}

int32 UPipeCompositeProcessor::Populate(TArray<FProcessorDependencySolver::FOrderInfo>& ProcessorsAndGroups, const int32 StartIndex)
{
	ChildPipeline.Processors.Reset();

	// if processor -> Add
	// if not processor 
	//    if "start" -> Create group and pass the InOutProcessorsAndGroups in
	//    else return (pop out)
	int32 Index = StartIndex;
	TMap<FName, int32> NameToIndexMap;
	bool bOffThreadGroupsFound = false;
	const FPipeProcessingPhaseConfig& PhaseConfig = GET_PIPE_CONFIG_VALUE(GetProcessingPhaseConfig(ProcessingPhase));

	while (ProcessorsAndGroups.IsValidIndex(Index))
	{
		FProcessorDependencySolver::FOrderInfo& Element = ProcessorsAndGroups[Index];
		ensure(Element.NodeType != EDependencyNodeType::Invalid);
		
		if (Element.NodeType == EDependencyNodeType::Processor)
		{
			check(Element.Processor);
			check(Element.Processor->GetExecutionOrder().ExecuteInGroup == GroupName || Element.Processor->GetExecutionOrder().ExecuteInGroup.IsNone());

			Element.Processor->DependencyIndices.Reset();
			for (const FName DependencyName : Element.Dependencies)
			{
				Element.Processor->DependencyIndices.Add(NameToIndexMap.FindChecked(DependencyName));
			}

			NameToIndexMap.Add(Element.Name, ChildPipeline.Processors.Num());
			ChildPipeline.AppendProcessor(*Element.Processor);
			++Index;
		}
		else if (Element.NodeType == EDependencyNodeType::GroupStart)
		{
			UPipeCompositeProcessor* GroupProcessor = NewObject<UPipeCompositeProcessor>(GetOuter());
			GroupProcessor->SetGroupName(Element.Name);
			GroupProcessor->SetProcessingPhase(ProcessingPhase);
			if (PhaseConfig.OffGameThreadGroupNames.Find(Element.Name) != INDEX_NONE)
			{
				GroupProcessor->bRunInSeparateThread = true;
			}

			GroupProcessor->DependencyIndices.Reset();
			for (const FName DependencyName : Element.Dependencies)
			{
				const int32 DependencyIndex = NameToIndexMap.FindChecked(DependencyName);
				GroupProcessor->DependencyIndices.Add(DependencyIndex);
			}

			NameToIndexMap.Add(Element.Name, ChildPipeline.Processors.Num());
			ChildPipeline.AppendProcessor(*GroupProcessor);

			Index = GroupProcessor->Populate(ProcessorsAndGroups, Index + 1) + 1;
			bOffThreadGroupsFound = (bOffThreadGroupsFound || GroupProcessor->bRunInSeparateThread);
		}
		else 
		{
			check(Element.NodeType == EDependencyNodeType::GroupEnd);
			check(Element.Name == GroupName);
			// done
			break;
		}
	}

	bHasOffThreadSubGroups = bOffThreadGroupsFound;

	return Index;
}

void UPipeCompositeProcessor::DebugOutputDescription(FOutputDevice& Ar, int32 Indent) const
{
#if WITH_PIPE_DEBUG
	if (ChildPipeline.Processors.Num() == 0)
	{
		Ar.Logf(TEXT("%*sGroup %s: []"), Indent, TEXT(""), *GroupName.ToString());
	}
	else
	{
		Ar.Logf(TEXT("%*sGroup %s:"), Indent, TEXT(""), *GroupName.ToString());
		for (UPipeProcessor* Proc : ChildPipeline.Processors)
		{
			check(Proc);
			Ar.Logf(TEXT("\n"));
			Proc->DebugOutputDescription(Ar, Indent + 3);
		}
	}
#endif // WITH_PIPE_DEBUG
}

void UPipeCompositeProcessor::SetProcessingPhase(EPipeProcessingPhase Phase)
{
	Super::SetProcessingPhase(Phase);
	for (UPipeProcessor* Proc : ChildPipeline.Processors)
	{
		Proc->SetProcessingPhase(Phase);
	}
}

void UPipeCompositeProcessor::SetGroupName(FName NewName)
{
	GroupName = NewName;
#if CPUPROFILERTRACE_ENABLED
	StatId = GroupName.ToString();
#endif
}

void UPipeCompositeProcessor::AddGroupedProcessor(FName RequestedGroupName, UPipeProcessor& Processor)
{
	if (RequestedGroupName.IsNone() || RequestedGroupName == GroupName)
	{
		ChildPipeline.AppendProcessor(Processor);
	}
	else
	{
		FString RemainingGroupName;
		UPipeCompositeProcessor* GroupProcessor = FindOrAddGroupProcessor(RequestedGroupName, &RemainingGroupName);
		check(GroupProcessor);
		GroupProcessor->AddGroupedProcessor(FName(*RemainingGroupName), Processor);
	}
}

UPipeCompositeProcessor* UPipeCompositeProcessor::FindOrAddGroupProcessor(FName RequestedGroupName, FString* OutRemainingGroupName)
{
	UPipeCompositeProcessor* GroupProcessor = nullptr;
	const FString NameAsString = RequestedGroupName.ToString();
	FString TopGroupName;
	if (NameAsString.Split(TEXT("."), &TopGroupName, OutRemainingGroupName))
	{
		RequestedGroupName = FName(*TopGroupName);
	}
	GroupProcessor = ChildPipeline.FindTopLevelGroupByName(RequestedGroupName);

	if (GroupProcessor == nullptr)
	{
		check(GetOuter());
		GroupProcessor = NewObject<UPipeCompositeProcessor>(GetOuter());
		GroupProcessor->SetGroupName(RequestedGroupName);
		ChildPipeline.AppendProcessor(*GroupProcessor);
	}

	return GroupProcessor;
}

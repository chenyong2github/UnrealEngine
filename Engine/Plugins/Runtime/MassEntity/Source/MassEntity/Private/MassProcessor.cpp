// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassProcessor.h"
#include "MassEntitySettings.h"
#include "MassProcessorDependencySolver.h"
#include "VisualLogger/VisualLogger.h"
#include "Engine/World.h"
#include "MassCommandBuffer.h"

DECLARE_CYCLE_STAT(TEXT("MassProcessor Group Completed"), Mass_GroupCompletedTask, STATGROUP_TaskGraphTasks);

#define PARALLELIZED_TRAFFIC_HACK !MASS_DO_PARALLEL

#if PARALLELIZED_TRAFFIC_HACK
namespace UE::MassTraffic
{
	int32 bParallelizeTraffic = 1;
	FAutoConsoleVariableRef CVarParallelizeTraffic(TEXT("ai.traffic.parallelize"), bParallelizeTraffic, TEXT("Whether to parallelize traffic or not"), ECVF_Cheat);
}
#endif // PARALLELIZED_TRAFFIC_HACK

#if WITH_MASSENTITY_DEBUG
namespace UE::Mass::Debug
{
	bool bLogProcessingGraph = false;
	FAutoConsoleVariableRef CVarLogProcessingGraph(TEXT("mass.LogProcessingGraph"), bLogProcessingGraph
		, TEXT("When enabled will log task graph tasks created while dispatching processors to other threads, along with their dependencies"), ECVF_Cheat);
}
#endif // WITH_MASSENTITY_DEBUG

// change to && 1 to enable more detailed processing tasks logging
#if WITH_MASSENTITY_DEBUG && 0
#define PROCESSOR_LOG(Fmt, ...) UE_LOG(LogMass, Verbose, Fmt, ##__VA_ARGS__)
#else // WITH_MASSENTITY_DEBUG
#define PROCESSOR_LOG(...) 
#endif // WITH_MASSENTITY_DEBUG

class FMassProcessorTask
{
public:
	FMassProcessorTask(UMassEntitySubsystem& InEntitySubsystem, const FMassExecutionContext& InExecutionContext, UMassProcessor& InProc, bool bInManageCommandBuffer = true)
		: EntitySubsystem(&InEntitySubsystem)
		, ExecutionContext(InExecutionContext)
		, Processor(&InProc)
		, bManageCommandBuffer(bInManageCommandBuffer)
	{}

	static TStatId GetStatId()
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FMassProcessorTask, STATGROUP_TaskGraphTasks);
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

		TRACE_CPUPROFILER_EVENT_SCOPE_STR("Mass Processor Task");
		
		if (bManageCommandBuffer)
		{
			TSharedPtr<FMassCommandBuffer> MainSharedPtr = ExecutionContext.GetSharedDeferredCommandBuffer();
			ExecutionContext.SetDeferredCommandBuffer(MakeShareable(new FMassCommandBuffer()));
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
	FMassExecutionContext ExecutionContext;
	UMassProcessor* Processor = nullptr;
	/** 
	 * indicates whether this task is responsible for creation of a dedicated command buffer and transferring over the 
	 * commands after processor's execution;
	 */
	bool bManageCommandBuffer = true;
};

class FMassProcessorsTask_GameThread : public FMassProcessorTask
{
public:
	FMassProcessorsTask_GameThread(UMassEntitySubsystem& InEntitySubsystem, const FMassExecutionContext& InExecutionContext, UMassProcessor& InProc)
		: FMassProcessorTask(InEntitySubsystem, InExecutionContext, InProc)
	{}

	static ENamedThreads::Type GetDesiredThread()
	{
		return ENamedThreads::GameThread;
	}
};

//----------------------------------------------------------------------//
// UMassProcessor 
//----------------------------------------------------------------------//
UMassProcessor::UMassProcessor(const FObjectInitializer& ObjectInitializer)
	: ExecutionFlags((int32)(EProcessorExecutionFlags::Server | EProcessorExecutionFlags::Standalone))
{
}

void UMassProcessor::SetShouldAutoRegisterWithGlobalList(const bool bAutoRegister)
{	
	if (ensureMsgf(HasAnyFlags(RF_ClassDefaultObject), TEXT("Setting bAutoRegisterWithProcessingPhases for non-CDOs has no effect")))
	{
		bAutoRegisterWithProcessingPhases = bAutoRegister;
#if WITH_EDITOR
		SaveConfig(CPF_Config, *GetDefaultConfigFilename());
#endif // WITH_EDITOR
	}
}

void UMassProcessor::PostInitProperties()
{
	Super::PostInitProperties();
	if (HasAnyFlags(RF_ClassDefaultObject) == false) 
	{
		ConfigureQueries();

		bool bNeedsGameThread = false;
		for (FMassEntityQuery* QueryPtr : OwnedQueries)
		{
			CA_ASSUME(QueryPtr);
			bNeedsGameThread = (bNeedsGameThread || QueryPtr->DoesRequireGameThreadExecution());
		}
		UE_CLOG(bRequiresGameThreadExecution != bNeedsGameThread, LogMass, Warning, TEXT("%s is marked bRequiresGameThreadExecution = %s, while the registered quries' requirement indicate the opposite")
			, *GetProcessorName(), bRequiresGameThreadExecution ? TEXT("TRUE") : TEXT("FALSE"));

		// better safe than sorry - if queries indicate the game thread execution is required then we marked the whole processor as such
		bRequiresGameThreadExecution = bRequiresGameThreadExecution || bNeedsGameThread;
	}
#if CPUPROFILERTRACE_ENABLED
	StatId = GetProcessorName();
#endif
}

void UMassProcessor::CallExecute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*StatId);
#if WITH_MASSENTITY_DEBUG
	Context.DebugSetExecutionDesc(FString::Printf(TEXT("%s (%s)"), *GetProcessorName(), *ToString(EntitySubsystem.GetWorld()->GetNetMode())));
#endif
	Execute(EntitySubsystem, Context);
}

void UMassProcessor::ExportRequirements(FMassExecutionRequirements& OutRequirements) const
{
	for (FMassEntityQuery* Query : OwnedQueries)
	{
		CA_ASSUME(Query);
		Query->ExportRequirements(OutRequirements);
	}
}

void UMassProcessor::RegisterQuery(FMassEntityQuery& Query)
{
	const uintptr_t ThisStart = (uintptr_t)this;
	const uintptr_t ThisEnd = ThisStart + GetClass()->GetStructureSize();
	const uintptr_t QueryStart = (uintptr_t)&Query;
	const uintptr_t QueryEnd = QueryStart + sizeof(FMassEntityQuery);

	if (QueryStart >= ThisStart && QueryEnd <= ThisEnd)
	{
		OwnedQueries.AddUnique(&Query);
	}
	else
	{
		constexpr TCHAR MessageFormat[] = TEXT("Registering entity query for %s while the query is not given processor's member variable. Skipping.");
		checkf(false, MessageFormat, *GetProcessorName());
		UE_LOG(LogMass, Error, MessageFormat, *GetProcessorName());
	}
}

FGraphEventRef UMassProcessor::DispatchProcessorTasks(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& ExecutionContext, const FGraphEventArray& Prerequisites)
{
	FGraphEventRef ReturnVal;
	if (bRequiresGameThreadExecution)
	{
		ReturnVal = TGraphTask<FMassProcessorsTask_GameThread>::CreateTask(&Prerequisites).ConstructAndDispatchWhenReady(EntitySubsystem, ExecutionContext, *this);
	}
	else
	{
		ReturnVal = TGraphTask<FMassProcessorTask>::CreateTask(&Prerequisites).ConstructAndDispatchWhenReady(EntitySubsystem, ExecutionContext, *this);
	}	
	return ReturnVal;
}

void UMassProcessor::DebugOutputDescription(FOutputDevice& Ar, int32 Indent) const
{
#if WITH_MASSENTITY_DEBUG
	Ar.Logf(TEXT("%*s%s"), Indent, TEXT(""), *GetProcessorName());
#endif // WITH_MASSENTITY_DEBUG
}

#if WITH_EDITOR
void UMassProcessor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		// this is here to make sure all the changes to CDOs we do via MassSettings gets serialized to the ini file
		SaveConfig(CPF_Config, *GetDefaultConfigFilename());
	}
}
#endif // WITH_EDITOR

//----------------------------------------------------------------------//
//  UMassCompositeProcessor
//----------------------------------------------------------------------//
UMassCompositeProcessor::UMassCompositeProcessor()
	: GroupName(TEXT("None"))
	, bRunInSeparateThread(false)
	, bHasOffThreadSubGroups(false)
{
	// not auto-registering composite processors since the idea of the global processors list is to indicate all 
	// the processors doing the work while composite processors are just containers. Having said that subclasses 
	// can change this behavior if need be.
	bAutoRegisterWithProcessingPhases = false;
}

void UMassCompositeProcessor::SetChildProcessors(TArray<UMassProcessor*>&& InProcessors)
{
	ChildPipeline.SetProcessors(MoveTemp(InProcessors));
}

void UMassCompositeProcessor::ConfigureQueries()
{
	// nothing to do here since ConfigureQueries will get automatically called for all the processors in ChildPipeline
	// via their individual PostInitProperties call
}

FGraphEventRef UMassCompositeProcessor::DispatchProcessorTasks(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& ExecutionContext, const FGraphEventArray& InPrerequisites)
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
				, GET_STATID(Mass_GroupCompletedTask), &Prerequisites, ENamedThreads::AnyHiPriThreadHiPriTask));
		}
	}


#if WITH_MASSENTITY_DEBUG
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
#endif // WITH_MASSENTITY_DEBUG

	FGraphEventRef CompletionEvent = FFunctionGraphTask::CreateAndDispatchWhenReady([this](){}
		, GET_STATID(Mass_GroupCompletedTask), &Events, ENamedThreads::AnyHiPriThreadHiPriTask);

	return CompletionEvent;
}

void UMassCompositeProcessor::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
#if PARALLELIZED_TRAFFIC_HACK
	if (UE::MassTraffic::bParallelizeTraffic && GetProcessingPhase() == EMassProcessingPhase::PrePhysics)
	{
		static FName TrafficGroup(TEXT("Traffic"));
		FMassExecutionContext TrafficExecutionContext;
		FGraphEventRef TrafficCompletionEvent;

		for (UMassProcessor* Proc : ChildPipeline.Processors)
		{
			check(Proc);
				if (GetProcessingPhase() == EMassProcessingPhase::PrePhysics)
			{
				if (const UMassCompositeProcessor* CompProcessor = Cast<UMassCompositeProcessor>(Proc))
				{
					if (CompProcessor->GetGroupName() == TrafficGroup)
					{
						TrafficExecutionContext = Context;
						// Needs its own command buffer as it will be in it own thread, will be merged after this loop
						TrafficExecutionContext.SetDeferredCommandBuffer(MakeShareable(new FMassCommandBuffer()));
						TrafficCompletionEvent = TGraphTask<FMassProcessorTask>::CreateTask().ConstructAndDispatchWhenReady(EntitySubsystem, TrafficExecutionContext, *Proc, /*bManageCommandBuffer=*/false);
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
	else
#endif // PARALLELIZED_TRAFFIC_HACK
	{
		for (UMassProcessor* Proc : ChildPipeline.Processors)
		{
			check(Proc);
			Proc->CallExecute(EntitySubsystem, Context);
		}
	}
}

void UMassCompositeProcessor::Initialize(UObject& Owner)
{
	// remove all nulls
	ChildPipeline.Processors.RemoveAll([](const UMassProcessor* Proc) { return Proc == nullptr; });

	// from this point on we don't expect to have nulls in ChildPipeline.Processors
	for (UMassProcessor* Proc : ChildPipeline.Processors)
	{
		REDIRECT_OBJECT_TO_VLOG(Proc, this);
		Proc->Initialize(Owner);
	}
}

void UMassCompositeProcessor::CopyAndSort(const FMassProcessingPhaseConfig& PhaseConfig, const FString& DependencyGraphFileName)
{
	check(GetOuter());
	
	// create processors via a temporary pipeline. This ensures consistency with other places we use FRuntimePipelines
	FMassRuntimePipeline TmpPipeline;
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
	TArray<int32> SuperGroupDependency;
	for (FProcessorDependencySolver::FOrderInfo& Element : SortedProcessorsAndGroups)
	{
		NameToDependencyIndex.Add(Element.Name, ProcessingFlatGraph.Num());

		FDependencyNode& Node = ProcessingFlatGraph.Add_GetRef({ Element.Name, Element.Processor });
		Node.Dependencies.Reserve(Element.Dependencies.Num());
		for (FName DependencyName : Element.Dependencies)
		{
			Node.Dependencies.Add(NameToDependencyIndex.FindChecked(DependencyName));
		}
		switch (Element.NodeType)
		{
		case EDependencyNodeType::GroupStart:
			Node.Name = FName(FString::Printf(TEXT("%s_Start"), *Node.Name.ToString()));
			SuperGroupDependency.Add(ProcessingFlatGraph.Num() - 1);
			break;
		case EDependencyNodeType::GroupEnd:
			Node.Name = FName(FString::Printf(TEXT("%s_End"), *Node.Name.ToString()));
			SuperGroupDependency.Pop();
			break;
		default:
			// this bit makes all processors withing a group depend on the group starting
			if (SuperGroupDependency.Num())
			{
				Node.Dependencies.Add(SuperGroupDependency.Last());
			}
			break;
		}
	}

#if WITH_MASSENTITY_DEBUG
	UE_LOG(LogMass, Log, TEXT("%s flat processing graph:"), *GroupName.ToString());

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
			UE_LOG(LogMass, Log, TEXT("Task %s%s%s"), *ProcessingNode.Processor->GetProcessorName()
				, DependenciesDesc.Len() > 0 ? TEXT(" depends on ") : TEXT(""), *DependenciesDesc);
		}
		else
		{
			UE_LOG(LogMass, Log, TEXT("Group %s%s%s"), *ProcessingNode.Name.ToString()
				, DependenciesDesc.Len() > 0 ? TEXT(" depends on ") : TEXT(""), *DependenciesDesc);
		}
	}
#endif // WITH_MASSENTITY_DEBUG
}

int32 UMassCompositeProcessor::Populate(TArray<FProcessorDependencySolver::FOrderInfo>& ProcessorsAndGroups, const int32 StartIndex)
{
	ChildPipeline.Processors.Reset();

	// if processor -> Add
	// if not processor 
	//    if "start" -> Create group and pass the InOutProcessorsAndGroups in
	//    else return (pop out)
	int32 Index = StartIndex;
	TMap<FName, int32> NameToIndexMap;
	bool bOffThreadGroupsFound = false;
	const FMassProcessingPhaseConfig& PhaseConfig = GET_MASS_CONFIG_VALUE(GetProcessingPhaseConfig(ProcessingPhase));

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
			UMassCompositeProcessor* GroupProcessor = NewObject<UMassCompositeProcessor>(GetOuter());
			GroupProcessor->SetGroupName(Element.Name);
			GroupProcessor->SetProcessingPhase(ProcessingPhase);
			// if there are no groups explicitly declared as "off game thread" we let everything go, subject to other 
			// limitations (like some subsystems requiring being run on game thread via TMassExternalSubsystemTraits
			GroupProcessor->bRunInSeparateThread = (PhaseConfig.OffGameThreadGroupNames.IsEmpty() == true)
				|| (PhaseConfig.OffGameThreadGroupNames.Find(Element.Name) != INDEX_NONE);

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

void UMassCompositeProcessor::DebugOutputDescription(FOutputDevice& Ar, int32 Indent) const
{
#if WITH_MASSENTITY_DEBUG
	if (ChildPipeline.Processors.Num() == 0)
	{
		Ar.Logf(TEXT("%*sGroup %s: []"), Indent, TEXT(""), *GroupName.ToString());
	}
	else
	{
		Ar.Logf(TEXT("%*sGroup %s:"), Indent, TEXT(""), *GroupName.ToString());
		for (UMassProcessor* Proc : ChildPipeline.Processors)
		{
			check(Proc);
			Ar.Logf(TEXT("\n"));
			Proc->DebugOutputDescription(Ar, Indent + 3);
		}
	}
#endif // WITH_MASSENTITY_DEBUG
}

void UMassCompositeProcessor::SetProcessingPhase(EMassProcessingPhase Phase)
{
	Super::SetProcessingPhase(Phase);
	for (UMassProcessor* Proc : ChildPipeline.Processors)
	{
		Proc->SetProcessingPhase(Phase);
	}
}

void UMassCompositeProcessor::SetGroupName(FName NewName)
{
	GroupName = NewName;
#if CPUPROFILERTRACE_ENABLED
	StatId = GroupName.ToString();
#endif
}

void UMassCompositeProcessor::AddGroupedProcessor(FName RequestedGroupName, UMassProcessor& Processor)
{
	if (RequestedGroupName.IsNone() || RequestedGroupName == GroupName)
	{
		ChildPipeline.AppendProcessor(Processor);
	}
	else
	{
		FString RemainingGroupName;
		UMassCompositeProcessor* GroupProcessor = FindOrAddGroupProcessor(RequestedGroupName, &RemainingGroupName);
		check(GroupProcessor);
		GroupProcessor->AddGroupedProcessor(FName(*RemainingGroupName), Processor);
	}
}

UMassCompositeProcessor* UMassCompositeProcessor::FindOrAddGroupProcessor(FName RequestedGroupName, FString* OutRemainingGroupName)
{
	UMassCompositeProcessor* GroupProcessor = nullptr;
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
		GroupProcessor = NewObject<UMassCompositeProcessor>(GetOuter());
		GroupProcessor->SetGroupName(RequestedGroupName);
		ChildPipeline.AppendProcessor(*GroupProcessor);
	}

	return GroupProcessor;
}

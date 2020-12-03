// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraSystemSimulation.h"
#include "NiagaraModule.h"
#include "Modules/ModuleManager.h"
#include "NiagaraTypes.h"
#include "NiagaraEvents.h"
#include "NiagaraSettings.h"
#include "NiagaraParameterCollection.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraConstants.h"
#include "NiagaraStats.h"
#include "Async/ParallelFor.h"
#include "NiagaraComponent.h"
#include "NiagaraWorldManager.h"
#include "NiagaraEmitterInstanceBatcher.h"
#include "NiagaraCrashReporterHandler.h"

// Niagara simulations async will block the tick task from completion until all async work is finished
// If simulations are allowed to tick async we will create a FNiagaraSystemSimulationTickTask task to run on any thread
// If instances are allowed to tick async we will create a FNiagaraSystemInstanceAsyncTask in batches to run on any thread
// If any async is enabled we create a FNiagaraSystemInstanceFinalizeTask for each batch that will not run until FNiagaraSystemSimulationTickTask is complete (Due to contention with SystemInstances) and will run on the GameThread
// If any async is enabled we create a FNiagaraSystemSimulationWaitAllFinalizeTask task to wait for all FNiagaraSystemInstanceFinalizeTask's to complete before allowing the tick group to advance

//High level stats for system sim tick.
DECLARE_CYCLE_STAT(TEXT("System Simulaton Tick [GT]"), STAT_NiagaraSystemSim_TickGT, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("System Simulaton Tick [CNC]"), STAT_NiagaraSystemSim_TickCNC, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("System Simulaton SpawnNew [GT]"), STAT_NiagaraSystemSim_SpawnNewGT, STATGROUP_Niagara);
//Some more detailed stats for system sim tick
DECLARE_CYCLE_STAT(TEXT("System Prepare For Simulate [CNC]"), STAT_NiagaraSystemSim_PrepareForSimulateCNC, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("System Sim Update [CNC]"), STAT_NiagaraSystemSim_UpdateCNC, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("System Sim Spawn [CNC]"), STAT_NiagaraSystemSim_SpawnCNC, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("System Sim Transfer Results [CNC]"), STAT_NiagaraSystemSim_TransferResultsCNC, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("System Sim Init [GT]"), STAT_NiagaraSystemSim_Init, STATGROUP_Niagara);

DECLARE_CYCLE_STAT(TEXT("System Sim Init (DataSets) [GT]"), STAT_NiagaraSystemSim_Init_DataSets, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("System Sim Init (ExecContexts) [GT]"), STAT_NiagaraSystemSim_Init_ExecContexts, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("System Sim Init (BindParams) [GT]"), STAT_NiagaraSystemSim_Init_BindParams, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("System Sim Init (DatasetAccessors) [GT]"), STAT_NiagaraSystemSim_Init_DatasetAccessors, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("System Sim Init (DirectBindings) [GT]"), STAT_NiagaraSystemSim_Init_DirectBindings, STATGROUP_Niagara);

DECLARE_CYCLE_STAT(TEXT("ForcedWaitForAsync"), STAT_NiagaraSystemSim_ForceWaitForAsync, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("ForcedWait Fake Stall"), STAT_NiagaraSystemSim_ForceWaitFakeStall, STATGROUP_Niagara);


static int32 GbDumpSystemData = 0;
static FAutoConsoleVariableRef CVarNiagaraDumpSystemData(
	TEXT("fx.DumpSystemData"),
	GbDumpSystemData,
	TEXT("If > 0, results of system simulations will be dumped to the log. \n"),
	ECVF_Default
);

static int32 GbSystemUpdateOnSpawn = 1;
static FAutoConsoleVariableRef CVarSystemUpdateOnSpawn(
	TEXT("fx.SystemUpdateOnSpawn"),
	GbSystemUpdateOnSpawn,
	TEXT("If > 0, system simulations are given a small update after spawn. \n"),
	ECVF_Default
);

static int32 GbParallelSystemSimTick = 1;
static FAutoConsoleVariableRef CVarParallelSystemSimTick(
	TEXT("fx.ParallelSystemSimTick"),
	GbParallelSystemSimTick,
	TEXT("If > 0, system post tick is parallelized. \n"),
	ECVF_Default
);

static int32 GbParallelSystemInstanceTick = 1;
static FAutoConsoleVariableRef CVarParallelSystemInstanceTick(
	TEXT("fx.ParallelSystemInstanceTick"),
	GbParallelSystemInstanceTick,
	TEXT("If > 0, system post tick is parallelized. \n"),
	ECVF_Default
);

static int32 GbParallelSystemInstanceTickBatchSize = NiagaraSystemTickBatchSize;
static FAutoConsoleVariableRef CVarParallelSystemInstanceTickBatchSize(
	TEXT("fx.ParallelSystemInstanceTickBatchSize"),
	GbParallelSystemInstanceTickBatchSize,
	TEXT("The number of system instances to process per async task. \n"),
	ECVF_Default
);

static int32 GbSystemSimTransferParamsParallelThreshold = 64;
static FAutoConsoleVariableRef CVarSystemSimTransferParamsParallelThreshold(
	TEXT("fx.SystemSimTransferParamsParallelThreshold"),
	GbSystemSimTransferParamsParallelThreshold,
	TEXT("The number of system instances required for the transfer parameters portion of the system tick to go wide. \n"),
	ECVF_Default
);

static int32 GNiagaraConcurrentGPUTickInit = 1;
static FAutoConsoleVariableRef CVarNiagaraConcurrentGPUTickInit(
	TEXT("fx.Niagara.ConcurrentGPUTickInit"),
	GNiagaraConcurrentGPUTickInit,
	TEXT("The if non zero we allow GPU Ticks to be initialized in the System's concurrent tick rather than on the game thread."),
	ECVF_Default
);

static int32 GNiagaraBatchGPUTickSubmit = 1;
static FAutoConsoleVariableRef CVarNiagaraBatchGPUTickSubmit(
	TEXT("fx.Niagara.BatchGPUTickSubmit"),
	GNiagaraBatchGPUTickSubmit,
	TEXT("The if non zero we allow GPU Ticks to be submitted to the Render Thread in batches."),
	ECVF_Default
);

static float GNiagaraSkipTickDeltaSeconds = 0.0f;
static FAutoConsoleVariableRef CVarNiagaraSkipTickDeltaSeconds(
	TEXT("fx.Niagara.SkipTickDeltaSeconds"),
	GNiagaraSkipTickDeltaSeconds,
	TEXT("When none zero we skip all ticks with a delta seconds less than equal to this number."),
	ECVF_Default
);

//////////////////////////////////////////////////////////////////////////
// Task priorities for simulation tasks

static FAutoConsoleTaskPriority GNiagaraTaskPriorities[] =
{
	//																														Thread Priority (w HiPri Thread)			Task Priority (w HiPri Thread)				Task Priority
	FAutoConsoleTaskPriority(TEXT("fx.Niagara.TaskPriorities.High"),		TEXT("Task Prority When Set to High"),			ENamedThreads::HighThreadPriority,			ENamedThreads::HighTaskPriority,			ENamedThreads::HighTaskPriority),
	FAutoConsoleTaskPriority(TEXT("fx.Niagara.TaskPriorities.Normal"),		TEXT("Task Prority When Set to Normal"),		ENamedThreads::HighThreadPriority,			ENamedThreads::NormalTaskPriority,			ENamedThreads::NormalTaskPriority),
	FAutoConsoleTaskPriority(TEXT("fx.Niagara.TaskPriorities.Low"),			TEXT("Task Prority When Set to Low"),			ENamedThreads::NormalThreadPriority,		ENamedThreads::HighTaskPriority,			ENamedThreads::NormalTaskPriority),
	FAutoConsoleTaskPriority(TEXT("fx.Niagara.TaskPriorities.Background"),	TEXT("Task Prority When Set to Background"),	ENamedThreads::NormalThreadPriority,		ENamedThreads::BackgroundThreadPriority,	ENamedThreads::BackgroundThreadPriority),
};

static int32 GNiagaraSystemSimulationSpawnPendingTaskPri = 1;
static FAutoConsoleVariableRef CVarNiagaraSystemSimulationSpawnPendingTaskPri(
	TEXT("fx.Niagara.TaskPriority.SystemSimulationSpawnPendingTask"),
	GNiagaraSystemSimulationSpawnPendingTaskPri,
	TEXT("Task priority to use for Niagara System Simulation Spawning Pending Task"),
	ECVF_Default
);

static int32 GNiagaraSystemSimulationTaskPri = 1;
static FAutoConsoleVariableRef CVarNiagaraSystemSimulationTaskPri(
	TEXT("fx.Niagara.TaskPriority.SystemSimulationTask"),
	GNiagaraSystemSimulationTaskPri,
	TEXT("Task priority to use for Niagara System Simulation Task"),
	ECVF_Default
);

static int32 GNiagaraSystemInstanceTaskPri = 1;
static FAutoConsoleVariableRef CVarNiagaraSystemInstanceTaskPri(
	TEXT("fx.Niagara.TaskPriority.SystemInstanceTask"),
	GNiagaraSystemInstanceTaskPri,
	TEXT("Task priority to use for Niagara System Instance Task"),
	ECVF_Default
);

static int32 GNiagaraSystemSimulationWaitAllTaskPri = 1;
static FAutoConsoleVariableRef CVarNiagaraSystemSimulationWaitAllTaskPri(
	TEXT("fx.Niagara.TaskPriority.SystemSimulationWaitAll"),
	GNiagaraSystemSimulationWaitAllTaskPri,
	TEXT("Task priority to use for Niagara System Simulation Wait All Task"),
	ECVF_Default
);

ENamedThreads::Type GetNiagaraTaskPriority(int32 Priority)
{
#if WITH_PARTICLE_PERF_STATS
	// If we are profiling particle performance make sure we don't get context switched due to lower priorty as that will confuse the results
	static const IConsoleVariable* EffectsQualityCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("fx.ParticlePerfStats.Enabled"));
	if (EffectsQualityCVar && (EffectsQualityCVar->GetInt() != 0))
	{
		return GNiagaraTaskPriorities[1].Get();
	}
#endif
	Priority = FMath::Clamp(Priority, 0, (int32)UE_ARRAY_COUNT(GNiagaraTaskPriorities) - 1);
	return GNiagaraTaskPriorities[Priority].Get();
}

static FAutoConsoleCommand CCmdNiagaraDumpPriorities(
	TEXT("fx.Niagara.TaskPriority.Dump"),
	TEXT("Dump currently set priorities"),
	FConsoleCommandDelegate::CreateLambda(
		[]()
		{
			auto DumpPriority =
				[](int32 Priority, const TCHAR* TaskName)
				{
					const ENamedThreads::Type TaskThread = GetNiagaraTaskPriority(Priority);
					UE_LOG(LogNiagara, Log, TEXT("%s = %d = Thread Priority(%d) Task Priority(%d)"), TaskName, Priority, ENamedThreads::GetThreadPriorityIndex(TaskThread), ENamedThreads::GetTaskPriority(TaskThread));
				};

			
			UE_LOG(LogNiagara, Log, TEXT("=== Niagara Task Priorities"));
			DumpPriority(GNiagaraSystemSimulationTaskPri, TEXT("NiagaraSystemSimulationTask"));
			DumpPriority(GNiagaraSystemInstanceTaskPri, TEXT("NiagaraSystemInstanceTask"));
			DumpPriority(GNiagaraSystemSimulationWaitAllTaskPri, TEXT("NiagaraSystemSimulationWaitAllTask"));
		}
	)
);

//////////////////////////////////////////////////////////////////////////

FNiagaraSystemSimulationTickContext FNiagaraSystemSimulationTickContext::MakeContextForTicking(class FNiagaraSystemSimulation* Owner, TArray<FNiagaraSystemInstance*>& Instances, FNiagaraDataSet& DataSet, float DeltaSeconds, int32 SpawnNum, const FGraphEventRef& MyCompletionGraphEvent)
{
	static const auto EffectsQualityCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("sg.EffectsQuality"));
	check(EffectsQualityCVar != nullptr);

	FNiagaraSystemSimulationTickContext Context(Instances, DataSet);
	Context.Owner = Owner;
	Context.System = Owner->GetSystem();
	Context.DeltaSeconds = DeltaSeconds;
	Context.SpawnNum = SpawnNum;
	Context.MyCompletionGraphEvent = MyCompletionGraphEvent;
	Context.FinalizeEvents = nullptr;
	Context.bTickAsync = MyCompletionGraphEvent.IsValid() && GbParallelSystemSimTick && FApp::ShouldUseThreadingForPerformance();
	Context.bTickInstancesAsync = MyCompletionGraphEvent.IsValid() && GbParallelSystemInstanceTick && FApp::ShouldUseThreadingForPerformance() && !Owner->GetIsSolo();
	Context.EffectsQuality = EffectsQualityCVar->GetInt();
	return Context;
}

FNiagaraSystemSimulationTickContext FNiagaraSystemSimulationTickContext::MakeContextForSpawning(class FNiagaraSystemSimulation* Owner, TArray<FNiagaraSystemInstance*>& Instances, FNiagaraDataSet& DataSet, float DeltaSeconds, int32 SpawnNum, bool bAllowAsync)
{
	static const auto EffectsQualityCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("sg.EffectsQuality"));
	check(EffectsQualityCVar != nullptr);

	FNiagaraSystemSimulationTickContext Context(Instances, DataSet);
	Context.Owner = Owner;
	Context.System = Owner->GetSystem();
	Context.DeltaSeconds = DeltaSeconds;
	Context.SpawnNum = SpawnNum;
	Context.MyCompletionGraphEvent = nullptr;
	Context.FinalizeEvents = nullptr;
	Context.bTickAsync = bAllowAsync && GbParallelSystemSimTick && FApp::ShouldUseThreadingForPerformance();
	Context.bTickInstancesAsync = false;
	Context.EffectsQuality = EffectsQualityCVar->GetInt();
	return Context;
}

//////////////////////////////////////////////////////////////////////////

// This task is used to wait for all finalize tasks to complete
class FNiagaraSystemSimulationWaitAllFinalizeTask
{
	FGraphEventArray EventsToWaitFor;
public:
	FNiagaraSystemSimulationWaitAllFinalizeTask(FGraphEventArray*& OutEventsToWaitFor)
	{
		OutEventsToWaitFor = &EventsToWaitFor;
	}

	FORCEINLINE TStatId GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(FNiagaraSystemSimulationWaitAllFinalizeTask, STATGROUP_TaskGraphTasks); }
	ENamedThreads::Type GetDesiredThread() { return GetNiagaraTaskPriority(GNiagaraSystemSimulationWaitAllTaskPri); }
	static ESubsequentsMode::Type GetSubsequentsMode() { return ESubsequentsMode::TrackSubsequents; }

	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		for ( FGraphEventRef& Event : EventsToWaitFor )
		{
			MyCompletionGraphEvent->DontCompleteUntil(Event);
		}
		EventsToWaitFor.Empty();
	}
};

//This task performs the concurrent part of the system simulation tick.
class FNiagaraSystemSimulationTickTask
{
	FNiagaraSystemSimulationTickContext Context;
	TGraphTask<FNiagaraSystemSimulationWaitAllFinalizeTask>* WaitAllFinalizeTask;
public:
	FNiagaraSystemSimulationTickTask(FNiagaraSystemSimulationTickContext InContext, TGraphTask<FNiagaraSystemSimulationWaitAllFinalizeTask>* InWaitAllFinalizeTask)
		: Context(InContext)
		, WaitAllFinalizeTask(InWaitAllFinalizeTask)
	{
	}

	FORCEINLINE TStatId GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(FNiagaraSystemSimulationTickTask, STATGROUP_TaskGraphTasks); }
	ENamedThreads::Type GetDesiredThread() { return GetNiagaraTaskPriority(GNiagaraSystemSimulationTaskPri); }
	static ESubsequentsMode::Type GetSubsequentsMode() { return ESubsequentsMode::TrackSubsequents; }

	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		{
			PARTICLE_PERF_STAT_CYCLES_GT(Context.System, TickConcurrent);
			Context.MyCompletionGraphEvent = MyCompletionGraphEvent;
			Context.Owner->Tick_Concurrent(Context);
			Context.FinalizeEvents = nullptr;
		}
		WaitAllFinalizeTask->Unlock();
	}
};

//This task performs the concurrent part of spawning new instances
class FNiagaraSystemSimulationSpawnPendingTask
{
	FNiagaraSystemSimulationTickContext Context;
	TGraphTask<FNiagaraSystemSimulationWaitAllFinalizeTask>* WaitAllFinalizeTask;
public:
	FNiagaraSystemSimulationSpawnPendingTask(FNiagaraSystemSimulationTickContext InContext, TGraphTask<FNiagaraSystemSimulationWaitAllFinalizeTask>* InWaitAllFinalizeTask)
		: Context(InContext)
		, WaitAllFinalizeTask(InWaitAllFinalizeTask)
	{
	}

	FORCEINLINE TStatId GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(FNiagaraSystemSimulationSpawnPendingTask, STATGROUP_TaskGraphTasks); }
	ENamedThreads::Type GetDesiredThread() { return GetNiagaraTaskPriority(GNiagaraSystemSimulationSpawnPendingTaskPri); }
	static ESubsequentsMode::Type GetSubsequentsMode() { return ESubsequentsMode::TrackSubsequents; }

	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		{
			PARTICLE_PERF_STAT_CYCLES_GT(Context.System, TickConcurrent);
			Context.MyCompletionGraphEvent = MyCompletionGraphEvent;
			Context.Owner->Spawn_Concurrent(Context);
			Context.FinalizeEvents = nullptr;
		}
		WaitAllFinalizeTask->Unlock();
	}
};

//////////////////////////////////////////////////////////////////////////

/** 
Task to call FinalizeTick_GameThread() on a batch of FNiagaraSystemInstances 
Must be done on the game thread.
*/
class FNiagaraSystemInstanceFinalizeTask
{
	FNiagaraSystemSimulation* SystemSim;
	FNiagaraSystemTickBatch Batch;
public:
	FNiagaraSystemInstanceFinalizeTask(FNiagaraSystemSimulation* InSystemSim, FNiagaraSystemTickBatch& InBatch)
		: SystemSim(InSystemSim)
		, Batch(InBatch)
	{

	}

	FORCEINLINE TStatId GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(FNiagaraSystemInstanceFinalizeTask, STATGROUP_TaskGraphTasks); }
	ENamedThreads::Type GetDesiredThread() { return ENamedThreads::GameThread; }
	static ESubsequentsMode::Type GetSubsequentsMode() { return ESubsequentsMode::TrackSubsequents; }

	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		check(CurrentThread == ENamedThreads::GameThread);
		FNiagaraScopedRuntimeCycleCounter RuntimeScope(SystemSim->GetSystem(), true, false);

		PARTICLE_PERF_STAT_CYCLES_GT(SystemSim->GetSystem(), Finalize);

		ENiagaraGPUTickHandlingMode Mode = SystemSim->GetGPUTickHandlingMode();
		if(Mode == ENiagaraGPUTickHandlingMode::GameThreadBatched)
		{
			TArray<FNiagaraGPUSystemTick, TInlineAllocator<NiagaraSystemTickBatchSize>> GPUTicks;
			GPUTicks.Reserve(Batch.Num());
			for (FNiagaraSystemInstance* Inst : Batch)
			{
				//Don't submit if the finalize was already done in a wait() as then we'd be double ticking GPU emitters.
				if(Inst->FinalizeTick_GameThread(false))
				{
					if(Inst->NeedsGPUTick())
					{
						Inst->InitGPUTick(GPUTicks.AddDefaulted_GetRef());
					}
				}
			}

			if(GPUTicks.Num() > 0)
			{
				NiagaraEmitterInstanceBatcher* TheBatcher = SystemSim->GetBatcher();
				ENQUEUE_RENDER_COMMAND(FNiagaraGiveSystemInstanceTickToRT)(
					[TheBatcher, GPUTicks](FRHICommandListImmediate& RHICmdList) mutable
					{
						for(auto& GPUTick : GPUTicks)
						{
							TheBatcher->GiveSystemTick_RenderThread(GPUTick);
						}
					}
				);
			}
		}
		else
		{
			for (FNiagaraSystemInstance* Inst : Batch)
			{
				Inst->FinalizeTick_GameThread();
			}
		}
	}
};

/** 
Async task to call Tick_Concurrent() on batches of FNiagaraSystemInstances.
Can be performed on task threads. 
*/
class FNiagaraSystemInstanceAsyncTask
{
	FNiagaraSystemSimulation* SystemSim;
	FNiagaraSystemTickBatch Batch;

public:
	FNiagaraSystemInstanceAsyncTask(FNiagaraSystemSimulation* InSystemSim, FNiagaraSystemTickBatch& InBatch)
		: SystemSim(InSystemSim)
		, Batch(InBatch)
	{
	}

	FORCEINLINE TStatId GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(FNiagaraSystemInstanceAsyncTask, STATGROUP_TaskGraphTasks); }
	ENamedThreads::Type GetDesiredThread() { return GetNiagaraTaskPriority(GNiagaraSystemInstanceTaskPri); }
	static ESubsequentsMode::Type GetSubsequentsMode() { return ESubsequentsMode::TrackSubsequents; }

	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		PARTICLE_PERF_STAT_CYCLES_GT(Batch[0]->GetSystem(), TickConcurrent);

		ENiagaraGPUTickHandlingMode Mode = SystemSim->GetGPUTickHandlingMode();
		if (Mode == ENiagaraGPUTickHandlingMode::ConcurrentBatched)
		{
			TArray<FNiagaraGPUSystemTick, TInlineAllocator<NiagaraSystemTickBatchSize>> GPUTicks;
			GPUTicks.Reserve(Batch.Num());
			for (FNiagaraSystemInstance* Inst : Batch)
			{
				Inst->Tick_Concurrent(false);
				if(Inst->NeedsGPUTick())
				{
					Inst->InitGPUTick(GPUTicks.AddDefaulted_GetRef());
				}
			}

			if (GPUTicks.Num() > 0)
			{
				NiagaraEmitterInstanceBatcher* TheBatcher = SystemSim->GetBatcher();
				ENQUEUE_RENDER_COMMAND(FNiagaraGiveSystemInstanceTickToRT)(
					[TheBatcher, GPUTicks](FRHICommandListImmediate& RHICmdList) mutable
					{
						for (auto& GPUTick : GPUTicks)
						{
							TheBatcher->GiveSystemTick_RenderThread(GPUTick);
						}
					}
				);
			}
		}
		else
		{
			for (FNiagaraSystemInstance* Inst : Batch)
			{
				Inst->Tick_Concurrent();
			}
		}
	}
};

//////////////////////////////////////////////////////////////////////////

void FNiagaraSystemSimulation::AddReferencedObjects(FReferenceCollector& Collector)
{
	//We keep a hard ref to the system.
	Collector.AddReferencedObject(EffectType);
}

FNiagaraSystemSimulation::FNiagaraSystemSimulation()
	: EffectType(nullptr)
	, SystemTickGroup(TG_MAX)
	, World(nullptr)
	, bCanExecute(false)
	, bBindingsInitialized(false)
	, bInSpawnPhase(false)
	, bIsSolo(false)
{

}

FNiagaraSystemSimulation::~FNiagaraSystemSimulation()
{
	Destroy();
}

bool FNiagaraSystemSimulation::Init(UNiagaraSystem* InSystem, UWorld* InWorld, bool bInIsSolo, ETickingGroup InTickGroup)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSystemSim_Init);
	UNiagaraSystem* System = InSystem;
	WeakSystem = System;

	EffectType = InSystem->GetEffectType();
	SystemTickGroup = InTickGroup;

	World = InWorld;

	bIsSolo = bInIsSolo;

	bBindingsInitialized = false;
	bInSpawnPhase = false;

	FNiagaraWorldManager* WorldMan = FNiagaraWorldManager::Get(InWorld);
	check(WorldMan);

	Batcher = nullptr;
	if (World && World->Scene)
	{
		FFXSystemInterface* FXSystemInterface = World->Scene->GetFXSystem();
		if (FXSystemInterface)
		{
			Batcher = static_cast<NiagaraEmitterInstanceBatcher*>(FXSystemInterface->GetInterface(NiagaraEmitterInstanceBatcher::Name));
		}
	}

	bCanExecute = System->GetSystemSpawnScript()->GetVMExecutableData().IsValid() && System->GetSystemUpdateScript()->GetVMExecutableData().IsValid();

	MaxDeltaTime = System->GetMaxDeltaTime();

	if (bCanExecute)
	{
		{
			//SCOPE_CYCLE_COUNTER(STAT_NiagaraSystemSim_Init_DataSets);

			const FNiagaraSystemCompiledData& SystemCompiledData = System->GetSystemCompiledData();
			//Initialize the main simulation dataset.
			MainDataSet.Init(&SystemCompiledData.DataSetCompiledData);

			//Initialize the main simulation dataset.
			SpawningDataSet.Init(&SystemCompiledData.DataSetCompiledData);

			//Initialize the dataset for paused systems.
			PausedInstanceData.Init(&SystemCompiledData.DataSetCompiledData);
			
			SpawnInstanceParameterDataSet.Init(&SystemCompiledData.SpawnInstanceParamsDataSetCompiledData);

			UpdateInstanceParameterDataSet.Init(&SystemCompiledData.UpdateInstanceParamsDataSetCompiledData);
		}

		UNiagaraScript* SpawnScript = System->GetSystemSpawnScript();
		UNiagaraScript* UpdateScript = System->GetSystemUpdateScript();

		{
			//SCOPE_CYCLE_COUNTER(STAT_NiagaraSystemSim_Init_ExecContexts);

			if (UseLegacySystemSimulationContexts())
			{
				SpawnExecContext = MakeUnique<FNiagaraScriptExecutionContext>();
				UpdateExecContext = MakeUnique<FNiagaraScriptExecutionContext>();
				bCanExecute &= SpawnExecContext->Init(SpawnScript, ENiagaraSimTarget::CPUSim);
				bCanExecute &= UpdateExecContext->Init(UpdateScript, ENiagaraSimTarget::CPUSim);
			}
			else
			{
				SpawnExecContext = MakeUnique<FNiagaraSystemScriptExecutionContext>(ENiagaraSystemSimulationScript::Spawn);
				UpdateExecContext = MakeUnique<FNiagaraSystemScriptExecutionContext>(ENiagaraSystemSimulationScript::Update);
				bCanExecute &= SpawnExecContext->Init(SpawnScript, ENiagaraSimTarget::CPUSim);
				bCanExecute &= UpdateExecContext->Init(UpdateScript, ENiagaraSimTarget::CPUSim);
			}
		}

		{
			//SCOPE_CYCLE_COUNTER(STAT_NiagaraSystemSim_Init_BindParams);

			//Bind parameter collections.
			for (UNiagaraParameterCollection* Collection : SpawnScript->GetCachedParameterCollectionReferences())
			{
				GetParameterCollectionInstance(Collection)->GetParameterStore().Bind(&SpawnExecContext->Parameters);
			}
			for (UNiagaraParameterCollection* Collection : UpdateScript->GetCachedParameterCollectionReferences())
			{
				GetParameterCollectionInstance(Collection)->GetParameterStore().Bind(&UpdateExecContext->Parameters);
			}

			TArray<UNiagaraScript*, TInlineAllocator<2>> Scripts;
			Scripts.Add(SpawnScript);
			Scripts.Add(UpdateScript);
			FNiagaraUtilities::CollectScriptDataInterfaceParameters(*System, Scripts, ScriptDefinedDataInterfaceParameters);

			ScriptDefinedDataInterfaceParameters.Bind(&SpawnExecContext->Parameters);
			ScriptDefinedDataInterfaceParameters.Bind(&UpdateExecContext->Parameters);

			SpawnScript->RapidIterationParameters.Bind(&SpawnExecContext->Parameters);
			UpdateScript->RapidIterationParameters.Bind(&UpdateExecContext->Parameters);

			// If this simulation is not solo than we have bind the source system parameters to the system simulation contexts so that
			// the system and emitter scripts use the default shared data interfaces.
			if (UseLegacySystemSimulationContexts() && !bIsSolo)
			{
				FNiagaraUserRedirectionParameterStore& ExposedParameters = System->GetExposedParameters();
				ExposedParameters.Bind(&SpawnExecContext->Parameters);
				ExposedParameters.Bind(&UpdateExecContext->Parameters);
			}
		}

		{
			//SCOPE_CYCLE_COUNTER(STAT_NiagaraSystemSim_Init_DirectBindings);

			SpawnNumSystemInstancesParam.Init(SpawnExecContext->Parameters, SYS_PARAM_ENGINE_NUM_SYSTEM_INSTANCES);
			UpdateNumSystemInstancesParam.Init(UpdateExecContext->Parameters, SYS_PARAM_ENGINE_NUM_SYSTEM_INSTANCES);
			SpawnGlobalSpawnCountScaleParam.Init(SpawnExecContext->Parameters, SYS_PARAM_ENGINE_GLOBAL_SPAWN_COUNT_SCALE);
			UpdateGlobalSpawnCountScaleParam.Init(UpdateExecContext->Parameters, SYS_PARAM_ENGINE_GLOBAL_SPAWN_COUNT_SCALE);
			SpawnGlobalSystemCountScaleParam.Init(SpawnExecContext->Parameters, SYS_PARAM_ENGINE_GLOBAL_SYSTEM_COUNT_SCALE);
			UpdateGlobalSystemCountScaleParam.Init(UpdateExecContext->Parameters, SYS_PARAM_ENGINE_GLOBAL_SYSTEM_COUNT_SCALE);
		}
	}

	return true;
}

void FNiagaraSystemSimulation::Destroy()
{
	check(IsInGameThread());
	WaitForSystemTickComplete();

	while (SystemInstances.Num())
	{
		FNiagaraSystemInstance* Inst = SystemInstances.Last();
		check(Inst);
		Inst->Deactivate(true);		
	}
	while (PendingSystemInstances.Num())
	{
		FNiagaraSystemInstance* Inst = PendingSystemInstances.Last();
		check(Inst);
		Inst->Deactivate(true);		
	}
	SystemInstances.Empty();
	PendingSystemInstances.Empty();

	FNiagaraWorldManager* WorldMan = FNiagaraWorldManager::Get(World);
	check(WorldMan);
	SpawnExecContext->Parameters.UnbindFromSourceStores();
	UpdateExecContext->Parameters.UnbindFromSourceStores();
}

UNiagaraParameterCollectionInstance* FNiagaraSystemSimulation::GetParameterCollectionInstance(UNiagaraParameterCollection* Collection)
{
	UNiagaraSystem* System = WeakSystem.Get();
	UNiagaraParameterCollectionInstance* Ret = nullptr;

	if (System)
	{
		System->GetParameterCollectionOverride(Collection);
	}

	//If no explicit override from the system, just get the current instance set on the world.
	if (!Ret)
	{
		if(FNiagaraWorldManager* WorldMan = FNiagaraWorldManager::Get(World))
		{
			Ret = WorldMan->GetParameterCollection(Collection);
		}
	}

	return Ret;
}

FNiagaraParameterStore& FNiagaraSystemSimulation::GetScriptDefinedDataInterfaceParameters()
{
	return ScriptDefinedDataInterfaceParameters;
}

void FNiagaraSystemSimulation::TransferInstance(FNiagaraSystemSimulation* SourceSimulation, FNiagaraSystemInstance* SystemInst)
{
	check(SourceSimulation->GetSystem() == GetSystem());
	check(SystemInst);

	check(!SystemInst->IsPaused());
	check(!bInSpawnPhase);
	check(!SourceSimulation->bInSpawnPhase);

	WaitForInstancesTickComplete();
	SourceSimulation->WaitForInstancesTickComplete();

	int32 SystemInstIdx = SystemInst->SystemInstanceIndex;
	if (!SystemInst->IsPendingSpawn() && SystemInst->SystemInstanceIndex != INDEX_NONE)
	{
// 		UE_LOG(LogNiagara, Log, TEXT("== Dataset Transfer ========================"));
// 		UE_LOG(LogNiagara, Log, TEXT(" ----- Existing values in src. Idx: %d -----"), SystemInstIdx);
// 		SourceSimulation->DataSet.Dump(true, SystemInstIdx, 1);

		//If we're not pending then the system actually has data to pull over. This is not fast.
		int32 NewDataSetIndex = MainDataSet.GetCurrentDataChecked().TransferInstance(SourceSimulation->MainDataSet.GetCurrentDataChecked(), SystemInstIdx, false);

// 		UE_LOG(LogNiagara, Log, TEXT(" ----- Transfered values in dest. Idx: %d -----"), NewDataIndex);
// 		DataSet.Dump(true, NewDataIndex, 1);
	
		SourceSimulation->RemoveInstance(SystemInst);
	
		//Move the system direct to the new sim's 
		SystemInst->SystemInstanceIndex = SystemInstances.Add(SystemInst);
		check(NewDataSetIndex == SystemInst->SystemInstanceIndex);

		if (!bBindingsInitialized)
		{
			InitParameterDataSetBindings(SystemInst);
		}
	}
	else
	{
		SourceSimulation->RemoveInstance(SystemInst);

		AddInstance(SystemInst);			
	}

	SystemInst->SystemSimulation = this->AsShared();
}

void FNiagaraSystemSimulation::DumpInstance(const FNiagaraSystemInstance* Inst)const
{
	ensure(Inst->bAsyncWorkInProgress == false);

	UE_LOG(LogNiagara, Log, TEXT("==  %s (%d) ========"), *Inst->GetSystem()->GetFullName(), Inst->SystemInstanceIndex);
	UE_LOG(LogNiagara, Log, TEXT(".................Spawn................."));
	SpawnExecContext->Parameters.DumpParameters(false);
	SpawnInstanceParameterDataSet.Dump(Inst->SystemInstanceIndex, 1, TEXT("Spawn Instance Parameters"));
	UE_LOG(LogNiagara, Log, TEXT(".................Update................."));
	UpdateExecContext->Parameters.DumpParameters(false);
	UpdateInstanceParameterDataSet.Dump(Inst->SystemInstanceIndex, 1, TEXT("Update Instance Parameters"));
	UE_LOG(LogNiagara, Log, TEXT("................. System Instance ................."));
	MainDataSet.Dump(Inst->SystemInstanceIndex, 1, TEXT("System Data"));
}

void FNiagaraSystemSimulation::DumpTickInfo(FOutputDevice& Ar)
{
	check(IsInGameThread());
	if (SystemInstances.Num() > 0)
	{
		Ar.Logf(TEXT("\t\tSystemInstances %d"), SystemInstances.Num());
		for (FNiagaraSystemInstance* Instance : SystemInstances)
		{
			Instance->DumpTickInfo(Ar);
		}
	}

	if (PendingSystemInstances.Num() > 0)
	{
		Ar.Logf(TEXT("\t\tPendingSystemInstances %d"), PendingSystemInstances.Num());
		for (FNiagaraSystemInstance* Instance : PendingSystemInstances)
		{
			Instance->DumpTickInfo(Ar);
		}
	}

	if (PausedSystemInstances.Num() > 0)
	{
		Ar.Logf(TEXT("\t\tPausedSystemInstances %d"), PausedSystemInstances.Num());
		for (FNiagaraSystemInstance* Instance : PausedSystemInstances)
		{
			Instance->DumpTickInfo(Ar);
		}
	}
}

void FNiagaraSystemSimulation::AddTickGroupPromotion(FNiagaraSystemInstance* Instance)
{
	check(IsInGameThread());
	check(!PendingTickGroupPromotions.Contains(Instance));
	PendingTickGroupPromotions.Add(Instance);

	check(bIsSolo == false);

	FNiagaraWorldManager* WorldManager = FNiagaraWorldManager::Get(World);
	check(WorldManager != nullptr);
	WorldManager->MarkSimulationForPostActorWork(this);
}

int32 FNiagaraSystemSimulation::AddPendingSystemInstance(FNiagaraSystemInstance* Instance)
{
	check(IsInGameThread());
	check(!PendingSystemInstances.Contains(Instance));

	if (bIsSolo == false)
	{
		FNiagaraWorldManager* WorldManager = FNiagaraWorldManager::Get(World);
		check(WorldManager != nullptr);
		WorldManager->MarkSimulationForPostActorWork(this);
	}

	return PendingSystemInstances.Add(Instance);
}

void FNiagaraSystemSimulation::AddSystemToTickBatch(FNiagaraSystemInstance* Instance, FNiagaraSystemSimulationTickContext& Context)
{
	TickBatch.Add(Instance);
	if (TickBatch.Num() == GbParallelSystemInstanceTickBatchSize)
	{
		FlushTickBatch(Context);
	}
}

void FNiagaraSystemSimulation::FlushTickBatch(FNiagaraSystemSimulationTickContext& Context)
{
	if (TickBatch.Num() > 0)
	{
		FGraphEventArray FinalizePrereq;
		FinalizePrereq.Add(Context.MyCompletionGraphEvent);

		// Enqueue or tick the instances
		if ( Context.bTickInstancesAsync )
		{
			check(Context.FinalizeEvents != nullptr);

			FGraphEventRef AsyncTask = TGraphTask<FNiagaraSystemInstanceAsyncTask>::CreateTask(nullptr).ConstructAndDispatchWhenReady(this, TickBatch);
			FinalizePrereq.Add(AsyncTask);
		}
		else
		{
			for (FNiagaraSystemInstance* Inst : TickBatch)
			{
				Inst->Tick_Concurrent();
			}
		}

		// Enqueue a finalize task?
		if (Context.bTickAsync || Context.bTickInstancesAsync )
		{
			check(Context.FinalizeEvents != nullptr);

			FGraphEventRef FinalizeTask = TGraphTask<FNiagaraSystemInstanceFinalizeTask>::CreateTask(&FinalizePrereq).ConstructAndDispatchWhenReady(this, TickBatch);
			Context.FinalizeEvents->Add(FinalizeTask);
		}

		TickBatch.Reset();
	}
}

/** First phase of system sim tick. Must run on GameThread. */
void FNiagaraSystemSimulation::Tick_GameThread(float DeltaSeconds, const FGraphEventRef& MyCompletionGraphEvent)
{
	if ((PendingSystemInstances.Num() == 0 && SystemInstances.Num() == 0) || !bCanExecute)
	{
		return;
	}

	if ((GNiagaraSkipTickDeltaSeconds > 0.0f) && (DeltaSeconds <= GNiagaraSkipTickDeltaSeconds))
	{
		return;
	}

	check(IsInGameThread());
	check(bInSpawnPhase == false);

	FNiagaraCrashReporterScope CRScope(this);

	WaitForSystemTickComplete(true);

	SCOPE_CYCLE_COUNTER(STAT_NiagaraOverview_GT);
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSystemSim_TickGT);
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(Effects);
	LLM_SCOPE(ELLMTag::Niagara);
	FScopeCycleCounterUObject AdditionalScope(GetSystem(), GET_STATID(STAT_NiagaraOverview_GT_CNC));

	UNiagaraSystem* System = WeakSystem.Get();
	FScopeCycleCounter SystemStatCounter(System->GetStatID(true, false));
	PARTICLE_PERF_STAT_INSTANCE_COUNT_GT(System, SystemInstances.Num());
	PARTICLE_PERF_STAT_CYCLES_GT(System, TickGameThread);

	SystemTickGraphEvent = nullptr;

	check(SystemInstances.Num() == MainDataSet.GetCurrentDataChecked().GetNumInstances());
	check(PausedSystemInstances.Num() == PausedInstanceData.GetCurrentDataChecked().GetNumInstances());
	FNiagaraScopedRuntimeCycleCounter RuntimeScope(System, true, false);

	if (MaxDeltaTime.IsSet())
	{
		DeltaSeconds = FMath::Clamp(DeltaSeconds, 0.0f, MaxDeltaTime.GetValue());
	}

	UNiagaraScript* SystemSpawnScript = System->GetSystemSpawnScript();
	UNiagaraScript* SystemUpdateScript = System->GetSystemUpdateScript();
#if WITH_EDITOR
	SystemSpawnScript->RapidIterationParameters.Tick();
	SystemUpdateScript->RapidIterationParameters.Tick();
#endif

	const bool bUpdateTickGroups = !bIsSolo;

	// Update instances
	int32 SystemIndex = 0;
	while (SystemIndex < SystemInstances.Num())
	{
		FNiagaraSystemInstance* Inst = SystemInstances[SystemIndex];

		// Update instance tick group, this can involve demoting the instance (i.e. removing from our list)
		if ( bUpdateTickGroups )
		{
			ETickingGroup DesiredTickGroup = Inst->CalculateTickGroup();
			if (DesiredTickGroup != SystemTickGroup )
			{
				// Tick demotion we need to do this now to ensure we complete in the correct group
				if (DesiredTickGroup > SystemTickGroup)
				{
					FNiagaraWorldManager* WorldManager = FNiagaraWorldManager::Get(World);
					check(WorldManager != nullptr);

					TSharedPtr<FNiagaraSystemSimulation, ESPMode::ThreadSafe> NewSim = WorldManager->GetSystemSimulation(DesiredTickGroup, System);
					NewSim->WaitForInstancesTickComplete();
					NewSim->MainDataSet.GetCurrentDataChecked().TransferInstance(MainDataSet.GetCurrentDataChecked(), SystemIndex, true);

					SystemInstances.RemoveAtSwap(SystemIndex);
					if (SystemInstances.IsValidIndex(SystemIndex))
					{
						SystemInstances[SystemIndex]->SystemInstanceIndex = SystemIndex;
					}

					Inst->SystemInstanceIndex = NewSim->SystemInstances.Add(Inst);
					Inst->SystemSimulation = NewSim;

					if (!NewSim->bBindingsInitialized)
					{
						NewSim->InitParameterDataSetBindings(Inst);
					}
					continue;
				}
				// Tick promotions must be deferred as the tick group has already been processed
				//-OPT: We could tick in this group and add a task dependent on both groups to do the transform async
				else
				{
					AddTickGroupPromotion(Inst);
				}
			}
		}

		// Perform instance tick
		Inst->Tick_GameThread(DeltaSeconds);

		// TickDataInterfaces could remove the system so we only increment if the system has changed
		// Also possible for this system to have been transfered to another system simulation.
		if ( Inst->GetSystemSimulation().Get() == this )
		{
			if (Inst->SystemInstanceIndex != INDEX_NONE)
			{
				check(Inst->SystemInstanceIndex == SystemIndex);
				++SystemIndex;
			}
		}
	}

	//Setup the few real constants like delta time.
	SetupParameters_GameThread(DeltaSeconds);

	// Somethings we don't want to happen during the spawn phase
	int32 SpawnNum = 0;
	if (PendingSystemInstances.Num() > 0)
	{
		SystemInstances.Reserve(SystemInstances.Num() + PendingSystemInstances.Num());

		SystemIndex = 0;
		while (SystemIndex < PendingSystemInstances.Num())
		{
			FNiagaraSystemInstance* Inst = PendingSystemInstances[SystemIndex];
			// Gather any pending spawn systems and add to the end of the system instances

			if (Inst->IsPaused())
			{
				++SystemIndex;
				continue;
			}

			// If we are paused continue

			if (!bIsSolo)
			{
				const ETickingGroup DesiredTickGroup = Inst->CalculateTickGroup();
				if (DesiredTickGroup != SystemTickGroup)
				{
					PendingSystemInstances.RemoveAtSwap(SystemIndex);
					if (PendingSystemInstances.IsValidIndex(SystemIndex))
					{
						PendingSystemInstances[SystemIndex]->SystemInstanceIndex = SystemIndex;
					}
					Inst->SystemInstanceIndex = INDEX_NONE;

					FNiagaraWorldManager* WorldManager = FNiagaraWorldManager::Get(World);
					check(WorldManager != nullptr);

					TSharedPtr<FNiagaraSystemSimulation, ESPMode::ThreadSafe> DestSim = WorldManager->GetSystemSimulation(DesiredTickGroup, System);

					Inst->SystemSimulation = DestSim;
					Inst->SystemInstanceIndex = DestSim->AddPendingSystemInstance(Inst);
					continue;
				}
			}

			// Execute instance tick
			Inst->Tick_GameThread(DeltaSeconds);

			if (Inst->SystemInstanceIndex != INDEX_NONE)
			{
				// We should not move tick group during Tick_GameThread but let's be safe
				check(Inst->SystemSimulation.Get() == this);

				// When the first instance is added we need to initialize the parameter store to data set bindings.
				if (!bBindingsInitialized)
				{
					InitParameterDataSetBindings(Inst);
				}

				check(PendingSystemInstances[SystemIndex] == Inst);
				PendingSystemInstances.RemoveAtSwap(SystemIndex);
				if (PendingSystemInstances.IsValidIndex(SystemIndex))
				{
					PendingSystemInstances[SystemIndex]->SystemInstanceIndex = SystemIndex;
				}

				Inst->SystemInstanceIndex = SystemInstances.Add(Inst);
				Inst->SetPendingSpawn(false);
				++SpawnNum;
			}
		}
	}

	FNiagaraSystemSimulationTickContext Context = FNiagaraSystemSimulationTickContext::MakeContextForTicking(this, SystemInstances, MainDataSet, DeltaSeconds, SpawnNum, MyCompletionGraphEvent);

	//Solo systems add their counts in their component tick.
	if (GetIsSolo() == false)
	{
		System->AddToInstanceCountStat(SystemInstances.Num(), false);
		INC_DWORD_STAT_BY(STAT_TotalNiagaraSystemInstances, SystemInstances.Num());
	}

	//Now kick of the concurrent tick.
	if (Context.bTickAsync)
	{
		TGraphTask<FNiagaraSystemSimulationWaitAllFinalizeTask>* WaitAllFinalizeTask = TGraphTask<FNiagaraSystemSimulationWaitAllFinalizeTask>::CreateTask(nullptr, ENamedThreads::GameThread).ConstructAndHold(Context.FinalizeEvents);
		FGraphEventRef FinalizeGraphEvent = WaitAllFinalizeTask->GetCompletionEvent();

		TGraphTask<FNiagaraSystemSimulationTickTask>* SimulationTickTask = TGraphTask<FNiagaraSystemSimulationTickTask>::CreateTask(nullptr, ENamedThreads::GameThread).ConstructAndHold(Context, WaitAllFinalizeTask);
		SystemTickGraphEvent = SimulationTickTask->GetCompletionEvent();
		Context.FinalizeEvents->Add(SystemTickGraphEvent);

		MyCompletionGraphEvent->DontCompleteUntil(FinalizeGraphEvent);

		SimulationTickTask->Unlock(ENamedThreads::GameThread);
	}
	else
	{
		TGraphTask<FNiagaraSystemSimulationWaitAllFinalizeTask>* WaitAllFinalizeTask = nullptr;
		if (Context.bTickInstancesAsync)
		{
			WaitAllFinalizeTask = TGraphTask<FNiagaraSystemSimulationWaitAllFinalizeTask>::CreateTask(nullptr, ENamedThreads::GameThread).ConstructAndHold(Context.FinalizeEvents);
		}

		Tick_Concurrent(Context);

		if (Context.bTickInstancesAsync)
		{
			WaitAllFinalizeTask->Unlock(ENamedThreads::GameThread);
			Context.FinalizeEvents = nullptr;
		}
	}
}

void FNiagaraSystemSimulation::UpdateTickGroups_GameThread()
{
	check(IsInGameThread());
	check(!bIsSolo);

	SCOPE_CYCLE_COUNTER(STAT_NiagaraSystemSim_SpawnNewGT);
	SCOPE_CYCLE_COUNTER(STAT_NiagaraOverview_GT);
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(Effects);
	LLM_SCOPE(ELLMTag::Niagara);
	FScopeCycleCounterUObject AdditionalScope(GetSystem(), GET_STATID(STAT_NiagaraOverview_GT_CNC));

	FNiagaraWorldManager* WorldManager = FNiagaraWorldManager::Get(World);
	check(WorldManager != nullptr);

	UNiagaraSystem* System = WeakSystem.Get();
	check(System != nullptr);

	FNiagaraScopedRuntimeCycleCounter RuntimeScope(System, true, false);

	// Transfer promoted instances to the new tick group
	//-OPT: This can be done async
	while (PendingTickGroupPromotions.Num() > 0)
	{
		FNiagaraSystemInstance* Instance = PendingTickGroupPromotions.Pop(false);

		const ETickingGroup TickGroup = Instance->CalculateTickGroup();
		if (TickGroup != SystemTickGroup)
		{
			TSharedPtr<FNiagaraSystemSimulation, ESPMode::ThreadSafe> NewSim = WorldManager->GetSystemSimulation(TickGroup, System);
			NewSim->TransferInstance(this, Instance);
		}
	}
	PendingTickGroupPromotions.Reset();

	// Move pending system instances into new tick groups
	int32 SystemIndex = 0;
	while ( SystemIndex < PendingSystemInstances.Num() )
	{
		FNiagaraSystemInstance* Instance = PendingSystemInstances[SystemIndex];
		if ( !Instance->IsPaused() )
		{
			const ETickingGroup DesiredTickGroup = Instance->CalculateTickGroup();
			if (DesiredTickGroup != SystemTickGroup)
			{
				PendingSystemInstances.RemoveAtSwap(SystemIndex);
				if (PendingSystemInstances.IsValidIndex(SystemIndex))
				{
					PendingSystemInstances[SystemIndex]->SystemInstanceIndex = SystemIndex;
				}
				Instance->SystemInstanceIndex = INDEX_NONE;

				TSharedPtr<FNiagaraSystemSimulation, ESPMode::ThreadSafe> DestSim = WorldManager->GetSystemSimulation(DesiredTickGroup, System);

				Instance->SystemSimulation = DestSim;
				Instance->SystemInstanceIndex = DestSim->AddPendingSystemInstance(Instance);
				continue;
			}
		}
		++SystemIndex;
	}
}

void FNiagaraSystemSimulation::Spawn_GameThread(float DeltaSeconds, bool bPostActorTick)
{
	// Early out, nothing to do
	if (PendingSystemInstances.Num() == 0 || !bCanExecute)
	{
		return;
	}

	if (SystemTickGraphEvent.IsValid() && !SystemTickGraphEvent->IsComplete())
	{
		//-OPT: We could chain a task onto the existing one here if we had a reference to the finalize all task
		if (bPostActorTick == false)
		{
			return;
		}

		// We should not wait here in post actor tick, but we will be safe and warn
		WaitForInstancesTickComplete(true);
	}
	SystemTickGraphEvent = nullptr;

	SCOPE_CYCLE_COUNTER(STAT_NiagaraSystemSim_SpawnNewGT);
	SCOPE_CYCLE_COUNTER(STAT_NiagaraOverview_GT);
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(Effects);
	LLM_SCOPE(ELLMTag::Niagara);

	UNiagaraSystem* System = WeakSystem.Get();
	FScopeCycleCounterUObject AdditionalScope(System, GET_STATID(STAT_NiagaraOverview_GT_CNC));

	FNiagaraCrashReporterScope CRScope(this);

	bInSpawnPhase = true;

	if (MaxDeltaTime.IsSet())
	{
		DeltaSeconds = FMath::Clamp(DeltaSeconds, 0.0f, MaxDeltaTime.GetValue());
	}

#if WITH_EDITOR
	System->GetSystemSpawnScript()->RapidIterationParameters.Tick();
	System->GetSystemUpdateScript()->RapidIterationParameters.Tick();
#endif

	SetupParameters_GameThread(DeltaSeconds);

	FNiagaraScopedRuntimeCycleCounter RuntimeScope(System, true, false);

	// Spawn instances
	FNiagaraWorldManager* WorldManager = FNiagaraWorldManager::Get(World);
	check(WorldManager != nullptr);

	SpawningInstances.Reserve(PendingSystemInstances.Num());

	int32 SystemIndex = 0;
	while (SystemIndex < PendingSystemInstances.Num())
	{
		FNiagaraSystemInstance* Instance = PendingSystemInstances[SystemIndex];
		if (Instance->IsPaused())
		{
			++SystemIndex;
			continue;
		}

		// Do we need to move tick group, has something changed since we last checked?
		const ETickingGroup DesiredTickGroup = Instance->CalculateTickGroup();
		if (DesiredTickGroup != SystemTickGroup)
		{
			PendingSystemInstances.RemoveAtSwap(SystemIndex);
			if (PendingSystemInstances.IsValidIndex(SystemIndex))
			{
				PendingSystemInstances[SystemIndex]->SystemInstanceIndex = SystemIndex;
			}
			Instance->SystemInstanceIndex = INDEX_NONE;

			TSharedPtr<FNiagaraSystemSimulation, ESPMode::ThreadSafe> DestSim = WorldManager->GetSystemSimulation(DesiredTickGroup, System);

			Instance->SystemSimulation = DestSim;
			Instance->SystemInstanceIndex = DestSim->AddPendingSystemInstance(Instance);
			continue;
		}

		// We are about to spawn execute game thread tick part
		Instance->Tick_GameThread(DeltaSeconds);

		// If we are still alive spawn
		if (Instance->SystemInstanceIndex != INDEX_NONE)
		{
			check(PendingSystemInstances[SystemIndex] == Instance);
			PendingSystemInstances.RemoveAtSwap(SystemIndex);
			if (PendingSystemInstances.IsValidIndex(SystemIndex))
			{
				PendingSystemInstances[SystemIndex]->SystemInstanceIndex = SystemIndex;
			}

			Instance->SystemInstanceIndex = SpawningInstances.Add(Instance);
		}
	}

	if ( SpawningInstances.Num() > 0 )
	{
		// When the first instance is added we need to initialize the parameter store to data set bindings.
		if (!bBindingsInitialized)
		{
			InitParameterDataSetBindings(SpawningInstances[0]);
		}

		// Execute Spawn_Concurrent (async or sync)
		FNiagaraSystemSimulationTickContext Context = FNiagaraSystemSimulationTickContext::MakeContextForSpawning(this, SpawningInstances, SpawningDataSet, DeltaSeconds, SpawningInstances.Num(), bPostActorTick == false);
		if (Context.bTickAsync || Context.bTickInstancesAsync)
		{
			TGraphTask<FNiagaraSystemSimulationWaitAllFinalizeTask>* WaitAllFinalizeTask = TGraphTask<FNiagaraSystemSimulationWaitAllFinalizeTask>::CreateTask(nullptr, ENamedThreads::GameThread).ConstructAndHold(Context.FinalizeEvents);
			WaitAllFinalizeTask->GetCompletionEvent();

			SystemTickGraphEvent = TGraphTask<FNiagaraSystemSimulationSpawnPendingTask>::CreateTask(nullptr, ENamedThreads::GameThread).ConstructAndDispatchWhenReady(Context, WaitAllFinalizeTask);

			check(bIsSolo == false);

			WorldManager->MarkSimulationForPostActorWork(this);
		}
		else
		{
			Spawn_Concurrent(Context);
		}
	}
	else
	{
		bInSpawnPhase = false;
	}
}

void FNiagaraSystemSimulation::Spawn_Concurrent(FNiagaraSystemSimulationTickContext& Context)
{
	check(bInSpawnPhase == true);

	Tick_Concurrent(Context);

	check(MainDataSet.GetCurrentDataChecked().GetNumInstances() == SystemInstances.Num());
	check(PausedInstanceData.GetCurrentDataChecked().GetNumInstances() == PausedSystemInstances.Num());
	check(SpawningDataSet.GetCurrentDataChecked().GetNumInstances() == SpawningInstances.Num());

	// Append spawned data to our active DataSet
	SpawningDataSet.CopyTo(MainDataSet, 0, INDEX_NONE, false);
	SpawningDataSet.ResetBuffers();

	// Move instances from spawning to active
	SystemInstances.Reserve(SystemInstances.Num() + SpawningInstances.Num());
	for (FNiagaraSystemInstance* Instance : SpawningInstances)
	{
		checkSlow(!Instance->IsComplete());
		Instance->SystemInstanceIndex = SystemInstances.Add(Instance);
		Instance->SetPendingSpawn(false);
	}
	SpawningInstances.Reset();

	check(MainDataSet.GetCurrentDataChecked().GetNumInstances() == SystemInstances.Num());
	check(PausedInstanceData.GetCurrentDataChecked().GetNumInstances() == PausedSystemInstances.Num());

	bInSpawnPhase = false;
}

void FNiagaraSystemSimulation::WaitForSystemTickComplete(bool bEnsureComplete)
{
	check(IsInGameThread());

	if (SystemTickGraphEvent.IsValid() && !SystemTickGraphEvent->IsComplete())
	{
		SCOPE_CYCLE_COUNTER(STAT_NiagaraSystemSim_ForceWaitForAsync);
		ensureAlwaysMsgf(!bEnsureComplete, TEXT("Niagara System Simulation Tasks should be complete by now. %s"), *GetSystem()->GetPathName());
		FTaskGraphInterface::Get().WaitUntilTaskCompletes(SystemTickGraphEvent, ENamedThreads::GameThread);
	}
	SystemTickGraphEvent = nullptr;
}

void FNiagaraSystemSimulation::WaitForInstancesTickComplete(bool bEnsureComplete)
{
	check(IsInGameThread());
	WaitForSystemTickComplete(bEnsureComplete);

	SCOPE_CYCLE_COUNTER(STAT_NiagaraSystemSim_ForceWaitForAsync);

	int32 SystemInstIndex = 0;
	while (SystemInstIndex < SystemInstances.Num())
	{
		// If we're in a spawn phase all existing instances should be complete already.
		FNiagaraSystemInstance* Inst = SystemInstances[SystemInstIndex];
		Inst->WaitForAsyncTickAndFinalize(bInSpawnPhase);

		// If the system completes during finalize it can be removed from instances so we don't update the index.
		if ( SystemInstances[SystemInstIndex] == Inst)
		{
			++SystemInstIndex;
		}

		check(MainDataSet.GetCurrentDataChecked().GetNumInstances() == SystemInstances.Num());
	}
}

void FNiagaraSystemSimulation::Tick_Concurrent(FNiagaraSystemSimulationTickContext& Context)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSystemSim_TickCNC);
	SCOPE_CYCLE_COUNTER(STAT_NiagaraOverview_GT_CNC);
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(Effects);
	LLM_SCOPE(ELLMTag::Niagara);

	FScopeCycleCounterUObject AdditionalScope(Context.System, GET_STATID(STAT_NiagaraOverview_GT_CNC));

	FNiagaraScopedRuntimeCycleCounter RuntimeScope(Context.System, true, true);
	FNiagaraSystemInstance* SoloSystemInstance = bIsSolo && Context.Instances.Num() == 1 ? Context.Instances[0] : nullptr;

	FNiagaraCrashReporterScope CRScope(this);

	if (bCanExecute && Context.Instances.Num())
	{
		if (GbDumpSystemData || Context.System->bDumpDebugSystemInfo)
		{
			UE_LOG(LogNiagara, Log, TEXT("=========================================================="));
			UE_LOG(LogNiagara, Log, TEXT("Niagara System Sim Tick_Concurrent(): %s"), *Context.System->GetName());
			UE_LOG(LogNiagara, Log, TEXT("=========================================================="));
		}

		FScopeCycleCounter SystemStatCounter(Context.System->GetStatID(true, true));

		for (FNiagaraSystemInstance* SystemInstance : Context.Instances)
		{
			SystemInstance->TickInstanceParameters_Concurrent();
		}

		PrepareForSystemSimulate(Context);

		if (Context.SpawnNum > 0)
		{
			SpawnSystemInstances(Context);
		}

		UpdateSystemInstances(Context);

		TransferSystemSimResults(Context);

		for (FNiagaraSystemInstance* Instance : Context.Instances)
		{
			AddSystemToTickBatch(Instance, Context);
		}
		FlushTickBatch(Context);

		//If both the instances and the main sim are run on the GT then we need to finalize here.
		if (!Context.bTickAsync && !Context.bTickInstancesAsync)
		{
			check(IsInGameThread());
			int32 SystemInstIndex = 0;
			while (SystemInstIndex < Context.Instances.Num())
			{
				FNiagaraSystemInstance* Inst = Context.Instances[SystemInstIndex];
				Inst->FinalizeTick_GameThread();

				// If the system completes during finalize it will be removed from the instances, therefore we do not need to increment our system index;
				if (!Inst->IsComplete())
				{
					++SystemInstIndex;
				}

				check(Context.DataSet.GetCurrentDataChecked().GetNumInstances() == Context.Instances.Num());
			}
		}

	#if WITH_EDITORONLY_DATA
		if (SoloSystemInstance)
		{
			SoloSystemInstance->FinishCapture();
		}
	#endif

		INC_DWORD_STAT_BY(STAT_NiagaraNumSystems, Context.Instances.Num());
	}
}

void FNiagaraSystemSimulation::SetupParameters_GameThread(float DeltaSeconds)
{
	check(IsInGameThread());

	SpawnNumSystemInstancesParam.SetValue(SystemInstances.Num());
	UpdateNumSystemInstancesParam.SetValue(SystemInstances.Num());
	SpawnGlobalSpawnCountScaleParam.SetValue(INiagaraModule::GetGlobalSpawnCountScale());
	UpdateGlobalSpawnCountScaleParam.SetValue(INiagaraModule::GetGlobalSpawnCountScale());
	SpawnGlobalSystemCountScaleParam.SetValue(INiagaraModule::GetGlobalSystemCountScale());
	UpdateGlobalSystemCountScaleParam.SetValue(INiagaraModule::GetGlobalSystemCountScale());
}

void FNiagaraSystemSimulation::PrepareForSystemSimulate(FNiagaraSystemSimulationTickContext& Context)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSystemSim_PrepareForSimulateCNC);

	const int32 NumInstances = Context.Instances.Num();
	if (NumInstances == 0)
	{
		return;
	}

	//Begin filling the state of the instance parameter datasets.
	SpawnInstanceParameterDataSet.BeginSimulate();
	UpdateInstanceParameterDataSet.BeginSimulate();

	SpawnInstanceParameterDataSet.Allocate(NumInstances);
	UpdateInstanceParameterDataSet.Allocate(NumInstances);

	UNiagaraSystem* System = GetSystem();
	check(System != nullptr);
	TConstArrayView<FNiagaraDataSetAccessor<ENiagaraExecutionState>> EmitterExecutionStateAccessors = System->GetEmitterExecutionStateAccessors();

	//Tick instance parameters and transfer any needed into the system simulation dataset.
	auto TransferInstanceParameters = [&](int32 SystemIndex)
	{
		FNiagaraSystemInstance* Inst = Context.Instances[SystemIndex];
		const FNiagaraParameterStore& InstParameters = Inst->GetInstanceParameters();

		if (InstParameters.GetParametersDirty() && bCanExecute)
		{
			SpawnInstanceParameterToDataSetBinding.ParameterStoreToDataSet(InstParameters, SpawnInstanceParameterDataSet, SystemIndex);
			UpdateInstanceParameterToDataSetBinding.ParameterStoreToDataSet(InstParameters, UpdateInstanceParameterDataSet, SystemIndex);
		}

		FNiagaraConstantBufferToDataSetBinding::CopyToDataSets(
			Context.System->GetSystemCompiledData(), *Inst, SpawnInstanceParameterDataSet, UpdateInstanceParameterDataSet, SystemIndex);

		//TODO: Find good way to check that we're not using any instance parameter data interfaces in the system scripts here.
		//In that case we need to solo and will never get here.

		TArray<TSharedRef<FNiagaraEmitterInstance, ESPMode::ThreadSafe>>& Emitters = Inst->GetEmitters();
		for (int32 EmitterIdx = 0; EmitterIdx < Emitters.Num(); ++EmitterIdx)
		{
			FNiagaraEmitterInstance& EmitterInst = Emitters[EmitterIdx].Get();
			if ( (EmitterExecutionStateAccessors.Num() > EmitterIdx) )
			{
				EmitterExecutionStateAccessors[EmitterIdx].GetWriter(Context.DataSet).SetSafe(SystemIndex, EmitterInst.GetExecutionState());
			}
		}
	};

	//This can go wide if we have a very large number of instances.
	//ParallelFor(Instances.Num(), TransferInstanceParameters, Instances.Num() < GbSystemSimTransferParamsParallelThreshold);
	ParallelFor(Context.Instances.Num(), TransferInstanceParameters, true);

	SpawnInstanceParameterDataSet.GetDestinationDataChecked().SetNumInstances(NumInstances);
	UpdateInstanceParameterDataSet.GetDestinationDataChecked().SetNumInstances(NumInstances);

	//We're done filling in the current state for the instance parameter datasets.
	SpawnInstanceParameterDataSet.EndSimulate();
	UpdateInstanceParameterDataSet.EndSimulate();
}

void FNiagaraSystemSimulation::SpawnSystemInstances(FNiagaraSystemSimulationTickContext& Context)
{
	//All instance spawning is done in a separate pass at the end of the frame so we can be sure we have all new spawns ready for processing.
	//We run the spawn and update scripts separately here as their own sim passes.

	SCOPE_CYCLE_COUNTER(STAT_NiagaraSystemSim_SpawnCNC);

	const int32 NumInstances = Context.Instances.Num();
	const int32 OrigNum = Context.Instances.Num() - Context.SpawnNum;
	const int32 SpawnNum = Context.SpawnNum;

	check(NumInstances >= Context.SpawnNum);

	FNiagaraSystemInstance* SoloSystemInstance = bIsSolo && Context.Instances.Num() == 1 ? Context.Instances[0] : nullptr;
	Context.DataSet.BeginSimulate();
	Context.DataSet.Allocate(NumInstances, true);
	Context.DataSet.GetDestinationDataChecked().SetNumInstances(NumInstances);

	// Run Spawn
	if (SpawnExecContext->Tick(SoloSystemInstance, ENiagaraSimTarget::CPUSim) == false)
	{
		for (FNiagaraSystemInstance* SystemInst : Context.Instances)
		{
			SystemInst->SetActualExecutionState(ENiagaraExecutionState::Disabled);
		}
		Context.DataSet.EndSimulate();
		return;
	}

	SpawnExecContext->BindSystemInstances(Context.Instances);
	SpawnExecContext->BindData(0, Context.DataSet, OrigNum, false);
	SpawnExecContext->BindData(1, SpawnInstanceParameterDataSet, OrigNum, false);

	FScriptExecutionConstantBufferTable SpawnConstantBufferTable;
	BuildConstantBufferTable(Context.Instances[0]->GetGlobalParameters(), SpawnExecContext, SpawnConstantBufferTable);

	SpawnExecContext->Execute(SpawnNum, SpawnConstantBufferTable);

	if (GbDumpSystemData || Context.System->bDumpDebugSystemInfo)
	{
		UE_LOG(LogNiagara, Log, TEXT("=== Spwaned %d Systems ==="), NumInstances);
		Context.DataSet.GetDestinationDataChecked().Dump(0, NumInstances, TEXT("System Dataset - Post Spawn"));
		SpawnInstanceParameterDataSet.GetCurrentDataChecked().Dump(0, NumInstances, TEXT("Spawn Instance Parameter Data"));
	}

	Context.DataSet.EndSimulate();

#if WITH_EDITORONLY_DATA
	if (SoloSystemInstance && SoloSystemInstance->ShouldCaptureThisFrame())
	{
		TSharedPtr<struct FNiagaraScriptDebuggerInfo, ESPMode::ThreadSafe> DebugInfo = SoloSystemInstance->GetActiveCaptureWrite(NAME_None, ENiagaraScriptUsage::SystemSpawnScript, FGuid());
		if (DebugInfo)
		{
			Context.DataSet.CopyTo(DebugInfo->Frame, OrigNum, SpawnNum);
			DebugInfo->Parameters = UpdateExecContext->Parameters;
			DebugInfo->bWritten = true;
		}
	}
#endif

	check(Context.DataSet.GetCurrentDataChecked().GetNumInstances() == Context.Instances.Num());
}

void FNiagaraSystemSimulation::UpdateSystemInstances(FNiagaraSystemSimulationTickContext& Context)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSystemSim_UpdateCNC);

	const int32 NumInstances = Context.Instances.Num();
	const int32 OrigNum = Context.Instances.Num() - Context.SpawnNum;
	const int32 SpawnNum = Context.SpawnNum;

	if (NumInstances > 0)
	{
		FNiagaraSystemInstance* SoloSystemInstance = bIsSolo && Context.Instances.Num() == 1 ? Context.Instances[0] : nullptr;

		FNiagaraDataBuffer& DestinationData = Context.DataSet.BeginSimulate();
		DestinationData.Allocate(NumInstances);
		DestinationData.SetNumInstances(NumInstances);

		// Tick UpdateExecContext, this can fail to bind VM functions if this happens we become invalid so mark all instances as disabled
		if (UpdateExecContext->Tick(Context.Instances[0], ENiagaraSimTarget::CPUSim) == false)
		{
			for (FNiagaraSystemInstance* SystemInst : Context.Instances)
			{
				SystemInst->SetActualExecutionState(ENiagaraExecutionState::Disabled);
			}
			Context.DataSet.EndSimulate();
			return;
		}

		// Run update.
		if (OrigNum > 0)
		{
			UpdateExecContext->BindSystemInstances(Context.Instances);
			UpdateExecContext->BindData(0, Context.DataSet, 0, false);
			UpdateExecContext->BindData(1, UpdateInstanceParameterDataSet, 0, false);

			FScriptExecutionConstantBufferTable UpdateConstantBufferTable;
			BuildConstantBufferTable(Context.Instances[0]->GetGlobalParameters(), UpdateExecContext, UpdateConstantBufferTable);

			UpdateExecContext->Execute(OrigNum, UpdateConstantBufferTable);
		}

		if (GbDumpSystemData || Context.System->bDumpDebugSystemInfo)
		{
			UE_LOG(LogNiagara, Log, TEXT("=== Updated %d Systems ==="), NumInstances);
			DestinationData.Dump(0, NumInstances, TEXT("System Data - Post Update"));
			UpdateInstanceParameterDataSet.GetCurrentDataChecked().Dump(0, NumInstances, TEXT("Update Instance Paramter Data"));
		}

		//Also run the update script on the newly spawned systems too.
		//TODO: JIRA - UE-60096 - Remove.
		//Ideally this should be compiled directly into the script similarly to interpolated particle spawning.
		if ( (SpawnNum > 0) && GbSystemUpdateOnSpawn)
		{
			UpdateExecContext->BindSystemInstances(Context.Instances);
			UpdateExecContext->BindData(0, Context.DataSet, OrigNum, false);
			UpdateExecContext->BindData(1, UpdateInstanceParameterDataSet, OrigNum, false);

			FNiagaraGlobalParameters UpdateOnSpawnParameters(Context.Instances[0]->GetGlobalParameters());
			UpdateOnSpawnParameters.EngineDeltaTime = 0.0001f;
			UpdateOnSpawnParameters.EngineInvDeltaTime = 10000.0f;

			FScriptExecutionConstantBufferTable UpdateConstantBufferTable;
			BuildConstantBufferTable(UpdateOnSpawnParameters, UpdateExecContext, UpdateConstantBufferTable);

			UpdateExecContext->Execute(SpawnNum, UpdateConstantBufferTable);

			if (GbDumpSystemData || Context.System->bDumpDebugSystemInfo)
			{
				UE_LOG(LogNiagara, Log, TEXT("=== Spawn Updated %d Systems ==="), SpawnNum);
				DestinationData.Dump(OrigNum, SpawnNum, TEXT("System Data - Post Update (new systems)"));
				UpdateInstanceParameterDataSet.GetCurrentDataChecked().Dump(OrigNum, SpawnNum, TEXT("Update Instance Paramter Data (new systems)"));
			}
		}

		Context.DataSet.EndSimulate();

#if WITH_EDITORONLY_DATA
		if (SoloSystemInstance && SoloSystemInstance->ShouldCaptureThisFrame())
		{
			TSharedPtr<struct FNiagaraScriptDebuggerInfo, ESPMode::ThreadSafe> DebugInfo = SoloSystemInstance->GetActiveCaptureWrite(NAME_None, ENiagaraScriptUsage::SystemUpdateScript, FGuid());
			if (DebugInfo)
			{
				Context.DataSet.CopyTo(DebugInfo->Frame);
				DebugInfo->Parameters = UpdateExecContext->Parameters;
				DebugInfo->bWritten = true;
			}
		}
#endif
	}

	check(Context.DataSet.GetCurrentDataChecked().GetNumInstances() == Context.Instances.Num());
}

void FNiagaraSystemSimulation::TransferSystemSimResults(FNiagaraSystemSimulationTickContext& Context)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSystemSim_TransferResultsCNC);

	if (Context.Instances.Num() == 0)
	{
		return;
	}


	UNiagaraSystem* System = GetSystem();
	check(System != nullptr);
#if STATS
	System->GetStatData().AddStatCapture(TTuple<uint64, ENiagaraScriptUsage>((uint64)this, ENiagaraScriptUsage::SystemSpawnScript), GetSpawnExecutionContext()->ReportStats());
	System->GetStatData().AddStatCapture(TTuple<uint64, ENiagaraScriptUsage>((uint64)this, ENiagaraScriptUsage::SystemUpdateScript), GetUpdateExecutionContext()->ReportStats());
#endif

	FNiagaraDataSetReaderInt32<ENiagaraExecutionState> SystemExecutionStateAccessor = System->GetSystemExecutionStateAccessor().GetReader(Context.DataSet);
	TConstArrayView<FNiagaraDataSetAccessor<ENiagaraExecutionState>> EmitterExecutionStateAccessors = System->GetEmitterExecutionStateAccessors();

	for (int32 SystemIndex = 0; SystemIndex < Context.Instances.Num(); ++SystemIndex)
	{
		FNiagaraSystemInstance* SystemInst = Context.Instances[SystemIndex];

		//Apply the systems requested execution state to it's actual execution state.
		ENiagaraExecutionState ExecutionState = SystemExecutionStateAccessor.GetSafe(SystemIndex, ENiagaraExecutionState::Disabled);
		SystemInst->SetActualExecutionState(ExecutionState);

		if (!SystemInst->IsDisabled())
		{
			//Now pull data out of the simulation and drive the emitters with it.
			TArray<TSharedRef<FNiagaraEmitterInstance, ESPMode::ThreadSafe>>& Emitters = SystemInst->GetEmitters();
			for (int32 EmitterIdx = 0; EmitterIdx < Emitters.Num(); ++EmitterIdx)
			{
				FNiagaraEmitterInstance& EmitterInst = Emitters[EmitterIdx].Get();

				//Early exit before we set the state as if we're complete or disabled we should never let the emitter turn itself back. It needs to be reset/reinited manually.
				if (EmitterInst.IsComplete())
				{
					continue;
				}

				check(Emitters.Num() > EmitterIdx);

				ENiagaraExecutionState State = EmitterExecutionStateAccessors[EmitterIdx].GetReader(Context.DataSet).GetSafe(SystemIndex, ENiagaraExecutionState::Disabled);
				EmitterInst.SetExecutionState(State);

				TConstArrayView<FNiagaraDataSetAccessor<FNiagaraSpawnInfo>> EmitterSpawnInfoAccessors = System->GetEmitterSpawnInfoAccessors(EmitterIdx);
				TArray<FNiagaraSpawnInfo>& EmitterInstSpawnInfos = EmitterInst.GetSpawnInfo();
				for (int32 SpawnInfoIdx = 0; SpawnInfoIdx < EmitterSpawnInfoAccessors.Num(); ++SpawnInfoIdx)
				{
					if (SpawnInfoIdx < EmitterInstSpawnInfos.Num())
					{
						EmitterInstSpawnInfos[SpawnInfoIdx] = EmitterSpawnInfoAccessors[SpawnInfoIdx].GetReader(Context.DataSet).Get(SystemIndex);
					}
					else
					{
						ensure(SpawnInfoIdx < EmitterInstSpawnInfos.Num());
					}
				}

				//TODO: Any other fixed function stuff like this?

				FNiagaraScriptExecutionContext& SpawnContext = EmitterInst.GetSpawnExecutionContext();
				DataSetToEmitterSpawnParameters[EmitterIdx].DataSetToParameterStore(SpawnContext.Parameters, Context.DataSet, SystemIndex);

				FNiagaraScriptExecutionContext& UpdateContext = EmitterInst.GetUpdateExecutionContext();
				DataSetToEmitterUpdateParameters[EmitterIdx].DataSetToParameterStore(UpdateContext.Parameters, Context.DataSet, SystemIndex);

				FNiagaraComputeExecutionContext* GPUContext = EmitterInst.GetGPUContext();
				if (GPUContext)
				{
					DataSetToEmitterGPUParameters[EmitterIdx].DataSetToParameterStore(GPUContext->CombinedParamStore, Context.DataSet, SystemIndex);
				}

				TArrayView<FNiagaraScriptExecutionContext> EventContexts = EmitterInst.GetEventExecutionContexts();
				for (int32 EventIdx = 0; EventIdx < EventContexts.Num(); ++EventIdx)
				{
					FNiagaraScriptExecutionContext& EventContext = EventContexts[EventIdx];
					if (DataSetToEmitterEventParameters[EmitterIdx].Num() > EventIdx)
					{
						DataSetToEmitterEventParameters[EmitterIdx][EventIdx].DataSetToParameterStore(EventContext.Parameters, Context.DataSet, SystemIndex);
					}
					else
					{
						UE_LOG(LogNiagara, Log, TEXT("Skipping DataSetToEmitterEventParameters because EventIdx is out-of-bounds. %d of %d"), EventIdx, DataSetToEmitterEventParameters[EmitterIdx].Num());
					}
				}

				DataSetToEmitterRendererParameters[EmitterIdx].DataSetToParameterStore(EmitterInst.GetRendererBoundVariables(), Context.DataSet, SystemIndex);

			}
		}
	}
}

void FNiagaraSystemSimulation::RemoveInstance(FNiagaraSystemInstance* Instance)
{
	if (Instance->SystemInstanceIndex == INDEX_NONE)
	{
		return;
	}

	check(IsInGameThread());
	if (EffectType)
	{
		--EffectType->NumInstances;
	}

	// Remove from pending promotions list
	PendingTickGroupPromotions.RemoveSingleSwap(Instance);

	UNiagaraSystem* System = WeakSystem.Get();

	if(System)
	{
		System->UnregisterActiveInstance();
	}

	if (Instance->IsPendingSpawn())
	{
		if (GbDumpSystemData || (System && System->bDumpDebugSystemInfo))
		{
			UE_LOG(LogNiagara, Log, TEXT("=== Removing Pending Spawn %d ==="), Instance->SystemInstanceIndex);
			MainDataSet.GetCurrentDataChecked().Dump(Instance->SystemInstanceIndex, 1, TEXT("System data being removed."));
		}

		// If we are in the spawn phase we may be running concurrently so handle this by waiting for the system tick to complete
		if (bInSpawnPhase)
		{
			WaitForSystemTickComplete();
			Instance->WaitForAsyncTickDoNotFinalize();

			check(SystemInstances.Num() == MainDataSet.GetCurrentDataChecked().GetNumInstances());
			check(PausedSystemInstances.Num() == PausedInstanceData.GetCurrentDataChecked().GetNumInstances());

			int32 SystemIndex = Instance->SystemInstanceIndex;
			if (SystemIndex != INDEX_NONE)
			{
				check(SpawningInstances.IsValidIndex(SystemIndex));
				check(Instance == SpawningInstances[SystemIndex]);

				SpawningDataSet.GetCurrentDataChecked().KillInstance(Instance->SystemInstanceIndex);

				SpawningInstances.RemoveAtSwap(SystemIndex);
				Instance->SystemInstanceIndex = INDEX_NONE;
				Instance->SetPendingSpawn(false);
				if (SpawningInstances.IsValidIndex(SystemIndex))
				{
					SpawningInstances[SystemIndex]->SystemInstanceIndex = SystemIndex;
				}
			}
		}
		else
		{
			int32 SystemIndex = Instance->SystemInstanceIndex;
			check(PendingSystemInstances.IsValidIndex(SystemIndex));
			check(Instance == PendingSystemInstances[SystemIndex]);

			PendingSystemInstances.RemoveAtSwap(SystemIndex);
			Instance->SystemInstanceIndex = INDEX_NONE;
			Instance->SetPendingSpawn(false);
			if (PendingSystemInstances.IsValidIndex(SystemIndex))
			{
				PendingSystemInstances[SystemIndex]->SystemInstanceIndex = SystemIndex;
			}
		}
	}
	else if (Instance->IsPaused())
	{
		if (GbDumpSystemData || (System && System->bDumpDebugSystemInfo))
		{
			UE_LOG(LogNiagara, Log, TEXT("=== Removing Paused %d ==="), Instance->SystemInstanceIndex);
			MainDataSet.GetCurrentDataChecked().Dump(Instance->SystemInstanceIndex, 1, TEXT("System data being removed."));
		}

		const int32 NumInstances = PausedInstanceData.GetCurrentDataChecked().GetNumInstances();
		check(PausedSystemInstances.Num() == NumInstances);

		const int32 SystemIndex = Instance->SystemInstanceIndex;
		check(PausedSystemInstances.IsValidIndex(SystemIndex));
		check(Instance == PausedSystemInstances[SystemIndex]);

		PausedInstanceData.GetCurrentDataChecked().KillInstance(SystemIndex);
		PausedSystemInstances.RemoveAtSwap(SystemIndex);
		Instance->SystemInstanceIndex = INDEX_NONE;
		if (PausedSystemInstances.IsValidIndex(SystemIndex))
		{
			PausedSystemInstances[SystemIndex]->SystemInstanceIndex = SystemIndex;
		}

		check(SystemInstances.Num() == MainDataSet.GetCurrentDataChecked().GetNumInstances());
		check(PausedSystemInstances.Num() == PausedInstanceData.GetCurrentDataChecked().GetNumInstances());
	}
	else if (SystemInstances.IsValidIndex(Instance->SystemInstanceIndex))
	{
		if (GbDumpSystemData || (System && System->bDumpDebugSystemInfo))
		{
			UE_LOG(LogNiagara, Log, TEXT("=== Removing System %d ==="), Instance->SystemInstanceIndex);
			MainDataSet.GetCurrentDataChecked().Dump(Instance->SystemInstanceIndex, 1, TEXT("System data being removed."));
		}

		// Wait for the system simulation & the system instances tick to complete as we are touching both the SystemInstances & DataSet
		// Note: We do not need to wait for all instances to complete as the system simulation concurrent tick will have transfered data from the DataSet out to ParameterStores
		WaitForSystemTickComplete();
		Instance->WaitForAsyncTickDoNotFinalize();

		check(SystemInstances.Num() == MainDataSet.GetCurrentDataChecked().GetNumInstances());
		check(PausedSystemInstances.Num() == PausedInstanceData.GetCurrentDataChecked().GetNumInstances());

		// There is a slim window where the finalize will have executed so we must ensure we have not been removed.
		// This can happen where the async task is not complete, we start to wait and it posts the finalize task.  The TG will drain the GT queue which contains the finalize and we have been removed (via completion)
		if ( Instance->SystemInstanceIndex != INDEX_NONE )
		{
			const int32 NumInstances = MainDataSet.GetCurrentDataChecked().GetNumInstances();
			check(SystemInstances.Num() == NumInstances);

			const int32 SystemIndex = Instance->SystemInstanceIndex;
			check(Instance == SystemInstances[SystemIndex]);
			check(SystemInstances.IsValidIndex(SystemIndex));

			MainDataSet.GetCurrentDataChecked().KillInstance(SystemIndex);
			SystemInstances.RemoveAtSwap(SystemIndex);
			Instance->SystemInstanceIndex = INDEX_NONE;
			if (SystemInstances.IsValidIndex(SystemIndex))
			{
				SystemInstances[SystemIndex]->SystemInstanceIndex = SystemIndex;
			}

			check(SystemInstances.Num() == MainDataSet.GetCurrentDataChecked().GetNumInstances());
			check(PausedSystemInstances.Num() == PausedInstanceData.GetCurrentDataChecked().GetNumInstances());
		}
	}

#if NIAGARA_NAN_CHECKING
	MainDataSet.CheckForNaNs();
#endif
}

void FNiagaraSystemSimulation::AddInstance(FNiagaraSystemInstance* Instance)
{
	check(IsInGameThread());
	check(Instance->SystemInstanceIndex == INDEX_NONE);

	WaitForSystemTickComplete();

	Instance->SetPendingSpawn(true);
	Instance->SystemInstanceIndex = AddPendingSystemInstance(Instance);

	UNiagaraSystem* System = WeakSystem.Get();
	if (GbDumpSystemData || (System && System->bDumpDebugSystemInfo))
	{
		UE_LOG(LogNiagara, Log, TEXT("=== Adding To Pending Spawn %d ==="), Instance->SystemInstanceIndex);
		//MainDataSet.Dump(true, Instance->SystemInstanceIndex, 1);
	}
	
	if(System)
	{
		System->RegisterActiveInstance();
	}

	if (EffectType)
	{
		++EffectType->NumInstances;
		EffectType->bNewSystemsSinceLastScalabilityUpdate = true;
	}

	check(SystemInstances.Num() == MainDataSet.GetCurrentDataChecked().GetNumInstances());
	check(PausedSystemInstances.Num() == PausedInstanceData.GetCurrentDataChecked().GetNumInstances());
}

void FNiagaraSystemSimulation::PauseInstance(FNiagaraSystemInstance* Instance)
{
	check(IsInGameThread());
	WaitForInstancesTickComplete();

	check(!Instance->IsPaused());
	check(!MainDataSet.GetDestinationData());
	check(!PausedInstanceData.GetDestinationData());

	check(SystemInstances.Num() == MainDataSet.GetCurrentDataChecked().GetNumInstances());
	check(PausedSystemInstances.Num() == PausedInstanceData.GetCurrentDataChecked().GetNumInstances());

	UNiagaraSystem* System = WeakSystem.Get();
	if (Instance->IsPendingSpawn())
	{
		if (GbDumpSystemData || (System && System->bDumpDebugSystemInfo))
		{
			UE_LOG(LogNiagara, Log, TEXT("=== Pausing Pending Spawn %d ==="), Instance->SystemInstanceIndex);
			//MainDataSet.Dump(true, Instance->SystemInstanceIndex, 1);
		}
		//Nothing to do for pending spawn systems.
		check(PendingSystemInstances[Instance->SystemInstanceIndex] == Instance);
		return;
	}

	if (GbDumpSystemData || (System && System->bDumpDebugSystemInfo))
	{
		UE_LOG(LogNiagara, Log, TEXT("=== Pausing System %d ==="), Instance->SystemInstanceIndex);
		MainDataSet.GetCurrentDataChecked().Dump(Instance->SystemInstanceIndex, 1, TEXT("System data being paused."));
	}

	int32 SystemIndex = Instance->SystemInstanceIndex;
	check(SystemInstances.IsValidIndex(SystemIndex));
	check(Instance == SystemInstances[SystemIndex]);

	int32 NewDataSetIndex = PausedInstanceData.GetCurrentDataChecked().TransferInstance(MainDataSet.GetCurrentDataChecked(), SystemIndex);

	Instance->SystemInstanceIndex = PausedSystemInstances.Add(Instance);

	check(NewDataSetIndex == Instance->SystemInstanceIndex);

	SystemInstances.RemoveAtSwap(SystemIndex);
	if (SystemInstances.IsValidIndex(SystemIndex))
	{
		SystemInstances[SystemIndex]->SystemInstanceIndex = SystemIndex;
	}

	check(SystemInstances.Num() == MainDataSet.GetCurrentDataChecked().GetNumInstances());
	check(PausedSystemInstances.Num() == PausedInstanceData.GetCurrentDataChecked().GetNumInstances());
}

void FNiagaraSystemSimulation::UnpauseInstance(FNiagaraSystemInstance* Instance)
{
	check(IsInGameThread());
	WaitForInstancesTickComplete();

	check(Instance->IsPaused());
	check(!MainDataSet.GetDestinationData());
	check(!PausedInstanceData.GetDestinationData());

	check(SystemInstances.Num() == MainDataSet.GetCurrentDataChecked().GetNumInstances());
	check(PausedSystemInstances.Num() == PausedInstanceData.GetCurrentDataChecked().GetNumInstances());

	UNiagaraSystem* System = WeakSystem.Get();
	if (Instance->IsPendingSpawn())
	{
		if (GbDumpSystemData || (System && System->bDumpDebugSystemInfo))
		{
			UE_LOG(LogNiagara, Log, TEXT("=== Unpausing Pending Spawn %d ==="), Instance->SystemInstanceIndex);
			//MainDataSet.Dump(true, Instance->SystemInstanceIndex, 1);
		}
		//Nothing to do for pending spawn systems.
		check(PendingSystemInstances[Instance->SystemInstanceIndex] == Instance);
		return;
	}

	if (GbDumpSystemData || (System && System->bDumpDebugSystemInfo))
	{
		UE_LOG(LogNiagara, Log, TEXT("=== Unpausing System %d ==="), Instance->SystemInstanceIndex);
		MainDataSet.GetCurrentDataChecked().Dump(Instance->SystemInstanceIndex, 1, TEXT("System data being unpaused."));
	}

	int32 SystemIndex = Instance->SystemInstanceIndex;
	check(PausedSystemInstances.IsValidIndex(SystemIndex));
	check(Instance == PausedSystemInstances[SystemIndex]);

	int32 NewDataSetIndex = MainDataSet.GetCurrentDataChecked().TransferInstance(PausedInstanceData.GetCurrentDataChecked(), SystemIndex);

	Instance->SystemInstanceIndex = SystemInstances.Add(Instance);
	check(NewDataSetIndex == Instance->SystemInstanceIndex);

	PausedSystemInstances.RemoveAtSwap(SystemIndex);
	if (PausedSystemInstances.IsValidIndex(SystemIndex))
	{
		PausedSystemInstances[SystemIndex]->SystemInstanceIndex = SystemIndex;
	}

	check(SystemInstances.Num() == MainDataSet.GetCurrentDataChecked().GetNumInstances());
	check(PausedSystemInstances.Num() == PausedInstanceData.GetCurrentDataChecked().GetNumInstances());
}

void FNiagaraSystemSimulation::InitParameterDataSetBindings(FNiagaraSystemInstance* SystemInst)
{
	//Have to init here as we need an actual parameter store to pull the layout info from.
	//TODO: Pull the layout stuff out of each data set and store. So much duplicated data.
	//This assumes that all layouts for all emitters is the same. Which it should be.
	//Ideally we can store all this layout info in the systm/emitter assets so we can just generate this in Init()
	if (!bBindingsInitialized && SystemInst != nullptr)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_Niagara_InitParameterDataSetBindings);

		bBindingsInitialized = true;

		SpawnInstanceParameterToDataSetBinding.Init(SpawnInstanceParameterDataSet, SystemInst->GetInstanceParameters());
		UpdateInstanceParameterToDataSetBinding.Init(UpdateInstanceParameterDataSet, SystemInst->GetInstanceParameters());

		TArray<TSharedRef<FNiagaraEmitterInstance, ESPMode::ThreadSafe>>& Emitters = SystemInst->GetEmitters();
		const int32 EmitterCount = Emitters.Num();

		DataSetToEmitterSpawnParameters.SetNum(EmitterCount);
		DataSetToEmitterUpdateParameters.SetNum(EmitterCount);
		DataSetToEmitterEventParameters.SetNum(EmitterCount);
		DataSetToEmitterGPUParameters.SetNum(EmitterCount);
		DataSetToEmitterRendererParameters.SetNum(EmitterCount);

		for (int32 EmitterIdx = 0; EmitterIdx < EmitterCount; ++EmitterIdx)
		{
			FNiagaraEmitterInstance& EmitterInst = Emitters[EmitterIdx].Get();
			if (EmitterInst.IsDisabled())
			{
				continue;
			}

			FNiagaraScriptExecutionContext& SpawnContext = EmitterInst.GetSpawnExecutionContext();
			DataSetToEmitterSpawnParameters[EmitterIdx].Init(MainDataSet, SpawnContext.Parameters);

			FNiagaraScriptExecutionContext& UpdateContext = EmitterInst.GetUpdateExecutionContext();
			DataSetToEmitterUpdateParameters[EmitterIdx].Init(MainDataSet, UpdateContext.Parameters);

			FNiagaraComputeExecutionContext* GPUContext = EmitterInst.GetGPUContext();
			if (GPUContext)
			{
				DataSetToEmitterGPUParameters[EmitterIdx].Init(MainDataSet, GPUContext->CombinedParamStore);
			}

			DataSetToEmitterRendererParameters[EmitterIdx].Init(MainDataSet, EmitterInst.GetRendererBoundVariables());

			TArrayView<FNiagaraScriptExecutionContext> EventContexts = EmitterInst.GetEventExecutionContexts();
			const int32 EventCount = EventContexts.Num();
			DataSetToEmitterEventParameters[EmitterIdx].SetNum(EventCount);

			for (int32 EventIdx = 0; EventIdx < EventCount; ++EventIdx)
			{
				FNiagaraScriptExecutionContext& EventContext = EventContexts[EventIdx];
				DataSetToEmitterEventParameters[EmitterIdx][EventIdx].Init(MainDataSet, EventContext.Parameters);
			}
		}
	}
}

const FString& FNiagaraSystemSimulation::GetCrashReporterTag()const
{
	if (CrashReporterTag.IsEmpty())
	{
		UNiagaraSystem* Sys = GetSystem();
		const FString& AssetName = Sys ? Sys->GetFullName() : TEXT("nullptr");

		CrashReporterTag = FString::Printf(TEXT("SystemSimulation | System: %s | bSolo: %s |"), *AssetName, bIsSolo ? TEXT("true") : TEXT("false"));
	}
	return CrashReporterTag;
}

void FNiagaraConstantBufferToDataSetBinding::CopyToDataSets(
	const FNiagaraSystemCompiledData& CompiledData,
	const FNiagaraSystemInstance& SystemInstance,
	FNiagaraDataSet& SpawnDataSet,
	FNiagaraDataSet& UpdateDataSet,
	int32 DataSestInstanceIndex)
{
	{
		const uint8* GlobalParameters = reinterpret_cast<const uint8*>(&SystemInstance.GetGlobalParameters());
		ApplyOffsets(CompiledData.SpawnInstanceGlobalBinding, GlobalParameters, SpawnDataSet, DataSestInstanceIndex);
		ApplyOffsets(CompiledData.UpdateInstanceGlobalBinding, GlobalParameters, UpdateDataSet, DataSestInstanceIndex);
	}

	{
		const uint8* SystemParameters = reinterpret_cast<const uint8*>(&SystemInstance.GetSystemParameters());
		ApplyOffsets(CompiledData.SpawnInstanceSystemBinding, SystemParameters, SpawnDataSet, DataSestInstanceIndex);
		ApplyOffsets(CompiledData.UpdateInstanceSystemBinding, SystemParameters, UpdateDataSet, DataSestInstanceIndex);
	}

	{
		const uint8* OwnerParameters = reinterpret_cast<const uint8*>(&SystemInstance.GetOwnerParameters());
		ApplyOffsets(CompiledData.SpawnInstanceOwnerBinding, OwnerParameters, SpawnDataSet, DataSestInstanceIndex);
		ApplyOffsets(CompiledData.UpdateInstanceOwnerBinding, OwnerParameters, UpdateDataSet, DataSestInstanceIndex);
	}

	const auto& Emitters = SystemInstance.GetEmitters();
	const int32 EmitterCount = Emitters.Num();

	for (int32 EmitterIdx = 0; EmitterIdx < EmitterCount; ++EmitterIdx)
	{
		const uint8* EmitterParameters = reinterpret_cast<const uint8*>(&SystemInstance.GetEmitterParameters(EmitterIdx));
		ApplyOffsets(CompiledData.SpawnInstanceEmitterBindings[EmitterIdx], EmitterParameters, SpawnDataSet, DataSestInstanceIndex);
		ApplyOffsets(CompiledData.UpdateInstanceEmitterBindings[EmitterIdx], EmitterParameters, UpdateDataSet, DataSestInstanceIndex);
	}
}

void FNiagaraConstantBufferToDataSetBinding::ApplyOffsets(
	const FNiagaraParameterDataSetBindingCollection& Offsets,
	const uint8* SourceData,
	FNiagaraDataSet& DataSet,
	int32 DataSetInstanceIndex)
{
	FNiagaraDataBuffer& CurrBuffer = DataSet.GetDestinationDataChecked();

	for (const auto& DataOffsets : Offsets.FloatOffsets)
	{
		float* ParamPtr = (float*)(SourceData + DataOffsets.ParameterOffset);
		float* DataSetPtr = CurrBuffer.GetInstancePtrFloat(DataOffsets.DataSetComponentOffset, DataSetInstanceIndex);
		*DataSetPtr = *ParamPtr;
	}
	for (const auto& DataOffsets : Offsets.Int32Offsets)
	{
		int32* ParamPtr = (int32*)(SourceData + DataOffsets.ParameterOffset);
		int32* DataSetPtr = CurrBuffer.GetInstancePtrInt32(DataOffsets.DataSetComponentOffset, DataSetInstanceIndex);
		*DataSetPtr = *ParamPtr;
	}
}


void FNiagaraSystemSimulation::BuildConstantBufferTable(
	const FNiagaraGlobalParameters& GlobalParameters,
	TUniquePtr<FNiagaraScriptExecutionContextBase>& ExecContext,
	FScriptExecutionConstantBufferTable& ConstantBufferTable) const
{
	const auto ScriptLiterals = ExecContext->GetScriptLiterals();

	check(!ExecContext->HasInterpolationParameters);

	const auto& ExternalParameterData = ExecContext->Parameters.GetParameterDataArray();
	uint8* ExternalParameterBuffer = const_cast<uint8*>(ExternalParameterData.GetData());
	const uint32 ExternalParameterSize = ExecContext->Parameters.GetExternalParameterSize();

	ConstantBufferTable.Reset(3);
	ConstantBufferTable.AddTypedBuffer(GlobalParameters);
	ConstantBufferTable.AddRawBuffer(ExternalParameterBuffer, ExternalParameterSize);
	ConstantBufferTable.AddRawBuffer(ScriptLiterals.GetData(), ScriptLiterals.Num());
}

ENiagaraGPUTickHandlingMode FNiagaraSystemSimulation::GetGPUTickHandlingMode()const
{
	const UNiagaraSystem* System = GetSystem();
	if (Batcher && FNiagaraUtilities::AllowGPUParticles(Batcher->GetShaderPlatform()) && System && System->HasAnyGPUEmitters())
	{
		//TODO: Maybe some DI post ticks can even be done concurrent too which would also remove this restriction.
		bool bGT = System->HasDIsWithPostSimulateTick() || GNiagaraConcurrentGPUTickInit == 0;
		bool bBatched = GNiagaraBatchGPUTickSubmit && !bIsSolo;

		if (bGT)
		{
			return bBatched ? ENiagaraGPUTickHandlingMode::GameThreadBatched : ENiagaraGPUTickHandlingMode::GameThread;
		}
		else
		{
			return bBatched ? ENiagaraGPUTickHandlingMode::ConcurrentBatched : ENiagaraGPUTickHandlingMode::Concurrent;
		}
	}

	return ENiagaraGPUTickHandlingMode::None;
}

static int32 GbNiagaraUseLegacySystemSimContexts = 0;
static FAutoConsoleVariableRef CVarNiagaraUseLevgacySystemSimContexts(
	TEXT("fx.Niagara.UseLegacySystemSimContexts"),
	GbNiagaraUseLegacySystemSimContexts,
	TEXT("If > 0, Niagara will use legacy system simulation contexts which would force the whole simulation solo if there were per instance DI calls in the system scripts. \n"),
	FConsoleVariableDelegate::CreateStatic(&FNiagaraSystemSimulation::OnChanged_UseLegacySystemSimulationContexts),
	ECVF_Default
);

bool FNiagaraSystemSimulation::bUseLegacyExecContexts = GbNiagaraUseLegacySystemSimContexts != 0;
bool FNiagaraSystemSimulation::UseLegacySystemSimulationContexts()
{
	return bUseLegacyExecContexts;
}

void FNiagaraSystemSimulation::OnChanged_UseLegacySystemSimulationContexts(IConsoleVariable* CVar)
{
	bool bNewValue = GbNiagaraUseLegacySystemSimContexts != 0;
	if( bUseLegacyExecContexts != bNewValue)
	{
		//To change at runtime we have to reinit all systems so they have the correct per instance DI bindings.
		FNiagaraSystemUpdateContext UpdateContext;
		UpdateContext.SetDestroyOnAdd(true);
		UpdateContext.SetOnlyActive(true);
		UpdateContext.AddAll(true);

		//Just to be sure there's no lingering state, clear out the pools.
		//TODO: Moveinto the update context itself?
		FNiagaraWorldManager::ForAllWorldManagers(
			[](FNiagaraWorldManager& WorldMan)
			{
				WorldMan.GetComponentPool()->Cleanup(nullptr);
			}
		);

		//Reactivate any FX that were active.
		bUseLegacyExecContexts = bNewValue;
		UpdateContext.CommitUpdate();

		//Re-prime the pools.
		FNiagaraWorldManager::ForAllWorldManagers(
			[](FNiagaraWorldManager& WorldMan)
			{
				WorldMan.PrimePoolForAllSystems();
			}
		);
	}
}
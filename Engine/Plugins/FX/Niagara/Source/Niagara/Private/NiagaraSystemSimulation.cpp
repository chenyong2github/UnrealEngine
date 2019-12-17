// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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
DECLARE_CYCLE_STAT(TEXT("System Sim FastPath [CNC]"), STAT_NiagaraSystemSim_FastPathCNC, STATGROUP_Niagara);

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

//////////////////////////////////////////////////////////////////////////

FNiagaraSystemSimulationTickContext::FNiagaraSystemSimulationTickContext(FNiagaraSystemSimulation* InOwner, TArray<FNiagaraSystemInstance*>& InInstances, FNiagaraDataSet& InDataSet, float InDeltaSeconds, int32 InSpawnNum, int32 InEffectsQuality, const FGraphEventRef& InMyCompletionGraphEvent)
	: Owner(InOwner)
	, System(InOwner->GetSystem())
	, Instances(InInstances)
	, DataSet(InDataSet)
	, DeltaSeconds(InDeltaSeconds)
	, SpawnNum(InSpawnNum)
	, EffectsQuality(InEffectsQuality)
	, MyCompletionGraphEvent(InMyCompletionGraphEvent)
	, FinalizeEvents(nullptr)
	, bTickAsync(GbParallelSystemSimTick && FApp::ShouldUseThreadingForPerformance() && InMyCompletionGraphEvent.IsValid())
	, bTickInstancesAsync(GbParallelSystemInstanceTick && FApp::ShouldUseThreadingForPerformance() && MyCompletionGraphEvent.IsValid() && !InOwner->GetIsSolo())
{
}

//////////////////////////////////////////////////////////////////////////

FAutoConsoleTaskPriority CPrio_NiagaraSystemSimulationTickTask(
	TEXT("TaskGraph.TaskPriorities.NiagaraSystemSimulationTickcTask"),
	TEXT("Task and thread priority for FNiagaraSystemSimulationTickTask."),
	ENamedThreads::HighThreadPriority, // if we have high priority task threads, then use them...
	ENamedThreads::NormalTaskPriority, // .. at normal task priority
	ENamedThreads::HighTaskPriority // if we don't have hi pri threads, then use normal priority threads at high task priority instead
);

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
	ENamedThreads::Type GetDesiredThread() { return CPrio_NiagaraSystemSimulationTickTask.Get(); }
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
	ENamedThreads::Type GetDesiredThread() { return CPrio_NiagaraSystemSimulationTickTask.Get(); }
	static ESubsequentsMode::Type GetSubsequentsMode() { return ESubsequentsMode::TrackSubsequents; }

	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		Context.MyCompletionGraphEvent = MyCompletionGraphEvent;
		Context.Owner->Tick_Concurrent(Context);
		Context.FinalizeEvents = nullptr;
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

		for (FNiagaraSystemInstance* Inst : Batch)
		{
			Inst->FinalizeTick_GameThread();
		}
	}
};

FAutoConsoleTaskPriority CPrio_NiagaraSystemInstanceAsyncTask(
	TEXT("TaskGraph.TaskPriorities.NiagaraSystemAsyncTask"),
	TEXT("Task and thread priority for FNiagaraSystemAsyncTask."),
	ENamedThreads::HighThreadPriority, // if we have high priority task threads, then use them...
	ENamedThreads::NormalTaskPriority, // .. at normal task priority
	ENamedThreads::HighTaskPriority // if we don't have hi pri threads, then use normal priority threads at high task priority instead
);

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
	ENamedThreads::Type GetDesiredThread() 	{ return CPrio_NiagaraSystemInstanceAsyncTask.Get(); }
	static ESubsequentsMode::Type GetSubsequentsMode() { return ESubsequentsMode::TrackSubsequents; }

	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		FNiagaraScopedRuntimeCycleCounter RuntimeScope(SystemSim->GetSystem(), true, true);
		for (FNiagaraSystemInstance* Inst : Batch)
		{
			Inst->Tick_Concurrent();
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
	, bHasEverTicked(false)
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
	bHasEverTicked = false;

	FNiagaraWorldManager* WorldMan = FNiagaraWorldManager::Get(InWorld);
	check(WorldMan);

	bCanExecute = System->GetSystemSpawnScript()->GetVMExecutableData().IsValid() && System->GetSystemUpdateScript()->GetVMExecutableData().IsValid();
	UEnum* EnumPtr = FNiagaraTypeDefinition::GetExecutionStateEnum();

	if (bCanExecute)
	{

		{
			SCOPE_CYCLE_COUNTER(STAT_NiagaraSystemSim_Init_DataSets);

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
			SCOPE_CYCLE_COUNTER(STAT_NiagaraSystemSim_Init_ExecContexts);



			SpawnExecContext.Init(SpawnScript, ENiagaraSimTarget::CPUSim);
			UpdateExecContext.Init(UpdateScript, ENiagaraSimTarget::CPUSim);
		}


		{
			SCOPE_CYCLE_COUNTER(STAT_NiagaraSystemSim_Init_BindParams);


			//Bind parameter collections.
			for (UNiagaraParameterCollection* Collection : SpawnScript->GetCachedParameterCollectionReferences())
			{
				GetParameterCollectionInstance(Collection)->GetParameterStore().Bind(&SpawnExecContext.Parameters);
			}
			for (UNiagaraParameterCollection* Collection : UpdateScript->GetCachedParameterCollectionReferences())
			{
				GetParameterCollectionInstance(Collection)->GetParameterStore().Bind(&UpdateExecContext.Parameters);
			}

			TArray<UNiagaraScript*, TInlineAllocator<2>> Scripts;
			Scripts.Add(SpawnScript);
			Scripts.Add(UpdateScript);
			FNiagaraUtilities::CollectScriptDataInterfaceParameters(*System, Scripts, ScriptDefinedDataInterfaceParameters);

			ScriptDefinedDataInterfaceParameters.Bind(&SpawnExecContext.Parameters);
			ScriptDefinedDataInterfaceParameters.Bind(&UpdateExecContext.Parameters);

			SpawnScript->RapidIterationParameters.Bind(&SpawnExecContext.Parameters);
			UpdateScript->RapidIterationParameters.Bind(&UpdateExecContext.Parameters);

			// If this simulation is not solo than we have bind the source system parameters to the system simulation contexts so that
			// the system and emitter scripts use the default shared data interfaces.
			if (!bIsSolo)
			{
				GetSystem()->GetExposedParameters().Bind(&GetSpawnExecutionContext().Parameters);
				GetSystem()->GetExposedParameters().Bind(&GetUpdateExecutionContext().Parameters);
			}
		}


		{
			SCOPE_CYCLE_COUNTER(STAT_NiagaraSystemSim_Init_DatasetAccessors);

			SystemExecutionStateAccessor.Create(&MainDataSet, FNiagaraVariable(EnumPtr, TEXT("System.ExecutionState")));
			EmitterSpawnInfoAccessors.Reset();
			EmitterExecutionStateAccessors.Reset();
			EmitterSpawnInfoAccessors.SetNum(System->GetNumEmitters());

			for (int32 EmitterIdx = 0; EmitterIdx < System->GetNumEmitters(); ++EmitterIdx)
			{
				FNiagaraEmitterHandle& EmitterHandle = System->GetEmitterHandle(EmitterIdx);
				UNiagaraEmitter* Emitter = EmitterHandle.GetInstance();
				if (Emitter)
				{
					FString EmitterName = Emitter->GetUniqueEmitterName();
					EmitterExecutionStateAccessors.Emplace(MainDataSet, FNiagaraVariable(EnumPtr, *(EmitterName + TEXT(".ExecutionState"))));
					const TArray<TSharedRef<const FNiagaraEmitterCompiledData>>& EmitterCompiledData = System->GetEmitterCompiledData();

					check(EmitterCompiledData.Num() == System->GetNumEmitters());
					for (FName AttrName : EmitterCompiledData[EmitterIdx]->SpawnAttributes)
					{
						EmitterSpawnInfoAccessors[EmitterIdx].Emplace(MainDataSet, FNiagaraVariable(FNiagaraTypeDefinition(FNiagaraSpawnInfo::StaticStruct()), AttrName));
					}

					if (Emitter->bLimitDeltaTime)
					{
						MaxDeltaTime = MaxDeltaTime.IsSet() ? FMath::Min(MaxDeltaTime.GetValue(), Emitter->MaxDeltaTimePerTick) : Emitter->MaxDeltaTimePerTick;
					}
				}
				else
				{
					EmitterExecutionStateAccessors.Emplace();
				}
			}
		}


		{
			SCOPE_CYCLE_COUNTER(STAT_NiagaraSystemSim_Init_DirectBindings);

			SpawnTimeParam.Init(SpawnExecContext.Parameters, SYS_PARAM_ENGINE_TIME);
			UpdateTimeParam.Init(UpdateExecContext.Parameters, SYS_PARAM_ENGINE_TIME);
			SpawnDeltaTimeParam.Init(SpawnExecContext.Parameters, SYS_PARAM_ENGINE_DELTA_TIME);
			UpdateDeltaTimeParam.Init(UpdateExecContext.Parameters, SYS_PARAM_ENGINE_DELTA_TIME);
			SpawnInvDeltaTimeParam.Init(SpawnExecContext.Parameters, SYS_PARAM_ENGINE_INV_DELTA_TIME);
			UpdateInvDeltaTimeParam.Init(UpdateExecContext.Parameters, SYS_PARAM_ENGINE_INV_DELTA_TIME);
			SpawnNumSystemInstancesParam.Init(SpawnExecContext.Parameters, SYS_PARAM_ENGINE_NUM_SYSTEM_INSTANCES);
			UpdateNumSystemInstancesParam.Init(UpdateExecContext.Parameters, SYS_PARAM_ENGINE_NUM_SYSTEM_INSTANCES);
			SpawnGlobalSpawnCountScaleParam.Init(SpawnExecContext.Parameters, SYS_PARAM_ENGINE_GLOBAL_SPAWN_COUNT_SCALE);
			UpdateGlobalSpawnCountScaleParam.Init(UpdateExecContext.Parameters, SYS_PARAM_ENGINE_GLOBAL_SPAWN_COUNT_SCALE);
			SpawnGlobalSystemCountScaleParam.Init(SpawnExecContext.Parameters, SYS_PARAM_ENGINE_GLOBAL_SYSTEM_COUNT_SCALE);
			UpdateGlobalSystemCountScaleParam.Init(UpdateExecContext.Parameters, SYS_PARAM_ENGINE_GLOBAL_SYSTEM_COUNT_SCALE);
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
		SystemInstances.Last()->Deactivate(true);
	}
	while (PendingSystemInstances.Num())
	{
		PendingSystemInstances.Last()->Deactivate(true);
	}
	SystemInstances.Empty();
	PendingSystemInstances.Empty();

	//TArray<FNiagaraSystemInstance*> SystemInstances;
	//TArray<FNiagaraSystemInstance*> SpawningInstances;
	//TArray<FNiagaraSystemInstance*> PausedSystemInstances;
	//PausedSystemInstances

	FNiagaraWorldManager* WorldMan = FNiagaraWorldManager::Get(World);
	check(WorldMan);
	SpawnExecContext.Parameters.UnbindFromSourceStores();
	UpdateExecContext.Parameters.UnbindFromSourceStores();
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
	SpawnExecContext.Parameters.DumpParameters(false);
	SpawnInstanceParameterDataSet.Dump(Inst->SystemInstanceIndex, 1, TEXT("Spawn Instance Parameters"));
	UE_LOG(LogNiagara, Log, TEXT(".................Update................."));
	UpdateExecContext.Parameters.DumpParameters(false);
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
	check(IsInGameThread());
	WaitForSystemTickComplete(true);

	SCOPE_CYCLE_COUNTER(STAT_NiagaraOverview_GT);
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSystemSim_TickGT);
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(Niagara);
	LLM_SCOPE(ELLMTag::Niagara);
	FScopeCycleCounterUObject AdditionalScope(GetSystem(), GET_STATID(STAT_NiagaraOverview_GT_CNC));

	bHasEverTicked = true;

	SystemTickGraphEvent = nullptr;

	check(SystemInstances.Num() == MainDataSet.GetCurrentDataChecked().GetNumInstances());
	check(PausedSystemInstances.Num() == PausedInstanceData.GetCurrentDataChecked().GetNumInstances());

	UNiagaraSystem* System = WeakSystem.Get();
	FNiagaraScopedRuntimeCycleCounter RuntimeScope(System, true, false);
	FScopeCycleCounter SystemStatCounter(System->GetStatID(true, false));

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

	const bool bUpdateTickGroups = !bIsSolo && !bInSpawnPhase;

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
				checkSlow(Inst->SystemInstanceIndex == SystemIndex);
				++SystemIndex;
			}
			else
			{
				checkSlow((SystemInstances.Num() <= SystemIndex) || (Inst == SystemInstances[SystemIndex]));
			}
		}
	}

	//Setup the few real constants like delta time.
	const float InvDt = 1.0f / DeltaSeconds;

	SpawnTimeParam.SetValue(World->TimeSeconds);
	UpdateTimeParam.SetValue(World->TimeSeconds);
	SpawnDeltaTimeParam.SetValue(DeltaSeconds);
	UpdateDeltaTimeParam.SetValue(DeltaSeconds);
	SpawnInvDeltaTimeParam.SetValue(InvDt);
	UpdateInvDeltaTimeParam.SetValue(InvDt);
	SpawnNumSystemInstancesParam.SetValue(SystemInstances.Num());
	UpdateNumSystemInstancesParam.SetValue(SystemInstances.Num());
	SpawnGlobalSpawnCountScaleParam.SetValue(INiagaraModule::GetGlobalSpawnCountScale());
	UpdateGlobalSpawnCountScaleParam.SetValue(INiagaraModule::GetGlobalSpawnCountScale());
	SpawnGlobalSystemCountScaleParam.SetValue(INiagaraModule::GetGlobalSystemCountScale());
	UpdateGlobalSystemCountScaleParam.SetValue(INiagaraModule::GetGlobalSystemCountScale());

	// Somethings we don't want to happen during the spawn phase
	if ( !bInSpawnPhase )
	{
		int32 SpawnNum = 0;

		// Gather any pending spawn systems and add to the end of the system instances
		if (PendingSystemInstances.Num() > 0)
		{
			SystemInstances.Reserve(SystemInstances.Num() + PendingSystemInstances.Num());

			SystemIndex = 0;
			while (SystemIndex < PendingSystemInstances.Num())
			{
				FNiagaraSystemInstance* Inst = PendingSystemInstances[SystemIndex];

				// If we are paused continue
				if (Inst->IsPaused())
				{
					++SystemIndex;
					continue;
				}

				// If our tick group has changed we need to move this pending instance
				// Note we don't do this with solo instances
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
						Inst->SystemInstanceIndex = DestSim->PendingSystemInstances.Add(Inst);
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

		static const auto EffectsQualityCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("sg.EffectsQuality"));
		FNiagaraSystemSimulationTickContext Context(this, SystemInstances, MainDataSet, DeltaSeconds, SpawnNum, EffectsQualityCVar->GetInt(), MyCompletionGraphEvent);

		//Now kick of the concurrent tick.
		if (Context.bTickAsync)
		{
			TGraphTask<FNiagaraSystemSimulationWaitAllFinalizeTask>* WaitAllFinalizeTask = TGraphTask<FNiagaraSystemSimulationWaitAllFinalizeTask>::CreateTask(nullptr, ENamedThreads::GameThread).ConstructAndHold(Context.FinalizeEvents);
			FGraphEventRef FinalizeGraphEvent = WaitAllFinalizeTask->GetCompletionEvent();

			TGraphTask<FNiagaraSystemSimulationTickTask>* SimulationTickTask = TGraphTask<FNiagaraSystemSimulationTickTask>::CreateTask(nullptr, ENamedThreads::GameThread).ConstructAndHold(Context, WaitAllFinalizeTask);
			SystemTickGraphEvent = SimulationTickTask->GetCompletionEvent();
			Context.FinalizeEvents->Add(SystemTickGraphEvent);

			MyCompletionGraphEvent->SetGatherThreadForDontCompleteUntil(ENamedThreads::GameThread);
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
}

void FNiagaraSystemSimulation::UpdateTickGroups_GameThread()
{
	check(IsInGameThread());
	check(!bIsSolo);

	SCOPE_CYCLE_COUNTER(STAT_NiagaraSystemSim_SpawnNewGT);
	SCOPE_CYCLE_COUNTER(STAT_NiagaraOverview_GT);
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(Niagara);
	LLM_SCOPE(ELLMTag::Niagara);
	FScopeCycleCounterUObject AdditionalScope(GetSystem(), GET_STATID(STAT_NiagaraOverview_GT_CNC));

	FNiagaraWorldManager* WorldManager = FNiagaraWorldManager::Get(World);
	check(WorldManager != nullptr);

	UNiagaraSystem* System = WeakSystem.Get();
	check(System != nullptr);

	FNiagaraScopedRuntimeCycleCounter RuntimeScope(System, true, false);

	// Transfer promoted instances to the new tick group
	//-OPT: This can be done async
	for (FNiagaraSystemInstance* Instance : PendingTickGroupPromotions)
	{
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
				Instance->SystemInstanceIndex = DestSim->PendingSystemInstances.Add(Instance);
				continue;
			}
		}
		++SystemIndex;
	}
}

void FNiagaraSystemSimulation::Spawn_GameThread(float DeltaSeconds)
{
	// Early out, nothing to do
	if (PendingSystemInstances.Num() == 0)
	{
		return;
	}

	SCOPE_CYCLE_COUNTER(STAT_NiagaraSystemSim_SpawnNewGT);
	SCOPE_CYCLE_COUNTER(STAT_NiagaraOverview_GT);
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(Niagara);
	LLM_SCOPE(ELLMTag::Niagara);

	UNiagaraSystem* System = WeakSystem.Get();
	FScopeCycleCounterUObject AdditionalScope(System, GET_STATID(STAT_NiagaraOverview_GT_CNC));

	WaitForSystemTickComplete(true);

	bInSpawnPhase = true;

	if (bHasEverTicked == false)
	{
		Tick_GameThread(DeltaSeconds, nullptr);
	}

	FNiagaraScopedRuntimeCycleCounter RuntimeScope(System, true, false);

	// Spawn instances
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
		Instance->Tick_GameThread(DeltaSeconds);

		if (Instance->SystemInstanceIndex != INDEX_NONE)
		{
			// When the first instance is added we need to initialize the parameter store to data set bindings.
			if (!bBindingsInitialized)
			{
				InitParameterDataSetBindings(Instance);
			}

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
		//-OPT: This can be async :)
		static const auto EffectsQualityCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("sg.EffectsQuality"));
		FNiagaraSystemSimulationTickContext Context(this, SpawningInstances, SpawningDataSet, DeltaSeconds, SpawningInstances.Num(), EffectsQualityCVar->GetInt(), nullptr);
		Tick_Concurrent(Context);

		check(MainDataSet.GetCurrentDataChecked().GetNumInstances() == SystemInstances.Num());
		check(PausedInstanceData.GetCurrentDataChecked().GetNumInstances() == PausedSystemInstances.Num());
		check(SpawningDataSet.GetCurrentDataChecked().GetNumInstances() == SpawningInstances.Num());

		// Append spawned data to our active DataSet
		SpawningDataSet.CopyTo(MainDataSet, 0, INDEX_NONE, false);
		SpawningDataSet.ResetBuffers();

		// Move instances
		SystemInstances.Reserve(SystemInstances.Num() + SpawningInstances.Num());
		for ( FNiagaraSystemInstance* Instance : SpawningInstances )
		{
			checkSlow(!Instance->IsComplete());
			Instance->SystemInstanceIndex = SystemInstances.Add(Instance);
		}
		SpawningInstances.Reset();

		check(MainDataSet.GetCurrentDataChecked().GetNumInstances() == SystemInstances.Num());
		check(PausedInstanceData.GetCurrentDataChecked().GetNumInstances() == PausedSystemInstances.Num());
	}

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
		Inst->WaitForAsyncTick(bInSpawnPhase);

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
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(Niagara);
	LLM_SCOPE(ELLMTag::Niagara);

	FScopeCycleCounterUObject AdditionalScope(Context.System, GET_STATID(STAT_NiagaraOverview_GT_CNC));

	FNiagaraScopedRuntimeCycleCounter RuntimeScope(Context.System, true, true);
	FNiagaraSystemInstance* SoloSystemInstance = bIsSolo && Context.Instances.Num() == 1 ? Context.Instances[0] : nullptr;

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

		if (Context.Owner->GetSystem()->FastPathMode != ENiagaraFastPathMode::ScriptVMOnly)
		{
			TickFastPath(Context);
		}

		if(Context.Owner->GetSystem()->FastPathMode != ENiagaraFastPathMode::FastPathOnly)
		{
			PrepareForSystemSimulate(Context);

			if (Context.SpawnNum > 0)
			{
				SpawnSystemInstances(Context);
			}

			UpdateSystemInstances(Context);

			TransferSystemSimResults(Context);
		}

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
				checkSlow(!Inst->IsComplete());
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

void FNiagaraSystemSimulation::TickFastPath(FNiagaraSystemSimulationTickContext& Context)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSystemSim_FastPathCNC);

	// PrepareForSystemSimulate
	for (FNiagaraSystemInstance* SystemInstance : Context.Instances)
	{
		FNiagaraSystemFastPath::FParamMap0& SystemMap = SystemInstance->GetFastPathMap();
		SystemMap.Engine.Owner.ExecutionState = SystemInstance->GetRequestedExecutionState();
		SystemMap.Engine.Owner.TimeSinceRendered = SystemInstance->GetSystemTimeSinceRendered();

		TArray<TSharedRef<FNiagaraEmitterInstance, ESPMode::ThreadSafe>>& EmitterInstances = SystemInstance->GetEmitters();
		for (int32 EmitterIndex = 0; EmitterIndex < EmitterInstances.Num(); ++EmitterIndex)
		{
			TSharedRef<FNiagaraEmitterInstance, ESPMode::ThreadSafe>& EmitterInstance = EmitterInstances[EmitterIndex];
			if (EmitterInstance->GetExecutionState() == ENiagaraExecutionState::Disabled)
			{
				continue;
			}
			FNiagaraEmitterFastPath::FParamMap0& EmitterMap = EmitterInstance->GetFastPathMap();
			EmitterMap.Engine.Owner.LODDistance = SystemInstance->GetOwnerLODDistance();
			EmitterMap.Engine.Owner.MaxLODDistance = SystemInstance->GetOwnerMaxLODDistance();
			EmitterMap.Engine.DeltaTime = Context.DeltaSeconds;
			EmitterMap.Engine.Emitter.NumParticles = SystemInstance->GetNumParticles(EmitterIndex);
			EmitterMap.Engine.Owner.Velocity = SystemInstance->GetOwnerVelocity();
			EmitterMap.Emitter.ExecutionState = EmitterInstance->GetExecutionState();
			EmitterMap.Engine.GlobalSpawnCountScale = INiagaraModule::GetGlobalSpawnCountScale();

			if (UNiagaraEmitter* CachedEmitter = EmitterInstance->GetCachedEmitter())
			{
				EmitterMap.Emitter.SpawnCountScale = CachedEmitter->GetSpawnCountScale(Context.EffectsQuality);
			}
		}
	}

	// SpawnSystemInstances
	int32 CurrentNumInstances = Context.Instances.Num() - Context.SpawnNum;
	if (Context.SpawnNum > 0)
	{
		// There is no simulation work to do here, but the number of instances in the data set needs to be correct for other codepaths.
		FNiagaraDataSet& SimulationDataSet = Context.DataSet;
		SimulationDataSet.BeginSimulate();
		SimulationDataSet.Allocate(Context.Instances.Num());
		SimulationDataSet.GetDestinationDataChecked().SetNumInstances(Context.Instances.Num());
		SimulationDataSet.EndSimulate();

		for(int32 SpawnIndex = CurrentNumInstances; SpawnIndex < Context.Instances.Num(); ++SpawnIndex)
		{
			FNiagaraSystemInstance* SystemInstance = Context.Instances[SpawnIndex];
			SystemInstance->ResetFastPathBindings();

			FNiagaraSystemFastPath::SetSpawnMapDefaults(SystemInstance->FastPathMap);
			TArray<TSharedRef<FNiagaraEmitterInstance, ESPMode::ThreadSafe>>& EmitterInstances = SystemInstance->GetEmitters();
			for (TSharedRef<FNiagaraEmitterInstance, ESPMode::ThreadSafe>& EmitterInstance : EmitterInstances)
			{
				const UNiagaraEmitter* Emitter = EmitterInstance->GetCachedEmitter();
				if (EmitterInstance->GetExecutionState() == ENiagaraExecutionState::Disabled)
				{
					continue;
				}

				FNiagaraEmitterFastPath::FParamMap0& EmitterMap = EmitterInstance->GetFastPathMap();
				FNiagaraEmitterFastPath::SetSpawnMapDefaults(*Emitter, EmitterMap);
				EmitterInstance->InitFastPathAttributeBindings();

				EmitterMap.SpawnRate.SetNumUninitialized(Emitter->SpawnRate.Num());
				for (int32 SpawnRateIndex = 0; SpawnRateIndex < Emitter->SpawnRate.Num(); ++SpawnRateIndex)
				{
					EmitterMap.SpawnRate[SpawnRateIndex].Init(
						Emitter, SpawnRateIndex, SystemInstance->GetInstanceParameters(),
						SystemInstance->FastPathIntUserParameterInputBindings, SystemInstance->FastPathFloatUserParameterInputBindings,
						SystemInstance->FastPathIntUpdateRangedInputBindings, SystemInstance->FastPathFloatUpdateRangedInputBindings);
				}

				EmitterMap.SpawnPerUnit.SetNumUninitialized(Emitter->SpawnPerUnit.Num());
				for (int32 SpawnPerUnitIndex = 0; SpawnPerUnitIndex < Emitter->SpawnPerUnit.Num(); ++SpawnPerUnitIndex)
				{
					EmitterMap.SpawnPerUnit[SpawnPerUnitIndex].Init(
						Emitter, SpawnPerUnitIndex, SystemInstance->GetInstanceParameters(),
						SystemInstance->FastPathIntUserParameterInputBindings, SystemInstance->FastPathFloatUserParameterInputBindings,
						SystemInstance->FastPathIntUpdateRangedInputBindings, SystemInstance->FastPathFloatUpdateRangedInputBindings);
				}

				EmitterMap.SpawnBurst_Instantaneous.SetNumUninitialized(Emitter->SpawnBurstInstantaneous.Num());
				for (int32 SpawnBurstInstantaneousIndex = 0; SpawnBurstInstantaneousIndex < Emitter->SpawnBurstInstantaneous.Num(); ++SpawnBurstInstantaneousIndex)
				{
					EmitterMap.SpawnBurst_Instantaneous[SpawnBurstInstantaneousIndex].Init(
						Emitter, SpawnBurstInstantaneousIndex, SystemInstance->GetInstanceParameters(),
						SystemInstance->FastPathIntUserParameterInputBindings, SystemInstance->FastPathFloatUserParameterInputBindings,
						SystemInstance->FastPathIntUpdateRangedInputBindings, SystemInstance->FastPathFloatUpdateRangedInputBindings);
				}
			}
		}
	}

	// UpdateSystemInstances
	const UNiagaraSystem* System = GetSystem();
	auto UpdateSystemInstance = [System](FNiagaraSystemInstance* SystemInstance)
	{
		FNiagaraSystemFastPath::FParamMap0& SystemMap = SystemInstance->FastPathMap;
		FNiagaraSystemFastPath::SetUpdateMapDefaults(SystemMap);
		SystemInstance->TickFastPathBindings();

		if (System->SystemScalability.bUseSystemScalability)
		{
			FNiagaraSystemFastPath::Module_SystemScalability(System->SystemScalability, SystemMap);
		}

		FNiagaraSystemFastPath::Module_SystemLifeCycle(System->SystemLifeCycle, SystemMap);

		TArray<TSharedRef<FNiagaraEmitterInstance, ESPMode::ThreadSafe>>& EmitterInstances = SystemInstance->GetEmitters();
		for (TSharedRef<FNiagaraEmitterInstance, ESPMode::ThreadSafe>& EmitterInstance : EmitterInstances)
		{
			if (EmitterInstance->GetExecutionState() == ENiagaraExecutionState::Disabled)
			{
				continue;
			}

			FNiagaraEmitterFastPath::FParamMap0& EmitterMap = EmitterInstance->GetFastPathMap();
			FNiagaraEmitterFastPath::SetUpdateMapDefaults(EmitterMap);
			EmitterMap.System = &SystemMap.System;

			const UNiagaraEmitter* Emitter = EmitterInstance->GetCachedEmitter();
			if (Emitter->EmitterScalability.bUseEmitterScalability)
			{
				FNiagaraEmitterFastPath::Module_EmitterScalability(Emitter->EmitterScalability, EmitterMap);
			}

			FNiagaraEmitterFastPath::Module_EmitterLifeCycle(Emitter->EmitterLifeCycle, EmitterMap);

			if (Emitter->SpawnRate.Num() > 0)
			{
				FNiagaraEmitterFastPath::Module_SpawnRate(EmitterMap);
			}

			if (Emitter->SpawnPerUnit.Num() > 0)
			{
				FNiagaraEmitterFastPath::Module_SpawnPerUnit(EmitterMap);
			}

			if (Emitter->SpawnBurstInstantaneous.Num() > 0)
			{
				FNiagaraEmitterFastPath::Module_SpawnBurstInstantaneous(EmitterMap);
			}
		}
	};

	// Run update on current instances.
	for (int32 CurrentIndex = 0; CurrentIndex < CurrentNumInstances; ++CurrentIndex)
	{
		FNiagaraSystemInstance* CurrentInstance = Context.Instances[CurrentIndex];
		UpdateSystemInstance(CurrentInstance);
	}

	// Run updated on spawned instances with a small delta time.
	for (int32 SpawnIndex = CurrentNumInstances; SpawnIndex < Context.Instances.Num(); ++SpawnIndex)
	{
		FNiagaraSystemInstance* SpawnedInstance = Context.Instances[SpawnIndex];
		TArray<TSharedRef<FNiagaraEmitterInstance, ESPMode::ThreadSafe>>& EmitterInstances = SpawnedInstance->GetEmitters();
		for (int32 EmitterIndex = 0; EmitterIndex < EmitterInstances.Num(); ++EmitterIndex)
		{
			TSharedRef<FNiagaraEmitterInstance, ESPMode::ThreadSafe>& EmitterInstance = EmitterInstances[EmitterIndex];
			if (EmitterInstance->GetExecutionState() == ENiagaraExecutionState::Disabled)
			{
				continue;
			}

			EmitterInstance->GetFastPathMap().Engine.DeltaTime = 0.0001f;
		}
		UpdateSystemInstance(SpawnedInstance);
	}

	// TransferSystemSimResults
	for (FNiagaraSystemInstance* SystemInstance : Context.Instances)
	{
		SystemInstance->SetActualExecutionState(SystemInstance->FastPathMap.System.ExecutionState);
		if (SystemInstance->IsDisabled() == false)
		{
			TArray<TSharedRef<FNiagaraEmitterInstance, ESPMode::ThreadSafe>>& EmitterInstances = SystemInstance->GetEmitters();
			for (TSharedRef<FNiagaraEmitterInstance, ESPMode::ThreadSafe>& EmitterInstance : EmitterInstances)
			{
				if (EmitterInstance->GetExecutionState() == ENiagaraExecutionState::Disabled)
				{
					continue;
				}

				FNiagaraEmitterFastPath::FParamMap0& EmitterMap = EmitterInstance->GetFastPathMap();

				EmitterInstance->SetExecutionState(EmitterMap.Emitter.ExecutionState);

				TArray<FNiagaraSpawnInfo>& EmitterInstSpawnInfos = EmitterInstance->GetSpawnInfo();

				int32 SpawnInfoIndex = 0;

				for (const FNiagaraEmitterFastPath::FParamMap0_Emitter_SpawnRate& SpawnRateOutput : EmitterMap.Emitter.SpawnRate)
				{
					EmitterInstSpawnInfos[SpawnInfoIndex] = SpawnRateOutput.SpawnOutputInfo;
					SpawnInfoIndex++;
				}

				for (const FNiagaraEmitterFastPath::FParamMap0_Emitter_SpawnPerUnit& SpawnPerUnitOutput : EmitterMap.Emitter.SpawnPerUnit)
				{
					EmitterInstSpawnInfos[SpawnInfoIndex] = SpawnPerUnitOutput.SpawnOutputInfo;
					SpawnInfoIndex++;
				}

				for (const FNiagaraEmitterFastPath::FParamMap0_Emitter_SpawnBurst_Instantaneous& SpawnBurstInstantaneousOutput : EmitterMap.Emitter.SpawnBurst_Instantaneous)
				{
					EmitterInstSpawnInfos[SpawnInfoIndex] = SpawnBurstInstantaneousOutput.SpawnBurst;
					SpawnInfoIndex++;
				}

				EmitterInstance->TickFastPathAttributeBindings();
			}
		}
	}
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

	bool bUsingFastPath = GetSystem()->FastPathMode != ENiagaraFastPathMode::ScriptVMOnly;
	if (bUsingFastPath == false)
	{
		for (int32 EmitterIdx = 0; EmitterIdx < Context.System->GetNumEmitters(); ++EmitterIdx)
		{
			EmitterExecutionStateAccessors[EmitterIdx].InitForAccess();
		}
	}

	//Tick instance parameters and transfer any needed into the system simulation dataset.
	auto TransferInstanceParameters = [&](int32 SystemIndex)
	{
		FNiagaraSystemInstance* Inst = Context.Instances[SystemIndex];

		if (Inst->GetParameters().GetParametersDirty() && bCanExecute)
		{
			SpawnInstanceParameterToDataSetBinding.ParameterStoreToDataSet(Inst->GetParameters(), SpawnInstanceParameterDataSet, SystemIndex);
			UpdateInstanceParameterToDataSetBinding.ParameterStoreToDataSet(Inst->GetParameters(), UpdateInstanceParameterDataSet, SystemIndex);
		}

		//TODO: Find good way to check that we're not using any instance parameter data interfaces in the system scripts here.
		//In that case we need to solo and will never get here.

		if (bUsingFastPath == false)
		{
			TArray<TSharedRef<FNiagaraEmitterInstance, ESPMode::ThreadSafe>>& Emitters = Inst->GetEmitters();
			for (int32 EmitterIdx = 0; EmitterIdx < Emitters.Num(); ++EmitterIdx)
			{
				FNiagaraEmitterInstance& EmitterInst = Emitters[EmitterIdx].Get();
				if (EmitterExecutionStateAccessors.Num() > EmitterIdx && EmitterExecutionStateAccessors[EmitterIdx].IsValidForWrite())
				{
					EmitterExecutionStateAccessors[EmitterIdx].Set(SystemIndex, (int32)EmitterInst.GetExecutionState());
				}
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

	//Run Spawn
	SpawnExecContext.Tick(SoloSystemInstance);//We can't require a specific instance here as these are for all instances.
	SpawnExecContext.BindData(0, Context.DataSet, OrigNum, false);
	SpawnExecContext.BindData(1, SpawnInstanceParameterDataSet, OrigNum, false);
	SpawnExecContext.Execute(SpawnNum);

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
			DebugInfo->Parameters = UpdateExecContext.Parameters;
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

		//Run update.
		if (OrigNum > 0)
		{
			UpdateExecContext.Tick(Context.Instances[0]);
			UpdateExecContext.BindData(0, Context.DataSet, 0, false);
			UpdateExecContext.BindData(1, UpdateInstanceParameterDataSet, 0, false);
			UpdateExecContext.Execute(OrigNum);
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
			UpdateExecContext.Tick(Context.Instances[0]);
			UpdateExecContext.Parameters.SetParameterValue(0.0001f, SYS_PARAM_ENGINE_DELTA_TIME);
			UpdateExecContext.Parameters.SetParameterValue(10000.0f, SYS_PARAM_ENGINE_INV_DELTA_TIME);

			UpdateExecContext.BindData(0, Context.DataSet, OrigNum, false);
			UpdateExecContext.BindData(1, UpdateInstanceParameterDataSet, OrigNum, false);
			UpdateExecContext.Execute(SpawnNum);

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
				DebugInfo->Parameters = UpdateExecContext.Parameters;
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

	bool bIsUsingFastPath = GetSystem()->FastPathMode != ENiagaraFastPathMode::ScriptVMOnly;
	if (bIsUsingFastPath == false)
	{
		SystemExecutionStateAccessor.SetDataSet(Context.DataSet);
		SystemExecutionStateAccessor.InitForAccess();
		for (int32 EmitterIdx = 0; EmitterIdx < Context.System->GetNumEmitters(); ++EmitterIdx)
		{
			EmitterExecutionStateAccessors[EmitterIdx].SetDataSet(Context.DataSet);
			EmitterExecutionStateAccessors[EmitterIdx].InitForAccess();
			for (int32 SpawnInfoIdx = 0; SpawnInfoIdx < EmitterSpawnInfoAccessors[EmitterIdx].Num(); ++SpawnInfoIdx)
			{
				EmitterSpawnInfoAccessors[EmitterIdx][SpawnInfoIdx].SetDataSet(Context.DataSet);
				EmitterSpawnInfoAccessors[EmitterIdx][SpawnInfoIdx].InitForAccess();
			}
		}
	}

	for (int32 SystemIndex = 0; SystemIndex < Context.Instances.Num(); ++SystemIndex)
	{
		FNiagaraSystemInstance* SystemInst = Context.Instances[SystemIndex];

		if (bIsUsingFastPath == false)
		{
			//Apply the systems requested execution state to it's actual execution state.
			ENiagaraExecutionState ExecutionState = (ENiagaraExecutionState)SystemExecutionStateAccessor.GetSafe(SystemIndex, (int32)ENiagaraExecutionState::Disabled);
			SystemInst->SetActualExecutionState(ExecutionState);
		}

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

				if (bIsUsingFastPath == false)
				{
					ENiagaraExecutionState State = (ENiagaraExecutionState)EmitterExecutionStateAccessors[EmitterIdx].GetSafe(SystemIndex, (int32)ENiagaraExecutionState::Disabled);
					EmitterInst.SetExecutionState(State);

					TArray<FNiagaraSpawnInfo>& EmitterInstSpawnInfos = EmitterInst.GetSpawnInfo();
					for (int32 SpawnInfoIdx = 0; SpawnInfoIdx < EmitterSpawnInfoAccessors[EmitterIdx].Num(); ++SpawnInfoIdx)
					{
						if (SpawnInfoIdx < EmitterInstSpawnInfos.Num())
						{
							EmitterInstSpawnInfos[SpawnInfoIdx] = EmitterSpawnInfoAccessors[EmitterIdx][SpawnInfoIdx].Get(SystemIndex);
						}
						else
						{
							ensure(SpawnInfoIdx < EmitterInstSpawnInfos.Num());
						}
					}
				}

				//TODO: Any other fixed function stuff like this?

				FNiagaraScriptExecutionContext& SpawnContext = EmitterInst.GetSpawnExecutionContext();
				DataSetToEmitterSpawnParameters[EmitterIdx].DataSetToParameterStore(SpawnContext.Parameters, Context.DataSet, SystemIndex);

				FNiagaraScriptExecutionContext& UpdateContext = EmitterInst.GetUpdateExecutionContext();
				DataSetToEmitterUpdateParameters[EmitterIdx].DataSetToParameterStore(UpdateContext.Parameters, Context.DataSet, SystemIndex);

				TArray<FNiagaraScriptExecutionContext>& EventContexts = EmitterInst.GetEventExecutionContexts();
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

	check(SystemInstances.Num() == MainDataSet.GetCurrentDataChecked().GetNumInstances());
	check(PausedSystemInstances.Num() == PausedInstanceData.GetCurrentDataChecked().GetNumInstances());

	check(IsInGameThread());
	if (EffectType)
	{
		--EffectType->NumInstances;
	}

	UNiagaraSystem* System = WeakSystem.Get();
	if (Instance->IsPendingSpawn())
	{
		if (GbDumpSystemData || (System && System->bDumpDebugSystemInfo))
		{
			UE_LOG(LogNiagara, Log, TEXT("=== Removing Pending Spawn %d ==="), Instance->SystemInstanceIndex);
			MainDataSet.GetCurrentDataChecked().Dump(Instance->SystemInstanceIndex, 1, TEXT("System data being removed."));
		}

		// Note: If we go async with PostActor spawning we will need to ensure this remove doesn't happen other than inside our task
		TArray<FNiagaraSystemInstance*>& Instances = bInSpawnPhase ? SpawningInstances : PendingSystemInstances;

		int32 SystemIndex = Instance->SystemInstanceIndex;
		check(Instances.IsValidIndex(SystemIndex));
		check(Instance == Instances[SystemIndex]);

		if ( bInSpawnPhase )
		{
			SpawningDataSet.GetCurrentDataChecked().KillInstance(Instance->SystemInstanceIndex);
		}

		Instances.RemoveAtSwap(SystemIndex);
		Instance->SystemInstanceIndex = INDEX_NONE;
		Instance->SetPendingSpawn(false);
		if (Instances.IsValidIndex(SystemIndex))
		{
			Instances[SystemIndex]->SystemInstanceIndex = SystemIndex;
		}
	}
	else if (Instance->IsPaused())
	{
		if (GbDumpSystemData || (System && System->bDumpDebugSystemInfo))
		{
			UE_LOG(LogNiagara, Log, TEXT("=== Removing Paused %d ==="), Instance->SystemInstanceIndex);
			MainDataSet.GetCurrentDataChecked().Dump(Instance->SystemInstanceIndex, 1, TEXT("System data being removed."));
		}

		int32 NumInstances = PausedInstanceData.GetCurrentDataChecked().GetNumInstances();
		check(PausedSystemInstances.Num() == NumInstances);

		int32 SystemIndex = Instance->SystemInstanceIndex;
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
		Instance->WaitForAsyncTick();

		int32 NumInstances = MainDataSet.GetCurrentDataChecked().GetNumInstances();
		check(SystemInstances.Num() == NumInstances);

		int32 SystemIndex = Instance->SystemInstanceIndex;
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

#if NIAGARA_NAN_CHECKING
	MainDataSet.CheckForNaNs();
#endif
}

void FNiagaraSystemSimulation::AddInstance(FNiagaraSystemInstance* Instance)
{
	check(IsInGameThread());
	check(Instance->SystemInstanceIndex == INDEX_NONE);

	Instance->SetPendingSpawn(true);
	Instance->SystemInstanceIndex = PendingSystemInstances.Add(Instance);

	UNiagaraSystem* System = WeakSystem.Get();
	if (GbDumpSystemData || (System && System->bDumpDebugSystemInfo))
	{
		UE_LOG(LogNiagara, Log, TEXT("=== Adding To Pending Spawn %d ==="), Instance->SystemInstanceIndex);
		//MainDataSet.Dump(true, Instance->SystemInstanceIndex, 1);
	}
	
	if (EffectType)
	{
		++EffectType->NumInstances;
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
		bBindingsInitialized = true;

		SpawnInstanceParameterToDataSetBinding.Init(SpawnInstanceParameterDataSet, SystemInst->GetInstanceParameters());
		UpdateInstanceParameterToDataSetBinding.Init(UpdateInstanceParameterDataSet, SystemInst->GetInstanceParameters());

		TArray<TSharedRef<FNiagaraEmitterInstance, ESPMode::ThreadSafe>>& Emitters = SystemInst->GetEmitters();
		DataSetToEmitterSpawnParameters.SetNum(Emitters.Num());
		DataSetToEmitterUpdateParameters.SetNum(Emitters.Num());
		DataSetToEmitterEventParameters.SetNum(Emitters.Num());
		for (int32 EmitterIdx = 0; EmitterIdx < Emitters.Num(); ++EmitterIdx)
		{
			FNiagaraEmitterInstance& EmitterInst = Emitters[EmitterIdx].Get();
			FNiagaraScriptExecutionContext& SpawnContext = EmitterInst.GetSpawnExecutionContext();
			DataSetToEmitterSpawnParameters[EmitterIdx].Init(MainDataSet, SpawnContext.Parameters);

			FNiagaraScriptExecutionContext& UpdateContext = EmitterInst.GetUpdateExecutionContext();
			DataSetToEmitterUpdateParameters[EmitterIdx].Init(MainDataSet, UpdateContext.Parameters);

			TArray<FNiagaraScriptExecutionContext>& EventContexts = EmitterInst.GetEventExecutionContexts();
			DataSetToEmitterEventParameters[EmitterIdx].SetNum(EventContexts.Num());
			for (int32 EventIdx = 0; EventIdx < EventContexts.Num(); ++EventIdx)
			{
				FNiagaraScriptExecutionContext& EventContext = EventContexts[EventIdx];
				DataSetToEmitterEventParameters[EmitterIdx][EventIdx].Init(MainDataSet, EventContext.Parameters);
			}
		}
	}
}

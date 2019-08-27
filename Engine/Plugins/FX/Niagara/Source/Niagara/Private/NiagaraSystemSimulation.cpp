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

//High level stats for system sim tick.
DECLARE_CYCLE_STAT(TEXT("System Simulaton Tick [GT]"), STAT_NiagaraSystemSim_TickGT, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("System Simulaton Tick [CNC]"), STAT_NiagaraSystemSim_TickCNC, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("System Simulaton SpawnNew [GT]"), STAT_NiagaraSystemSim_SpawnNewGT, STATGROUP_Niagara);
//Some more detailed stats for system sim tick
DECLARE_CYCLE_STAT(TEXT("System Prepare For Simulate [CNC]"), STAT_NiagaraSystemSim_PrepareForSimulateCNC, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("System Sim Update [CNC]"), STAT_NiagaraSystemSim_UpdateCNC, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("System Sim Spawn [CNC]"), STAT_NiagaraSystemSim_SpawnCNC, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("System Sim Transfer Results [CNC]"), STAT_NiagaraSystemSim_TransferResultsCNC, STATGROUP_Niagara);


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

static int32 GbForceNiagaraAsyncWait = 0;
static FAutoConsoleVariableRef CVarForceNiagaraAsyncWait(
	TEXT("fx.ForceNiagaraAsyncWait"),
	GbForceNiagaraAsyncWait,
	TEXT(""),
	ECVF_Default
);

static float GbWaitForAsyncStallMS = .5f;
static FAutoConsoleVariableRef CVarWaitForAsyncStallMS(
	TEXT("fx.WaitForAsyncStallMS"),
	GbWaitForAsyncStallMS,
	TEXT(""),
	ECVF_Default
);

//////////////////////////////////////////////////////////////////////////

FNiagaraSystemSimulationTickContext::FNiagaraSystemSimulationTickContext(FNiagaraSystemSimulation* InOwner, float InDeltaSeconds, bool bInPendingSpawnPass, const FGraphEventRef& InMyCompletionGraphEvent)
	: Owner(InOwner)
	, DeltaSeconds(InDeltaSeconds)
	, bPendingSpawnPass(bInPendingSpawnPass)
	, MyCompletionGraphEvent(InMyCompletionGraphEvent)
{
	System = Owner->GetSystem();
	
	bTickAsync = Owner->ShouldTickAsync(*this);
	bTickInstancesAsync = Owner->ShouldTickInstancesAsync(*this);
}

TArray<FNiagaraSystemInstance*>& FNiagaraSystemSimulationTickContext::GetInstances()
{
	return bPendingSpawnPass ? Owner->SpawningSystemInstances : Owner->SystemInstances;
}

FNiagaraDataSet& FNiagaraSystemSimulationTickContext::GetDataSet()
{
	return bPendingSpawnPass ? Owner->PendingSpawnDataSet : Owner->DataSet;
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

FAutoConsoleTaskPriority CPrio_NiagaraSystemSimulationTickTask(
	TEXT("TaskGraph.TaskPriorities.NiagaraSystemSimulationTickcTask"),
	TEXT("Task and thread priority for FNiagaraSystemSimulationTickTask."),
	ENamedThreads::HighThreadPriority, // if we have high priority task threads, then use them...
	ENamedThreads::NormalTaskPriority, // .. at normal task priority
	ENamedThreads::HighTaskPriority // if we don't have hi pri threads, then use normal priority threads at high task priority instead
);

//This task performs the concurrent part of the system simulation tick.
class FNiagaraSystemSimulationTickTask
{
	FNiagaraSystemSimulationTickContext Context;
public:
	FNiagaraSystemSimulationTickTask(FNiagaraSystemSimulationTickContext InContext)
		: Context(InContext)
	{
	}

	FORCEINLINE TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FNiagaraSystemSimulationTickTask, STATGROUP_TaskGraphTasks);
	}

	ENamedThreads::Type GetDesiredThread()
	{
		return CPrio_NiagaraSystemSimulationTickTask.Get();
	}

	static ESubsequentsMode::Type GetSubsequentsMode() { return ESubsequentsMode::TrackSubsequents; }

	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		Context.MyCompletionGraphEvent = MyCompletionGraphEvent;
		Context.Owner->Tick_Concurrent(Context);
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

	FORCEINLINE TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FNiagaraSystemInstanceFinalizeTask, STATGROUP_TaskGraphTasks);
	}

	ENamedThreads::Type GetDesiredThread()
	{
		return ENamedThreads::GameThread;
	}

	static ESubsequentsMode::Type GetSubsequentsMode() { return ESubsequentsMode::TrackSubsequents; }

	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		check(CurrentThread == ENamedThreads::GameThread);

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

	FORCEINLINE TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FNiagaraSystemInstanceAsyncTask, STATGROUP_TaskGraphTasks);
	}

	ENamedThreads::Type GetDesiredThread()
	{
		return CPrio_NiagaraSystemInstanceAsyncTask.Get();
	}

	static ESubsequentsMode::Type GetSubsequentsMode() { return ESubsequentsMode::TrackSubsequents; }

	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		for (FNiagaraSystemInstance* Inst : Batch)
		{
			Inst->Tick_Concurrent();
		}

		//Now kick off a finalize task for this batch on the game thread.
		FGraphEventRef FinalizeTask = TGraphTask<FNiagaraSystemInstanceFinalizeTask>::CreateTask(nullptr, CurrentThread).ConstructAndDispatchWhenReady(SystemSim, Batch);
		MyCompletionGraphEvent->SetGatherThreadForDontCompleteUntil(ENamedThreads::GameThread);
		MyCompletionGraphEvent->DontCompleteUntil(FinalizeTask);
	}
};

//////////////////////////////////////////////////////////////////////////

FNiagaraSystemSimulation::~FNiagaraSystemSimulation()
{
	Destroy();
}

bool FNiagaraSystemSimulation::Init(UNiagaraSystem* InSystem, UWorld* InWorld, bool bInIsSolo)
{
	UNiagaraSystem* System = InSystem;
	WeakSystem = System;

	World = InWorld;

	bIsSolo = bInIsSolo;

	bBindingsInitialized = false;
	bInSpawnPhase = false;

	FNiagaraWorldManager* WorldMan = FNiagaraWorldManager::Get(InWorld);
	check(WorldMan);

	bCanExecute = System->GetSystemSpawnScript()->GetVMExecutableData().IsValid() && System->GetSystemUpdateScript()->GetVMExecutableData().IsValid();
	UEnum* EnumPtr = FNiagaraTypeDefinition::GetExecutionStateEnum();

	if (bCanExecute)
	{
		//TODO: Move all layout data into the System!

		//Initilize the main simulation dataset.
		DataSet.Init(FNiagaraDataSetID(), ENiagaraSimTarget::CPUSim, System->GetFullName());
		DataSet.AddVariables(System->GetSystemSpawnScript()->GetVMExecutableData().Attributes);
		DataSet.AddVariables(System->GetSystemUpdateScript()->GetVMExecutableData().Attributes);
		DataSet.Finalize();

		//Initialize the data set for newly spawned systems.
		PendingSpawnDataSet.Init(FNiagaraDataSetID(), ENiagaraSimTarget::CPUSim, System->GetFullName());
		PendingSpawnDataSet.AddVariables(System->GetSystemSpawnScript()->GetVMExecutableData().Attributes);
		PendingSpawnDataSet.AddVariables(System->GetSystemUpdateScript()->GetVMExecutableData().Attributes);
		PendingSpawnDataSet.Finalize();

		//Initialize the dataset for paused systems.
		PausedInstanceData.Init(FNiagaraDataSetID(), ENiagaraSimTarget::CPUSim, System->GetFullName());
		PausedInstanceData.AddVariables(System->GetSystemSpawnScript()->GetVMExecutableData().Attributes);
		PausedInstanceData.AddVariables(System->GetSystemUpdateScript()->GetVMExecutableData().Attributes);
		PausedInstanceData.Finalize();

		{
			SpawnInstanceParameterDataSet.Init(FNiagaraDataSetID(), ENiagaraSimTarget::CPUSim, System->GetFullName());
			FNiagaraParameters* EngineParamsSpawn = System->GetSystemSpawnScript()->GetVMExecutableData().DataSetToParameters.Find(TEXT("Engine"));
			if (EngineParamsSpawn != nullptr)
			{
				SpawnInstanceParameterDataSet.AddVariables(EngineParamsSpawn->Parameters);
			}
			SpawnInstanceParameterDataSet.Finalize();
			UpdateInstanceParameterDataSet.Init(FNiagaraDataSetID(), ENiagaraSimTarget::CPUSim, System->GetFullName());
			FNiagaraParameters* EngineParamsUpdate = System->GetSystemUpdateScript()->GetVMExecutableData().DataSetToParameters.Find(TEXT("Engine"));
			if (EngineParamsUpdate != nullptr)
			{
				UpdateInstanceParameterDataSet.AddVariables(EngineParamsUpdate->Parameters);
			}
			UpdateInstanceParameterDataSet.Finalize();
		}

		UNiagaraScript* SpawnScript = System->GetSystemSpawnScript();
		UNiagaraScript* UpdateScript = System->GetSystemUpdateScript();

		SpawnExecContext.Init(SpawnScript, ENiagaraSimTarget::CPUSim);
		UpdateExecContext.Init(UpdateScript, ENiagaraSimTarget::CPUSim);

		//Bind parameter collections.
		for (UNiagaraParameterCollection* Collection : SpawnScript->GetCachedParameterCollectionReferences())
		{
			GetParameterCollectionInstance(Collection)->GetParameterStore().Bind(&SpawnExecContext.Parameters);
		}
		for (UNiagaraParameterCollection* Collection : UpdateScript->GetCachedParameterCollectionReferences())
		{
			GetParameterCollectionInstance(Collection)->GetParameterStore().Bind(&UpdateExecContext.Parameters);
		}

		TArray<UNiagaraScript*> Scripts;
		Scripts.Add(SpawnScript);
		Scripts.Add(UpdateScript);
		FNiagaraUtilities::CollectScriptDataInterfaceParameters(*System, Scripts, ScriptDefinedDataInterfaceParameters);

		ScriptDefinedDataInterfaceParameters.Bind(&SpawnExecContext.Parameters);
		ScriptDefinedDataInterfaceParameters.Bind(&UpdateExecContext.Parameters);

		SpawnScript->RapidIterationParameters.Bind(&SpawnExecContext.Parameters);
		UpdateScript->RapidIterationParameters.Bind(&UpdateExecContext.Parameters);

		SystemExecutionStateAccessor.Create(&DataSet, FNiagaraVariable(EnumPtr, TEXT("System.ExecutionState")));
		EmitterSpawnInfoAccessors.Reset();
		EmitterExecutionStateAccessors.Reset();
		EmitterSpawnInfoAccessors.SetNum(System->GetNumEmitters());

		for (int32 EmitterIdx = 0; EmitterIdx < System->GetNumEmitters(); ++EmitterIdx)
		{
			FNiagaraEmitterHandle& EmitterHandle = System->GetEmitterHandle(EmitterIdx);
			UNiagaraEmitter* Emitter = EmitterHandle.GetInstance();
			FString EmitterName = Emitter->GetUniqueEmitterName();
			check(Emitter);
			EmitterExecutionStateAccessors.Emplace(DataSet, FNiagaraVariable(EnumPtr, *(EmitterName + TEXT(".ExecutionState"))));
			const TArray<FNiagaraEmitterSpawnAttributes>& EmitterSpawnAttrNames = System->GetEmitterSpawnAttributes();
			
			check(EmitterSpawnAttrNames.Num() == System->GetNumEmitters());
			for (FName AttrName : EmitterSpawnAttrNames[EmitterIdx].SpawnAttributes)
			{
				EmitterSpawnInfoAccessors[EmitterIdx].Emplace(DataSet, FNiagaraVariable(FNiagaraTypeDefinition(FNiagaraSpawnInfo::StaticStruct()), AttrName));
			}

			if (Emitter->bLimitDeltaTime)
			{
				MaxDeltaTime = MaxDeltaTime.IsSet() ? FMath::Min(MaxDeltaTime.GetValue(), Emitter->MaxDeltaTimePerTick) : Emitter->MaxDeltaTimePerTick;
			}
		}

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

	return true;
}

void FNiagaraSystemSimulation::Destroy()
{
	check(IsInGameThread());
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
	SpawningSystemInstances.Empty();


	FNiagaraWorldManager* WorldMan = FNiagaraWorldManager::Get(World);
	check(WorldMan);
	SpawnExecContext.Parameters.UnbindFromSourceStores();
	UpdateExecContext.Parameters.UnbindFromSourceStores();
}

UNiagaraParameterCollectionInstance* FNiagaraSystemSimulation::GetParameterCollectionInstance(UNiagaraParameterCollection* Collection)
{
	UNiagaraSystem* System = WeakSystem.Get();
	check(System != nullptr);
	UNiagaraParameterCollectionInstance* Ret = System->GetParameterCollectionOverride(Collection);

	//If no explicit override from the system, just get the current instance set on the world.
	if (!Ret)
	{
		FNiagaraWorldManager* WorldMan = FNiagaraWorldManager::Get(World);
		Ret = WorldMan->GetParameterCollection(Collection);
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

	int32 SystemInstIdx = SystemInst->SystemInstanceIndex;
	if (!SystemInst->IsPendingSpawn() && SystemInst->SystemInstanceIndex != INDEX_NONE)
	{
// 		UE_LOG(LogNiagara, Log, TEXT("== Dataset Transfer ========================"));
// 		UE_LOG(LogNiagara, Log, TEXT(" ----- Existing values in src. Idx: %d -----"), SystemInstIdx);
// 		SourceSimulation->DataSet.Dump(true, SystemInstIdx, 1);

		//If we're not pending then the system actually has data to pull over. This is not fast.
		int32 NewDataSetIndex = DataSet.GetCurrentDataChecked().TransferInstance(SourceSimulation->DataSet.GetCurrentDataChecked(), SystemInstIdx, false);

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
	UE_LOG(LogNiagara, Log, TEXT("==  %s (%d) ========"), *Inst->GetSystem()->GetFullName(), Inst->SystemInstanceIndex);
	UE_LOG(LogNiagara, Log, TEXT(".................Spawn................."));
	SpawnExecContext.Parameters.DumpParameters(false);
	SpawnInstanceParameterDataSet.Dump(Inst->SystemInstanceIndex, 1, TEXT("Spawn Instance Parameters"));
	UE_LOG(LogNiagara, Log, TEXT(".................Update................."));
	UpdateExecContext.Parameters.DumpParameters(false);
	UpdateInstanceParameterDataSet.Dump(Inst->SystemInstanceIndex, 1, TEXT("Update Instance Parameters"));
	UE_LOG(LogNiagara, Log, TEXT("................. System Instance ................."));
	DataSet.Dump(Inst->SystemInstanceIndex, 1, TEXT("System Data"));
}

bool FNiagaraSystemSimulation::ShouldTickAsync(const FNiagaraSystemSimulationTickContext& Context)
{
	return GbParallelSystemSimTick && FApp::ShouldUseThreadingForPerformance() && !Context.bPendingSpawnPass && Context.MyCompletionGraphEvent.IsValid();
}

bool FNiagaraSystemSimulation::ShouldTickInstancesAsync(const FNiagaraSystemSimulationTickContext& Context)
{
	return GbParallelSystemInstanceTick && !bIsSolo && FApp::ShouldUseThreadingForPerformance() && !Context.bPendingSpawnPass && Context.MyCompletionGraphEvent.IsValid();
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
		if (Context.bTickInstancesAsync)
		{
			checkSlow(Context.MyCompletionGraphEvent.IsValid());
			//If we're able, kick off a task to process this batch on a task thread.
			//When this task has finished the concurrent ticks for this batch it will enqueue a finalize task for the batch on the game thread.
			FGraphEventRef AsyncTask = TGraphTask<FNiagaraSystemInstanceAsyncTask>::CreateTask(nullptr).ConstructAndDispatchWhenReady(this, TickBatch);
			Context.MyCompletionGraphEvent->SetGatherThreadForDontCompleteUntil(ENamedThreads::GameThread);
			Context.MyCompletionGraphEvent->DontCompleteUntil(AsyncTask);
		}
		else
		{
			//Otherwise just do directly here. This may already be on a task thread or could be on game thread.
			for (FNiagaraSystemInstance* Inst : TickBatch)
			{
				Inst->Tick_Concurrent();
			}

			if (Context.bTickAsync)
			{
				checkSlow(Context.MyCompletionGraphEvent.IsValid());
				//If we're ticking off the main thread then we still need to add a finalize task on the GT for this batch.
				FGraphEventRef FinalizeTask = TGraphTask<FNiagaraSystemInstanceFinalizeTask>::CreateTask(nullptr).ConstructAndDispatchWhenReady(this, TickBatch);
				Context.MyCompletionGraphEvent->SetGatherThreadForDontCompleteUntil(ENamedThreads::GameThread);
				Context.MyCompletionGraphEvent->DontCompleteUntil(FinalizeTask);
			}
		}
		TickBatch.Reset();
	}
}

void FNiagaraSystemSimulation::SpawnNew_GameThread(float DeltaSeconds, const FGraphEventRef& MyCompletionGraphEvent)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSystemSim_SpawnNewGT);
	SCOPE_CYCLE_COUNTER(STAT_NiagaraOverview_GT);
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(Niagara);
	LLM_SCOPE(ELLMTag::Niagara);

	check(SystemInstances.Num() == DataSet.GetCurrentDataChecked().GetNumInstances());
	check(PausedSystemInstances.Num() == PausedInstanceData.GetCurrentDataChecked().GetNumInstances());

	//We have to do new spwaning at the end of the frame to account for new niagara systmes spawned after our main simulation tick.
	//So here we build a list of instances to actually spawn from the PendingSpawnInstances.
	//Then spwan these into a new dataset which is then transfered into the main dataset.
	//Currently this is all done on the game thread but we could likely also move this off the GT too.

	UNiagaraSystem* System = WeakSystem.Get();

	check(IsInGameThread());
	if (PendingSystemInstances.Num() > 0)
	{
		int32 SystemIndex = 0;
		SpawningSystemInstances.Reserve(PendingSystemInstances.Num());
		
		while (SystemIndex < PendingSystemInstances.Num())
		{
			FNiagaraSystemInstance* Inst = PendingSystemInstances[SystemIndex];

			//If an instance is paused, just leave it in the PendingSpawnInstances.
			if (!Inst->IsPaused())
			{
				Inst->Tick_GameThread(DeltaSeconds);
								
				// TickDataInterfaces could remove the system so we only increment if the system has changed
				if (Inst->SystemInstanceIndex != INDEX_NONE)
				{
					checkSlow(!Inst->IsComplete());
					checkSlow(Inst->SystemInstanceIndex == SystemIndex);
					
					if (!bBindingsInitialized)
					{
						// When the first instance is added we need to initialize the parameter store to data set bindings.
						InitParameterDataSetBindings(Inst);
					}

					int32 SpawnInstIdx = SpawningSystemInstances.Add(Inst);
					PendingSystemInstances.RemoveAtSwap(SystemIndex);

					Inst->SystemInstanceIndex = SpawnInstIdx;
					if (PendingSystemInstances.IsValidIndex(SystemIndex))
					{
						PendingSystemInstances[SystemIndex]->SystemInstanceIndex = SystemIndex;
					}

					continue;
				}
			}
			
			++SystemIndex;
		}		

		if (SpawningSystemInstances.Num() > 0)
		{
			//Lets any RemoveInstance calls inside Tick_Concurrent know that we're spawning and all our instances are in SpawningSystemInstances;
			bInSpawnPhase = true;

			//For now just do the concurrent tick for new systems on the GT here but we may also want to push this off to task threads too.
			FNiagaraSystemSimulationTickContext Context(this, DeltaSeconds, true, MyCompletionGraphEvent);
			Tick_Concurrent(Context);

			check(SystemInstances.Num() == DataSet.GetCurrentDataChecked().GetNumInstances());
			check(PausedSystemInstances.Num() == PausedInstanceData.GetCurrentDataChecked().GetNumInstances());
			check(SpawningSystemInstances.Num() == PendingSpawnDataSet.GetCurrentDataChecked().GetNumInstances());

			//Copy the sim data.
			PendingSpawnDataSet.CopyTo(DataSet, 0, INDEX_NONE, false);
			PendingSpawnDataSet.ResetBuffers();

			//Move new instances over to the main buffer and fix their indices.
			SystemInstances.Reserve(SystemInstances.Num() + SpawningSystemInstances.Num());
			for(FNiagaraSystemInstance* NewInst : SpawningSystemInstances)
			{
				checkSlow(!NewInst->IsComplete());
				NewInst->SystemInstanceIndex = SystemInstances.Add(NewInst);
			}

			check(SystemInstances.Num() == DataSet.GetCurrentDataChecked().GetNumInstances());
			check(PausedSystemInstances.Num() == PausedInstanceData.GetCurrentDataChecked().GetNumInstances());

			SpawningSystemInstances.Reset();
			bInSpawnPhase = false;
		}
	}
}

/** First phase of system sim tick. Must run on GameThread. */
void FNiagaraSystemSimulation::Tick_GameThread(float DeltaSeconds, const FGraphEventRef& MyCompletionGraphEvent)
{
	check(IsInGameThread());
	SCOPE_CYCLE_COUNTER(STAT_NiagaraOverview_GT);
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSystemSim_TickGT);
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(Niagara);
	LLM_SCOPE(ELLMTag::Niagara);

	check(SystemInstances.Num() == DataSet.GetCurrentDataChecked().GetNumInstances());
	check(PausedSystemInstances.Num() == PausedInstanceData.GetCurrentDataChecked().GetNumInstances());

	UNiagaraSystem* System = WeakSystem.Get();

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

	int32 SystemIndex = 0;
	while (SystemIndex < SystemInstances.Num())
	{
		FNiagaraSystemInstance* Inst = SystemInstances[SystemIndex];
		Inst->Tick_GameThread(DeltaSeconds);

		// TickDataInterfaces could remove the system so we only increment if the system has changed
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

	//Setup the few real constants like delta time.
	float InvDt = 1.0f / DeltaSeconds;

	SpawnTimeParam.SetValue(World->TimeSeconds);
	UpdateTimeParam.SetValue(World->TimeSeconds);
	SpawnDeltaTimeParam.SetValue(DeltaSeconds);
	UpdateDeltaTimeParam.SetValue(DeltaSeconds);
	SpawnInvDeltaTimeParam.SetValue(InvDt);
	UpdateInvDeltaTimeParam.SetValue(InvDt);
	SpawnNumSystemInstancesParam.SetValue(SystemInstances.Num());
	UpdateNumSystemInstancesParam.SetValue(SystemInstances.Num());

	FNiagaraSystemSimulationTickContext Context(this, DeltaSeconds, false, MyCompletionGraphEvent);

	//Now kick of the concurrent tick.
	if (Context.bTickAsync)
	{
		FGraphEventRef AsyncTask = TGraphTask<FNiagaraSystemSimulationTickTask>::CreateTask(nullptr, ENamedThreads::GameThread).ConstructAndDispatchWhenReady(Context);
		MyCompletionGraphEvent->SetGatherThreadForDontCompleteUntil(ENamedThreads::GameThread);
		MyCompletionGraphEvent->DontCompleteUntil(AsyncTask);
	}
	else
	{
		Tick_Concurrent(Context);
	}

	//TEMPORARY DEV CODE:
	//Test that the wait system is working
	if (GbForceNiagaraAsyncWait)
	{
		WaitForTickComplete();

		{
			//We spin a bit here to help make sure that all the Niagara work did infact complete before we get here. Makes things easier to see in Razor.
			SCOPE_CYCLE_COUNTER(STAT_NiagaraSystemSim_ForceWaitFakeStall);

			float CurrTimeMS = FPlatformTime::Seconds() * 1000;
			float WaitEnd = CurrTimeMS + GbWaitForAsyncStallMS;
			do
			{
				CurrTimeMS = FPlatformTime::Seconds() * 1000;
			} 
			while (CurrTimeMS < WaitEnd);
		}
	}
}

void FNiagaraSystemSimulation::WaitForTickComplete()
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSystemSim_ForceWaitForAsync);
	check(IsInGameThread());
	int32 SystemInstIndex = 0;
	while (SystemInstIndex < SystemInstances.Num())
	{
		FNiagaraSystemInstance* Inst = SystemInstances[SystemInstIndex];
		Inst->WaitForAsyncTick(bInSpawnPhase);//if we're in a spawn phase all existing instances should be complete already.
		if (!Inst->IsComplete())
		{
			//If the system completes during finalize it will be removed from instances so we don't update the index.
			++SystemInstIndex;
		}

		check(DataSet.GetCurrentDataChecked().GetNumInstances() == SystemInstances.Num());
	}

	if (bInSpawnPhase)
	{
		SystemInstIndex = 0;
		while (SystemInstIndex < SpawningSystemInstances.Num())
		{
			FNiagaraSystemInstance* Inst = SpawningSystemInstances[SystemInstIndex];
			Inst->WaitForAsyncTick();
			if (!Inst->IsComplete())
			{
				//If the system completes during finalize it will be removed from instances so we don't update the index.
				++SystemInstIndex;
			}

			check(PendingSpawnDataSet.GetCurrentDataChecked().GetNumInstances() == SpawningSystemInstances.Num());
		}
	}
}

void FNiagaraSystemSimulation::Tick_Concurrent(FNiagaraSystemSimulationTickContext& Context)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSystemSim_TickCNC);
	SCOPE_CYCLE_COUNTER(STAT_NiagaraOverview_GT_CNC);
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(Niagara);
	LLM_SCOPE(ELLMTag::Niagara);

	TArray<FNiagaraSystemInstance*>& Instances = Context.GetInstances();
	FNiagaraDataSet& SimulationDataSet = Context.GetDataSet();
	FNiagaraSystemInstance* SoloSystemInstance = bIsSolo && Instances.Num() == 1 ? Instances[0] : nullptr;

	if (bCanExecute && Instances.Num())
	{
		if (GbDumpSystemData || Context.System->bDumpDebugSystemInfo)
		{
			UE_LOG(LogNiagara, Log, TEXT("=========================================================="));
			UE_LOG(LogNiagara, Log, TEXT("Niagara System Sim Tick_Concurrent(): %s"), *Context.System->GetName());
			UE_LOG(LogNiagara, Log, TEXT("=========================================================="));
		}

		PrepareForSystemSimulate(Context);

		if (Context.bPendingSpawnPass)
		{
			SpawnSystemInstances(Context);
		}

		UpdateSystemInstances(Context);

		TransferSystemSimResults(Context);

		for (FNiagaraSystemInstance* Instance : Instances)
		{
			AddSystemToTickBatch(Instance, Context);
		}
		FlushTickBatch(Context);

		//If both the instances and the main sim are run on the GT then we need to finalize here.
		if (!Context.bTickAsync && !Context.bTickInstancesAsync)
		{
			check(IsInGameThread());
			int32 SystemInstIndex = 0;
			while (SystemInstIndex < Instances.Num())
			{
				FNiagaraSystemInstance* Inst = Instances[SystemInstIndex];
				checkSlow(!Inst->IsComplete());
				Inst->FinalizeTick_GameThread();
				if (!Inst->IsComplete())
				{
					//If the system completes during finalize it will be removed from instances so we don't update the index.
					++SystemInstIndex;
				}

				check(SimulationDataSet.GetCurrentDataChecked().GetNumInstances() == Instances.Num());
			}
		}

	#if WITH_EDITORONLY_DATA
		if (SoloSystemInstance)
		{
			SoloSystemInstance->FinishCapture();
		}
	#endif

		if (!Context.bPendingSpawnPass)
		{
			INC_DWORD_STAT_BY(STAT_NiagaraNumSystems, Instances.Num());
		}
	}
}

void FNiagaraSystemSimulation::PrepareForSystemSimulate(FNiagaraSystemSimulationTickContext& Context)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSystemSim_PrepareForSimulateCNC);

	TArray<FNiagaraSystemInstance*>& Instances = Context.GetInstances();
	int32 NumInstances = Instances.Num();

	if (NumInstances == 0)
	{
		return;
	}

	//Begin filling the state of the instance parameter datasets.
	SpawnInstanceParameterDataSet.BeginSimulate();
	UpdateInstanceParameterDataSet.BeginSimulate();

	SpawnInstanceParameterDataSet.Allocate(NumInstances);
	UpdateInstanceParameterDataSet.Allocate(NumInstances);

	for (int32 EmitterIdx = 0; EmitterIdx < Context.System->GetNumEmitters(); ++EmitterIdx)
	{
		EmitterExecutionStateAccessors[EmitterIdx].InitForAccess();
	}

	//Tick instance parameters and transfer any needed into the system simulation dataset.
	auto TransferInstanceParameters = [&](int32 SystemIndex)
	{
		FNiagaraSystemInstance* Inst = Instances[SystemIndex];

		//Inst->TickInstanceParameters(Context.DeltaSeconds);

		if (Inst->GetParameters().GetParametersDirty() && bCanExecute)
		{
			SpawnInstanceParameterToDataSetBinding.ParameterStoreToDataSet(Inst->GetParameters(), SpawnInstanceParameterDataSet, SystemIndex);
			UpdateInstanceParameterToDataSetBinding.ParameterStoreToDataSet(Inst->GetParameters(), UpdateInstanceParameterDataSet, SystemIndex);
		}

		//TODO: Find good way to check that we're not using any instance parameter data interfaces in the system scripts here.
		//In that case we need to solo and will never get here.

		TArray<TSharedRef<FNiagaraEmitterInstance, ESPMode::ThreadSafe>>& Emitters = Inst->GetEmitters();
		for (int32 EmitterIdx = 0; EmitterIdx < Emitters.Num(); ++EmitterIdx)
		{
			FNiagaraEmitterInstance& EmitterInst = Emitters[EmitterIdx].Get();
			if (EmitterExecutionStateAccessors.Num() > EmitterIdx && EmitterExecutionStateAccessors[EmitterIdx].IsValidForWrite())
			{
				EmitterExecutionStateAccessors[EmitterIdx].Set(SystemIndex, (int32)EmitterInst.GetExecutionState());
			}
		}
	};

	//This can go wide if we have a very large number of instances.
	//ParallelFor(Instances.Num(), TransferInstanceParameters, Instances.Num() < GbSystemSimTransferParamsParallelThreshold);
	ParallelFor(Instances.Num(), TransferInstanceParameters, true);

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

	TArray<FNiagaraSystemInstance*>& Instances = Context.GetInstances();
	FNiagaraDataSet& SimulationDataSet = Context.GetDataSet();
	int32 NumInstances = Instances.Num();
	
	check(Context.bPendingSpawnPass);

	if (NumInstances > 0)
	{

		FNiagaraSystemInstance* SoloSystemInstance = bIsSolo && Instances.Num() == 1 ? Instances[0] : nullptr;
		SimulationDataSet.BeginSimulate();
		SimulationDataSet.Allocate(NumInstances);
		SimulationDataSet.GetDestinationDataChecked().SetNumInstances(NumInstances);

		//Run Spawn
		SpawnExecContext.Tick(SoloSystemInstance);//We can't require a specific instance here as these are for all instances.
		SpawnExecContext.BindData(0, SimulationDataSet, 0, false);
		SpawnExecContext.BindData(1, SpawnInstanceParameterDataSet, 0, false);
		SpawnExecContext.Execute(NumInstances);

		if (GbDumpSystemData || Context.System->bDumpDebugSystemInfo)
		{
			UE_LOG(LogNiagara, Log, TEXT("=== Spwaned %d Systems ==="), NumInstances);
			SimulationDataSet.GetDestinationDataChecked().Dump(0, NumInstances, TEXT("System Dataset - Post Spawn"));
			SpawnInstanceParameterDataSet.GetCurrentDataChecked().Dump(0, NumInstances, TEXT("Spawn Instance Parameter Data"));
		}

		SimulationDataSet.EndSimulate();
	}

	check(SimulationDataSet.GetCurrentDataChecked().GetNumInstances() == Instances.Num());
}

void FNiagaraSystemSimulation::UpdateSystemInstances(FNiagaraSystemSimulationTickContext& Context)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSystemSim_UpdateCNC);

	TArray<FNiagaraSystemInstance*>& Instances = Context.GetInstances();
	FNiagaraDataSet& SimulationDataSet = Context.GetDataSet();
	int32 NumInstances = Instances.Num();

	if (NumInstances > 0)
	{
		if (Context.bPendingSpawnPass)
		{
			UpdateDeltaTimeParam.SetValue(0.0001f);
			UpdateInvDeltaTimeParam.SetValue(10000.0f);
		}


		FNiagaraSystemInstance* SoloSystemInstance = bIsSolo && Instances.Num() == 1 ? Instances[0] : nullptr;

		FNiagaraDataBuffer& DestinationData = SimulationDataSet.BeginSimulate();
		DestinationData.Allocate(NumInstances);
		DestinationData.SetNumInstances(NumInstances);

		//Run update.
		UpdateExecContext.Tick(Instances[0]);
		UpdateExecContext.BindData(0, SimulationDataSet, 0, false);
		UpdateExecContext.BindData(1, UpdateInstanceParameterDataSet, 0, false);
		UpdateExecContext.Execute(NumInstances);

		if (GbDumpSystemData || Context.System->bDumpDebugSystemInfo)
		{
			UE_LOG(LogNiagara, Log, TEXT("=== Updated %d Systems ==="), NumInstances);
			DestinationData.Dump(0, NumInstances, TEXT("System Data - Post Update"));
			UpdateInstanceParameterDataSet.GetCurrentDataChecked().Dump(0, NumInstances, TEXT("Update Instance Paramter Data"));
		}

		SimulationDataSet.EndSimulate();

#if WITH_EDITORONLY_DATA
		if (SoloSystemInstance && SoloSystemInstance->ShouldCaptureThisFrame())
		{
			TSharedPtr<struct FNiagaraScriptDebuggerInfo, ESPMode::ThreadSafe> DebugInfo;

			if (Context.bPendingSpawnPass)
			{
				//We say this update phase is the out put of the SpawnScript but it's really just the initial post spwan state of the particles. We may want to alter the UI to reflect this?
				SoloSystemInstance->GetActiveCaptureWrite(NAME_None, ENiagaraScriptUsage::SystemSpawnScript, FGuid());
			}
			else
			{
				SoloSystemInstance->GetActiveCaptureWrite(NAME_None, ENiagaraScriptUsage::SystemUpdateScript, FGuid());
			}

			if (DebugInfo)
			{
				SimulationDataSet.CopyTo(DebugInfo->Frame);
				DebugInfo->Parameters = UpdateExecContext.Parameters;
				DebugInfo->bWritten = true;
			}
		}
#endif
	}

	check(SimulationDataSet.GetCurrentDataChecked().GetNumInstances() == Instances.Num());
}

void FNiagaraSystemSimulation::TransferSystemSimResults(FNiagaraSystemSimulationTickContext& Context)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraSystemSim_TransferResultsCNC);

	TArray<FNiagaraSystemInstance*>& Instances = Context.GetInstances();
	if (Instances.Num() == 0)
	{
		return;
	}

	FNiagaraDataSet& SimulationDataSet = Context.GetDataSet();
	SystemExecutionStateAccessor.SetDataSet(SimulationDataSet);
	SystemExecutionStateAccessor.InitForAccess();
	for (int32 EmitterIdx = 0; EmitterIdx < Context.System->GetNumEmitters(); ++EmitterIdx)
	{
		EmitterExecutionStateAccessors[EmitterIdx].SetDataSet(SimulationDataSet);
		EmitterExecutionStateAccessors[EmitterIdx].InitForAccess();
		for (int32 SpawnInfoIdx = 0; SpawnInfoIdx < EmitterSpawnInfoAccessors[EmitterIdx].Num(); ++SpawnInfoIdx)
		{
			EmitterSpawnInfoAccessors[EmitterIdx][SpawnInfoIdx].SetDataSet(SimulationDataSet);
			EmitterSpawnInfoAccessors[EmitterIdx][SpawnInfoIdx].InitForAccess();
		}
	}

	for (int32 SystemIndex = 0; SystemIndex < Instances.Num(); ++SystemIndex)
	{
		ENiagaraExecutionState ExecutionState = (ENiagaraExecutionState)SystemExecutionStateAccessor.GetSafe(SystemIndex, (int32)ENiagaraExecutionState::Disabled);
		FNiagaraSystemInstance* SystemInst = Instances[SystemIndex];

		//Apply the systems requested execution state to it's actual execution state.
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

				//TODO: Any other fixed function stuff like this?

				FNiagaraScriptExecutionContext& SpawnContext = EmitterInst.GetSpawnExecutionContext();
				DataSetToEmitterSpawnParameters[EmitterIdx].DataSetToParameterStore(SpawnContext.Parameters, SimulationDataSet, SystemIndex);

				FNiagaraScriptExecutionContext& UpdateContext = EmitterInst.GetUpdateExecutionContext();
				DataSetToEmitterUpdateParameters[EmitterIdx].DataSetToParameterStore(UpdateContext.Parameters, SimulationDataSet, SystemIndex);

				TArray<FNiagaraScriptExecutionContext>& EventContexts = EmitterInst.GetEventExecutionContexts();
				for (int32 EventIdx = 0; EventIdx < EventContexts.Num(); ++EventIdx)
				{
					FNiagaraScriptExecutionContext& EventContext = EventContexts[EventIdx];
					if (DataSetToEmitterEventParameters[EmitterIdx].Num() > EventIdx)
					{
						DataSetToEmitterEventParameters[EmitterIdx][EventIdx].DataSetToParameterStore(EventContext.Parameters, SimulationDataSet, SystemIndex);
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

	check(SystemInstances.Num() == DataSet.GetCurrentDataChecked().GetNumInstances());
	check(PausedSystemInstances.Num() == PausedInstanceData.GetCurrentDataChecked().GetNumInstances());

	check(IsInGameThread());
	check(DataSet.GetDestinationData() == nullptr);
	UNiagaraSystem* System = WeakSystem.Get();
	if (Instance->IsPendingSpawn())
	{
		if (GbDumpSystemData || (System && System->bDumpDebugSystemInfo))
		{
			UE_LOG(LogNiagara, Log, TEXT("=== Removing Pending Spawn %d ==="), Instance->SystemInstanceIndex);
			DataSet.GetCurrentDataChecked().Dump(Instance->SystemInstanceIndex, 1, TEXT("System data being removed."));
		}

		//If we're currently spawning then all our pending spawn instances will be inside
		TArray<FNiagaraSystemInstance*>& Instances = bInSpawnPhase ? SpawningSystemInstances : PendingSystemInstances;

		int32 SystemIndex = Instance->SystemInstanceIndex;
		check(Instances.IsValidIndex(SystemIndex));
		check(Instance == Instances[SystemIndex]);
		
		if (bInSpawnPhase)
		{
			//We should have already spawned this into our dataset so need to kill it.
			PendingSpawnDataSet.GetCurrentDataChecked().KillInstance(SystemIndex);
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
			DataSet.GetCurrentDataChecked().Dump(Instance->SystemInstanceIndex, 1, TEXT("System data being removed."));
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

		check(SystemInstances.Num() == DataSet.GetCurrentDataChecked().GetNumInstances());
		check(PausedSystemInstances.Num() == PausedInstanceData.GetCurrentDataChecked().GetNumInstances());
	}
	else if (SystemInstances.IsValidIndex(Instance->SystemInstanceIndex))
	{
		if (GbDumpSystemData || (System && System->bDumpDebugSystemInfo))
		{
			UE_LOG(LogNiagara, Log, TEXT("=== Removing System %d ==="), Instance->SystemInstanceIndex);
			DataSet.GetCurrentDataChecked().Dump(Instance->SystemInstanceIndex, 1, TEXT("System data being removed."));
		}

		int32 NumInstances = DataSet.GetCurrentDataChecked().GetNumInstances();
		check(SystemInstances.Num() == NumInstances);

		int32 SystemIndex = Instance->SystemInstanceIndex;
		check(Instance == SystemInstances[SystemIndex]);
		check(SystemInstances.IsValidIndex(SystemIndex));

		DataSet.GetCurrentDataChecked().KillInstance(SystemIndex);
		SystemInstances.RemoveAtSwap(SystemIndex);
		Instance->SystemInstanceIndex = INDEX_NONE;
		if (SystemInstances.IsValidIndex(SystemIndex))
		{
			SystemInstances[SystemIndex]->SystemInstanceIndex = SystemIndex;
		}

		check(SystemInstances.Num() == DataSet.GetCurrentDataChecked().GetNumInstances());
		check(PausedSystemInstances.Num() == PausedInstanceData.GetCurrentDataChecked().GetNumInstances());
	}

#if NIAGARA_NAN_CHECKING
	DataSet.CheckForNaNs();
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
		//DataSet.Dump(true, Instance->SystemInstanceIndex, 1);
	}

	check(SystemInstances.Num() == DataSet.GetCurrentDataChecked().GetNumInstances());
	check(PausedSystemInstances.Num() == PausedInstanceData.GetCurrentDataChecked().GetNumInstances());
}

void FNiagaraSystemSimulation::PauseInstance(FNiagaraSystemInstance* Instance)
{
	check(IsInGameThread());
	check(!Instance->IsPaused());
	check(!DataSet.GetDestinationData());
	check(!PausedInstanceData.GetDestinationData());

	check(SystemInstances.Num() == DataSet.GetCurrentDataChecked().GetNumInstances());
	check(PausedSystemInstances.Num() == PausedInstanceData.GetCurrentDataChecked().GetNumInstances());

	UNiagaraSystem* System = WeakSystem.Get();
	if (Instance->IsPendingSpawn())
	{
		if (GbDumpSystemData || (System && System->bDumpDebugSystemInfo))
		{
			UE_LOG(LogNiagara, Log, TEXT("=== Pausing Pending Spawn %d ==="), Instance->SystemInstanceIndex);
			//DataSet.Dump(true, Instance->SystemInstanceIndex, 1);
		}
		//Nothing to do for pending spawn systems.
		check(PendingSystemInstances[Instance->SystemInstanceIndex] == Instance);
		return;
	}

	if (GbDumpSystemData || (System && System->bDumpDebugSystemInfo))
	{
		UE_LOG(LogNiagara, Log, TEXT("=== Pausing System %d ==="), Instance->SystemInstanceIndex);
		DataSet.GetCurrentDataChecked().Dump(Instance->SystemInstanceIndex, 1, TEXT("System data being paused."));
	}

	int32 SystemIndex = Instance->SystemInstanceIndex;
	check(SystemInstances.IsValidIndex(SystemIndex));
	check(Instance == SystemInstances[SystemIndex]);

	int32 NewDataSetIndex = PausedInstanceData.GetCurrentDataChecked().TransferInstance(DataSet.GetCurrentDataChecked(), SystemIndex);

	Instance->SystemInstanceIndex = PausedSystemInstances.Add(Instance);

	check(NewDataSetIndex == Instance->SystemInstanceIndex);

	SystemInstances.RemoveAtSwap(SystemIndex);
	if (SystemInstances.IsValidIndex(SystemIndex))
	{
		SystemInstances[SystemIndex]->SystemInstanceIndex = SystemIndex;
	}

	check(SystemInstances.Num() == DataSet.GetCurrentDataChecked().GetNumInstances());
	check(PausedSystemInstances.Num() == PausedInstanceData.GetCurrentDataChecked().GetNumInstances());
}

void FNiagaraSystemSimulation::UnpauseInstance(FNiagaraSystemInstance* Instance)
{
	check(IsInGameThread());
	check(Instance->IsPaused());
	check(!DataSet.GetDestinationData());
	check(!PausedInstanceData.GetDestinationData());

	check(SystemInstances.Num() == DataSet.GetCurrentDataChecked().GetNumInstances());
	check(PausedSystemInstances.Num() == PausedInstanceData.GetCurrentDataChecked().GetNumInstances());

	UNiagaraSystem* System = WeakSystem.Get();
	if (Instance->IsPendingSpawn())
	{
		if (GbDumpSystemData || (System && System->bDumpDebugSystemInfo))
		{
			UE_LOG(LogNiagara, Log, TEXT("=== Unpausing Pending Spawn %d ==="), Instance->SystemInstanceIndex);
			//DataSet.Dump(true, Instance->SystemInstanceIndex, 1);
		}
		//Nothing to do for pending spawn systems.
		check(PendingSystemInstances[Instance->SystemInstanceIndex] == Instance);
		return;
	}

	if (GbDumpSystemData || (System && System->bDumpDebugSystemInfo))
	{
		UE_LOG(LogNiagara, Log, TEXT("=== Unpausing System %d ==="), Instance->SystemInstanceIndex);
		DataSet.GetCurrentDataChecked().Dump(Instance->SystemInstanceIndex, 1, TEXT("System data being unpaused."));
	}

	int32 SystemIndex = Instance->SystemInstanceIndex;
	check(PausedSystemInstances.IsValidIndex(SystemIndex));
	check(Instance == PausedSystemInstances[SystemIndex]);

	int32 NewDataSetIndex = DataSet.GetCurrentDataChecked().TransferInstance(PausedInstanceData.GetCurrentDataChecked(), SystemIndex);

	Instance->SystemInstanceIndex = SystemInstances.Add(Instance);
	check(NewDataSetIndex == Instance->SystemInstanceIndex);

	PausedSystemInstances.RemoveAtSwap(SystemIndex);
	if (PausedSystemInstances.IsValidIndex(SystemIndex))
	{
		PausedSystemInstances[SystemIndex]->SystemInstanceIndex = SystemIndex;
	}

	check(SystemInstances.Num() == DataSet.GetCurrentDataChecked().GetNumInstances());
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
			DataSetToEmitterSpawnParameters[EmitterIdx].Init(DataSet, SpawnContext.Parameters);

			FNiagaraScriptExecutionContext& UpdateContext = EmitterInst.GetUpdateExecutionContext();
			DataSetToEmitterUpdateParameters[EmitterIdx].Init(DataSet, UpdateContext.Parameters);

			TArray<FNiagaraScriptExecutionContext>& EventContexts = EmitterInst.GetEventExecutionContexts();
			DataSetToEmitterEventParameters[EmitterIdx].SetNum(EventContexts.Num());
			for (int32 EventIdx = 0; EventIdx < EventContexts.Num(); ++EventIdx)
			{
				FNiagaraScriptExecutionContext& EventContext = EventContexts[EventIdx];
				DataSetToEmitterEventParameters[EmitterIdx][EventIdx].Init(DataSet, EventContext.Parameters);
			}
		}
	}
}

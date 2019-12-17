// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ChaosSolversModule.h"
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

#include "ChaosStats.h"
#include "ChaosLog.h"
#include "HAL/PlatformProcess.h"
#include "Framework/PersistentTask.h"
#include "Misc/CoreDelegates.h"
#include "HAL/IConsoleManager.h"
#include "PhysicsSolver.h"
#include "FramePro/FramePro.h"
#include "Chaos/BoundingVolume.h"
#include "Chaos/PBDRigidParticles.h"
#include "Chaos/UniformGrid.h"
#include "UObject/Class.h"
#include "Framework/Dispatcher.h"
#include "Framework/DispatcherImpl.h"
#include "Misc/App.h"
#include "Chaos/PhysicalMaterials.h"

TAutoConsoleVariable<int32> CVarChaosThreadEnabled(
	TEXT("p.Chaos.DedicatedThreadEnabled"),
	1,
	TEXT("Enables a dedicated physics task/thread for Chaos tasks.")
	TEXT("0: Disabled")
	TEXT("1: Enabled"));

TAutoConsoleVariable<float> CVarDedicatedThreadDesiredHz(
	TEXT("p.Chaos.Thread.DesiredHz"),
	60.0f,
	TEXT("Desired update rate of the dedicated physics thread in Hz/FPS (Default 60.0f)"));

TAutoConsoleVariable<int32> CVarDedicatedThreadSyncThreshold(
	TEXT("p.Chaos.Thread.WaitThreshold"),
	0,
	TEXT("Desired wait time in ms before the game thread stops waiting to sync physics and just takes the last result. (default 16ms)")
);

namespace Chaos
{
#if !UE_BUILD_SHIPPING
	CHAOS_API extern bool bPendingHierarchyDump;
#endif

	void ChangeThreadingMode(EThreadingMode InMode)
	{
		FChaosSolversModule* ChaosModule = FModuleManager::Get().GetModulePtr<FChaosSolversModule>("ChaosSolvers");

		ChaosModule->ChangeThreadingMode(InMode);
	}

	void ChangeBufferingMode()
	{
		FChaosSolversModule* ChaosModule = FModuleManager::Get().GetModulePtr<FChaosSolversModule>("ChaosSolvers");

		EMultiBufferMode MultiBufferMode = ChaosModule->GetDesiredBufferingMode();

		ChaosModule->ChangeBufferMode(MultiBufferMode);
	}

	namespace ConsoleCommands
	{
		void ThreadingModel(const TArray<FString>& InParams)
		{
			FChaosSolversModule* ChaosModule = FModuleManager::Get().GetModulePtr<FChaosSolversModule>("ChaosSolvers");

			if(!ChaosModule)
			{
				UE_LOG(LogChaos, Error, TEXT("ChaosSolvers module is not loaded, cannot change threading model"));
				return;
			}

			if(!ChaosModule->GetDispatcher())
			{
				UE_LOG(LogChaos, Error, TEXT("ChaosSolvers module has no dispatcher, cannot change threading model"));
				return;
			}

			if(InParams.Num() == 0)
			{
				// Need a model name
				UE_LOG(LogChaos, Error, TEXT("Invalid usage: p.Chaos.ThreadingModel <ModelName>"));
				return;
			}

			EThreadingMode NewMode;
			LexFromString(NewMode, *InParams[0]);

			ChangeThreadingMode(NewMode);
		}

		FAutoConsoleCommand ThreadingModelCommand(TEXT("p.Chaos.ThreadingModel"), TEXT("Controls the current threading model. See Chaos::DispatcherMode for accepted mode names"), FConsoleCommandWithArgsDelegate::CreateStatic(&ThreadingModel));
	}

	Chaos::EThreadingMode FInternalDefaultSettings::GetDefaultThreadingMode() const
	{
		return Chaos::EThreadingMode::TaskGraph;
	}

	EChaosSolverTickMode FInternalDefaultSettings::GetDedicatedThreadTickMode() const
	{
		return EChaosSolverTickMode::VariableCappedWithTarget;
	}

	EChaosBufferMode FInternalDefaultSettings::GetDedicatedThreadBufferMode() const
	{
		return EChaosBufferMode::Double;
	}

	FInternalDefaultSettings GDefaultChaosSettings;
}

static FAutoConsoleVariableSink CVarChaosModuleSink(FConsoleCommandDelegate::CreateStatic(&FChaosConsoleSinks::OnCVarsChanged));

void FChaosConsoleSinks::OnCVarsChanged()
{
	// #BG TODO - Currently this isn't dynamic, should be made to be switchable.
	FChaosSolversModule* ChaosModule = FModuleManager::Get().GetModulePtr<FChaosSolversModule>("ChaosSolvers");

	if(ChaosModule)
	{
		if(ChaosModule->IsPersistentTaskRunning())
		{
			float NewHz = CVarDedicatedThreadDesiredHz.GetValueOnGameThread();
			ChaosModule->GetDispatcher()->EnqueueCommandImmediate([NewHz](Chaos::FPersistentPhysicsTask* Thread)
			{
				if(Thread)
				{
					Thread->SetTargetDt(1.0f / NewHz);
				}
			});
		}
	}
}

FSolverStateStorage::FSolverStateStorage()
	: Solver(nullptr)
{

}

FChaosSolversModule* FChaosSolversModule::GetModule()
{
	static FChaosSolversModule* Instance = nullptr;

	if(!Instance)
	{
		Instance = FModuleManager::Get().LoadModulePtr<FChaosSolversModule>("ChaosSolvers");
	}

	return Instance;
}

FChaosSolversModule::FChaosSolversModule()
	: SolverActorClassProvider(nullptr)
	, SettingsProvider(nullptr)
	, bPersistentTaskSpawned(false)
	, PhysicsAsyncTask(nullptr)
	, PhysicsInnerTask(nullptr)
	, Dispatcher(nullptr)
	, SolverActorClass(nullptr)
	, SolverActorRequiredBaseClass(nullptr)
#if STATS
	, AverageUpdateTime(0.0f)
	, TotalAverageUpdateTime(0.0f)
	, Fps(0.0f)
	, EffectiveFps(0.0f)
#endif
#if WITH_EDITOR
	, bPauseSolvers(false)
	, SingleStepCounter(0)
#endif
	, bModuleInitialized(false)
{
#if WITH_EDITOR
	if(!IsRunningGame())
	{
		// In the editor we begin with everything paused so we don't needlessly tick
		// the physics solvers until PIE begins. Delegates are bound in FPhysScene_ChaosPauseHandler
		// to handle editor world transitions. In games and -game we want to just let them tick
		bPauseSolvers = true;
	}
#endif
}

void FChaosSolversModule::StartupModule()
{
	// Load dependent modules if we can
	if(FModuleManager::Get().ModuleExists(TEXT("FieldSystemEngine")))
	{
		FModuleManager::Get().LoadModule("FieldSystemEngine");
	}
	Initialize();
}

void FChaosSolversModule::ShutdownModule()
{
	Shutdown();

	FCoreDelegates::OnPreExit.RemoveAll(this);
}

void FChaosSolversModule::Initialize()
{
	if(!bModuleInitialized)
	{
		const Chaos::EThreadingMode DefaultThreadingMode = GetDesiredThreadingMode();

		switch(DefaultThreadingMode)
		{
		case Chaos::EThreadingMode::DedicatedThread:
			StartPhysicsTask();
			break;
		case Chaos::EThreadingMode::SingleThread:
			Dispatcher = new Chaos::FDispatcher<Chaos::EThreadingMode::SingleThread>(this);
			break;
		case Chaos::EThreadingMode::TaskGraph:
			Dispatcher = new Chaos::FDispatcher<Chaos::EThreadingMode::TaskGraph>(this);
			break;

		default:
			// Must have a dispatcher! Add handling for new threading models above
			check(false);
			break;
		}

		// Bind to the material manager
		Chaos::FPhysicalMaterialManager& MaterialManager = Chaos::FPhysicalMaterialManager::Get();
		OnCreateMaterialHandle = MaterialManager.OnMaterialCreated.Add(Chaos::FMaterialCreatedDelegate::CreateRaw(this, &FChaosSolversModule::OnCreateMaterial));
		OnDestroyMaterialHandle = MaterialManager.OnMaterialDestroyed.Add(Chaos::FMaterialDestroyedDelegate::CreateRaw(this, &FChaosSolversModule::OnDestroyMaterial));
		OnUpdateMaterialHandle = MaterialManager.OnMaterialUpdated.Add(Chaos::FMaterialUpdatedDelegate::CreateRaw(this, &FChaosSolversModule::OnUpdateMaterial));

		bModuleInitialized = true;
	}
}

void FChaosSolversModule::Shutdown()
{
	if(bModuleInitialized)
	{
		EndPhysicsTask();

		// Unbind material events
		Chaos::FPhysicalMaterialManager& MaterialManager = Chaos::FPhysicalMaterialManager::Get();
		MaterialManager.OnMaterialCreated.Remove(OnCreateMaterialHandle);
		MaterialManager.OnMaterialDestroyed.Remove(OnDestroyMaterialHandle);
		MaterialManager.OnMaterialUpdated.Remove(OnUpdateMaterialHandle);

		bModuleInitialized = false;
	}
}

void FChaosSolversModule::OnSettingsChanged()
{
	Chaos::EThreadingMode CurrentThreadMode = GetDesiredThreadingMode();

	if(Dispatcher && CurrentThreadMode != Dispatcher->GetMode())
	{
		Chaos::ChangeThreadingMode(CurrentThreadMode);
	}

	// buffer mode switching depends on current threading mode and on EChaosBufferMode property setting
	Chaos::ChangeBufferingMode();
}

void FChaosSolversModule::ShutdownThreadingMode()
{
	using namespace Chaos;

	if(!Dispatcher)
	{
		return;
	}

	Chaos::EThreadingMode CurrentMode = Dispatcher->GetMode();

	switch(CurrentMode)
	{
	case EThreadingMode::DedicatedThread:
	{
		ensure(IsPersistentTaskRunning());

		EndPhysicsTask();
	}
	break;
	
	case EThreadingMode::TaskGraph:
	{
		if(Dispatcher)
		{
			// we need to flush out any commands currently waiting in the taskgraph dispatcher.
			// Dedicated will wait for execution to end, and single thread runs immediately so we only
			// need to handle this for task graph dispatchers
			Dispatcher->Execute();

			for(FPBDRigidsSolver* Solver : Solvers)
			{
				IDispatcher::FSolverCommand Command;
				while(Solver->CommandQueue.Dequeue(Command))
				{
					Command(Solver);
				}
			}

			delete Dispatcher;
			Dispatcher = nullptr;
		}
	}
	break;

	case EThreadingMode::SingleThread:
	{
		if(Dispatcher)
		{
			delete Dispatcher;
			Dispatcher = nullptr;
		}
	}
	break;

	default:
		break;
	}
}

void FChaosSolversModule::InitializeThreadingMode(Chaos::EThreadingMode InNewMode)
{
	using namespace Chaos;

	// Check we're not trying to initialize without shutting down the threading mode first
	check(!Dispatcher);

	switch(InNewMode)
	{
	case EThreadingMode::DedicatedThread:
	{
		StartPhysicsTask();
	}
	break;

	case EThreadingMode::SingleThread:
	{
		Dispatcher = new FDispatcher<Chaos::EThreadingMode::SingleThread>(this);
	}
	break;

	case EThreadingMode::TaskGraph:
	{
		Dispatcher = new FDispatcher<Chaos::EThreadingMode::TaskGraph>(this);
	}
	break;

	default:
		break;
	}
}

void FChaosSolversModule::ChangeThreadingMode(Chaos::EThreadingMode InNewMode)
{
	EChaosThreadingMode CurrentMode = GetDispatcher()->GetMode();

	if(InNewMode != EChaosThreadingMode::Invalid && InNewMode != CurrentMode)
	{
		// Handle shutdown of current threading model
		ShutdownThreadingMode();

		// Handle entering new threading model
		InitializeThreadingMode(InNewMode);

		// Buffering mode may change when the threading mode changes
		//ChangeBufferingMode();
	}
}

bool FChaosSolversModule::IsPersistentTaskEnabled() const
{
	return CVarChaosThreadEnabled.GetValueOnGameThread() == 1;
}

bool FChaosSolversModule::IsPersistentTaskRunning() const
{
	return bPersistentTaskSpawned;
}

void FChaosSolversModule::StartPhysicsTask()
{
	// Create the dispatcher
	if(Dispatcher)
	{
		delete Dispatcher;
		Dispatcher = nullptr;
	}

	Dispatcher = new Chaos::FDispatcher<Chaos::EThreadingMode::DedicatedThread>(this);

	// Setup the physics thread (Cast the dispatcher out to the correct type for threaded work)
	const float SafeFps = FMath::Clamp(CVarDedicatedThreadDesiredHz.GetValueOnGameThread(), 5.0f, 1000.0f);
	PhysicsAsyncTask = new FAsyncTask<Chaos::FPersistentPhysicsTask>(1.0f / SafeFps, false, (Chaos::FDispatcher<Chaos::EThreadingMode::DedicatedThread>*)Dispatcher);
	PhysicsInnerTask = &PhysicsAsyncTask->GetTask();
	PhysicsAsyncTask->StartBackgroundTask();
	bPersistentTaskSpawned = true;

	PreExitHandle = FCoreDelegates::OnPreExit.AddRaw(this, &FChaosSolversModule::EndPhysicsTask);
}

void FChaosSolversModule::EndPhysicsTask()
{
	// Pull down the thread if it exists
	if(PhysicsInnerTask)
	{
		// Ask the physics thread to stop
		PhysicsInnerTask->RequestShutdown();
		// Wait for the stop
		PhysicsInnerTask->GetShutdownEvent()->Wait();
		PhysicsInnerTask = nullptr;
		// Wait for the actual task to complete so we can get rid of it, then delete
		PhysicsAsyncTask->EnsureCompletion(false);
		delete PhysicsAsyncTask;
		PhysicsAsyncTask = nullptr;

		bPersistentTaskSpawned = false;

		FCoreDelegates::OnPreExit.Remove(PreExitHandle);
	}

	// Destroy the dispatcher
	if(Dispatcher)
	{
		delete Dispatcher;
		Dispatcher = nullptr;
	}

}

Chaos::IDispatcher* FChaosSolversModule::GetDispatcher() const
{
	return Dispatcher;
}

Chaos::FPersistentPhysicsTask* FChaosSolversModule::GetDedicatedTask() const
{
	return PhysicsInnerTask;
}

void FChaosSolversModule::SyncTask(bool bForceBlockingSync /*= false*/)
{
	// Hard lock the physics thread before syncing our data
	FChaosScopedPhysicsThreadLock ScopeLock(bForceBlockingSync ? MAX_uint32 : (uint32)(CVarDedicatedThreadSyncThreshold.GetValueOnGameThread()));

	// This will either get the results because physics finished, or fall back on whatever physics last gave us
	// to allow the game thread to continue on without stalling.
	PhysicsInnerTask->SyncProxiesFromCache(ScopeLock.DidGetLock());

	// Update stats if necessary
	UpdateStats();
}

Chaos::FPhysicsSolver* FChaosSolversModule::CreateSolver(bool bStandalone /*= false*/
#if CHAOS_CHECKED
	, const FName& DebugName
#endif
)
{
	FChaosScopeSolverLock SolverScopeLock;
	
	Chaos::EMultiBufferMode SolverBufferMode = Chaos::EMultiBufferMode::Single;
	if (GetDispatcher())
	{
		SolverBufferMode = GetDesiredBufferingMode();
	}

	Solvers.Add(new Chaos::FPhysicsSolver(SolverBufferMode));
	Chaos::FPhysicsSolver* NewSolver = Solvers.Last();

#if CHAOS_CHECKED
    // Add solver nubmer to solver name
	const FName NewDebugName = *FString::Printf(TEXT("%s (%d)"), DebugName == NAME_None ? TEXT("Solver") : *DebugName.ToString(), Solvers.Num() - 1);
	NewSolver->SetDebugName(NewDebugName);
#endif

	// Set up the material lists on the new solver, copying from the current master list
	{
		Chaos::FPhysicalMaterialManager& Manager =	Chaos::FPhysicalMaterialManager::Get();
		NewSolver->QueryMaterialLock.WriteLock();
		NewSolver->QueryMaterials = Manager.GetMasterMaterials();
		NewSolver->SimMaterials = Manager.GetMasterMaterials();
		NewSolver->QueryMaterialLock.WriteUnlock();
	}

	if(!bStandalone && IsPersistentTaskRunning() && Dispatcher)
	{
		// Need to let the thread know there's a new solver to care about
		Dispatcher->EnqueueCommandImmediate([NewSolver](Chaos::FPersistentPhysicsTask* PhysThread)
		{
			PhysThread->AddSolver(NewSolver);
		});
	}

	return NewSolver;
}

UClass* FChaosSolversModule::GetSolverActorClass() const
{
	check(SolverActorClassProvider);
	return SolverActorClassProvider->GetSolverActorClass();
}

bool FChaosSolversModule::IsValidSolverActorClass(UClass* Class) const
{
	return Class->IsChildOf(SolverActorRequiredBaseClass);
}

void FChaosSolversModule::SetDedicatedThreadTickMode(EChaosSolverTickMode InTickMode)
{
	check(Dispatcher);

	Dispatcher->EnqueueCommandImmediate([InTickMode](Chaos::FPersistentPhysicsTask* InThread)
	{
		if(InThread)
		{
			InThread->SetTickMode(InTickMode);
		}
	});
}

void FChaosSolversModule::DestroySolver(Chaos::FPhysicsSolver* InSolver)
{
	FChaosScopeSolverLock SolverScopeLock;

	if(Solvers.Remove(InSolver) > 0)
	{
		if(Dispatcher)
		{
			Dispatcher->EnqueueCommandImmediate([InSolver](Chaos::FPersistentPhysicsTask* PhysThread)
			{
				if(PhysThread)
				{
					PhysThread->RemoveSolver(InSolver);
				}
				delete InSolver;
			});
		}
		else
		{
			delete InSolver;
		}
	}
	else if(InSolver)
	{
		UE_LOG(LogChaosGeneral, Warning, TEXT("Passed valid solver state to DestroySolverState but it wasn't in the solver storage list! Make sure it was created using the Chaos module."));
	}
}

TAutoConsoleVariable<FString> DumpHier_ElementBuckets(
	TEXT("p.Chaos.DumpHierElementBuckets"),
	TEXT("1,4,8,16,32,64,128,256,512"),
	TEXT("Distribution buckets for dump hierarchy stats command"));

void FChaosSolversModule::DumpHierarchyStats(int32* OutOptMaxCellElements)
{
	TArray<FString> BucketStrings;
	DumpHier_ElementBuckets.GetValueOnGameThread().ParseIntoArray(BucketStrings, TEXT(","));
	
	// 2 extra for the 0 bucket at the start and the larger bucket at the end
	const int32 NumBuckets = BucketStrings.Num() + 2;
	TArray<int32> BucketSizes;
	BucketSizes.AddZeroed(NumBuckets);
	BucketSizes.Last() = MAX_int32;

	for(int32 BucketIndex = 1; BucketIndex < NumBuckets - 1; ++BucketIndex)
	{
		BucketSizes[BucketIndex] = FCString::Atoi(*BucketStrings[BucketIndex - 1]);
	}
	BucketSizes.Sort();

	TArray<int32> BucketCounts;
	BucketCounts.AddZeroed(NumBuckets);

	const int32 NumSolvers = Solvers.Num();
	for(int32 SolverIndex = 0; SolverIndex < NumSolvers; ++SolverIndex)
	{
		Chaos::FPhysicsSolver* Solver = Solvers[SolverIndex];
#if TODO_REIMPLEMENT_SPATIAL_ACCELERATION_ACCESS
		if(const Chaos::ISpatialAcceleration<float,3>* SpatialAcceleration = Solver->GetSpatialAcceleration())
		{
#if !UE_BUILD_SHIPPING
			SpatialAcceleration->DumpStats();
#endif
			Solver->ReleaseSpatialAcceleration();
		}
#endif
#if 0

		const TArray<Chaos::TBox<float, 3>>& Boxes = Hierarchy->GetWorldSpaceBoxes();

		if(Boxes.Num() > 0)
		{
			FString OutputString = TEXT("\n\n");
			OutputString += FString::Printf(TEXT("Solver %d - Hierarchy Stats\n"));

			const Chaos::TUniformGrid<float, 3>& Grid = Hierarchy->GetGrid();

			const int32 NumCells = Grid.GetNumCells();
			const FVector Min = Grid.MinCorner();
			const FVector Max = Grid.MaxCorner();
			const FVector Extent = Max - Min;

			OutputString += FString::Printf(TEXT("Grid:\n\tCells: [%d, %d, %d] (%d)\n\tMin: %s\n\tMax: %s\n\tExtent: %s\n"),
				Grid.Counts()[0],
				Grid.Counts()[1],
				Grid.Counts()[2],
				NumCells,
				*Min.ToString(),
				*Max.ToString(),
				*Extent.ToString()
				);

			int32 CellsL0 = 0;
			int32 TotalElems = 0;
			int32 MaxElements = 0;
			const int32 NumHeirElems = Hierarchy->GetElements().Num();
			for(int32 ElemIndex = 0; ElemIndex < NumHeirElems; ++ElemIndex)
			{
				const TArray<int32>& CellElems = Hierarchy->GetElements()[ElemIndex];

				const int32 NumCellEntries = CellElems.Num();

				if(NumCellEntries > 0)
				{
					++CellsL0;
				}

				if(NumCellEntries > MaxElements)
				{
					MaxElements = NumCellEntries;
				}

				TotalElems += NumCellEntries;

				for(int32 BucketIndex = 1; BucketIndex < NumBuckets; ++BucketIndex)
				{
					if(NumCellEntries >= BucketSizes[BucketIndex - 1] && NumCellEntries < BucketSizes[BucketIndex])
					{
						BucketCounts[BucketIndex]++;
						break;
					}
				}
			}

			if(OutOptMaxCellElements)
			{
				(*OutOptMaxCellElements) = MaxElements;
			}

			const float AveragePopulatedCount = (float)TotalElems / (float)CellsL0;

			OutputString += FString::Printf(TEXT("\n\tL0: %d\n\tAvg elements per populated cell: %.5f\n\tTotal elems: %d"),
				CellsL0,
				AveragePopulatedCount,
				TotalElems);

			int32 MaxBucketCount = 0;
			for(int32 Count : BucketCounts)
			{
				if(Count > MaxBucketCount)
				{
					MaxBucketCount = Count;
				}
			}

			const int32 MaxChars = 20;
			const float CountPerCharacter = (float)MaxBucketCount / (float)MaxChars;

			OutputString += TEXT("\n\nElement Count Distribution:\n");

			for(int32 BucketIndex = 1; BucketIndex < NumBuckets; ++BucketIndex)
			{
				const int32 NumChars = (float)BucketCounts[BucketIndex] / (float)CountPerCharacter;

				if(BucketIndex < (NumBuckets - 1))
				{
					OutputString += FString::Printf(TEXT("\t[%4d - %4d) (%4d) |"), BucketSizes[BucketIndex - 1], BucketSizes[BucketIndex], BucketCounts[BucketIndex]);
				}
				else
				{
					OutputString += FString::Printf(TEXT("\t[%4d -  inf) (%4d) |"), BucketSizes[BucketIndex - 1], BucketCounts[BucketIndex]);
				}

				for(int32 CharIndex = 0; CharIndex < NumChars; ++CharIndex)
				{
					OutputString += TEXT("-");
				}

				OutputString += TEXT("\n");
			}

			OutputString += TEXT("\n--------------------------------------------------");
			
			UE_LOG(LogChaos, Warning, TEXT("%s"), *OutputString);
		}
#endif

#if TODO_REIMPLEMENT_SPATIAL_ACCELERATION_ACCESS
		Solver->ReleaseSpatialAcceleration();
#endif

#if !UE_BUILD_SHIPPING
		Chaos::bPendingHierarchyDump = true;	//mark solver pending dump to get more info
#endif
	}
}

void FChaosSolversModule::LockResultsRead()
{
	if(IsPersistentTaskRunning())
	{
		PhysicsInnerTask->CacheLock.ReadLock();
	}
}

void FChaosSolversModule::UnlockResultsRead()
{
	if(IsPersistentTaskRunning())
	{
		PhysicsInnerTask->CacheLock.ReadUnlock();
	}
}

DECLARE_CYCLE_STAT(TEXT("PhysicsDedicatedStats"), STAT_PhysicsDedicatedStats, STATGROUP_ChaosDedicated);	//this is a hack, needed to make stat group turn on
DECLARE_FLOAT_COUNTER_STAT(TEXT("PhysicsThreadTotalTime(ms)"), STAT_PhysicsThreadTotalTime, STATGROUP_ChaosDedicated);
DECLARE_DWORD_COUNTER_STAT(TEXT("NumActiveConstraints"), STAT_NumActiveConstraintsDedicated, STATGROUP_ChaosDedicated);
DECLARE_DWORD_COUNTER_STAT(TEXT("NumActiveParticles"), STAT_NumActiveParticlesDedicated, STATGROUP_ChaosDedicated);
DECLARE_DWORD_COUNTER_STAT(TEXT("NumActiveCollisionPoints"), STAT_NumActiveCollisionPointsDedicated, STATGROUP_ChaosDedicated);
DECLARE_DWORD_COUNTER_STAT(TEXT("NumActiveShapes"), STAT_NumActiveShapesDedicated, STATGROUP_ChaosDedicated);

void FChaosSolversModule::UpdateStats()
{
#if STATS
	SCOPE_CYCLE_COUNTER(STAT_PhysicsStatUpdate);
	SCOPE_CYCLE_COUNTER(STAT_PhysicsDedicatedStats);

	Chaos::FPersistentPhysicsTaskStatistics PhysStats = PhysicsInnerTask->GetNextThreadStatistics_GameThread();

	if(PhysStats.NumUpdates > 0)
	{
		AverageUpdateTime = PhysStats.AccumulatedTime / (float)PhysStats.NumUpdates;
		TotalAverageUpdateTime = PhysStats.ActualAccumulatedTime / (float)PhysStats.NumUpdates;
		Fps = 1.0f / AverageUpdateTime;
		EffectiveFps = 1.0f / TotalAverageUpdateTime;
	}

	// Only set the stats if something is actually running
	if(Fps != 0.0f)
	{
		SET_FLOAT_STAT(STAT_PhysicsThreadTime, AverageUpdateTime * 1000.0f);
		SET_FLOAT_STAT(STAT_PhysicsThreadTimeEff, TotalAverageUpdateTime * 1000.0f);
		SET_FLOAT_STAT(STAT_PhysicsThreadFps, Fps);
		SET_FLOAT_STAT(STAT_PhysicsThreadFpsEff, EffectiveFps);

		if (Fps != 0.0f && PhysStats.SolverStats.Num() > 0)
		{
			PerSolverStats = PhysStats.AccumulateSolverStats();
		}

		SET_FLOAT_STAT(STAT_PhysicsThreadTotalTime, AverageUpdateTime * 1000.0f);
		SET_DWORD_STAT(STAT_NumActiveConstraintsDedicated, PerSolverStats.NumActiveConstraints);
		SET_DWORD_STAT(STAT_NumActiveParticlesDedicated, PerSolverStats.NumActiveParticles);
		SET_DWORD_STAT(STAT_NumActiveCollisionPointsDedicated, PerSolverStats.EvolutionStats.ActiveCollisionPoints);
		SET_DWORD_STAT(STAT_NumActiveShapesDedicated, PerSolverStats.EvolutionStats.ActiveShapes);

	}

#if FRAMEPRO_ENABLED

	// Custom framepro stats for graphs
	const float AvgUpdateMs = AverageUpdateTime * 1000.f;
	const float AvgEffectiveUpdateMs = TotalAverageUpdateTime * 1000.0f;

	FRAMEPRO_CUSTOM_STAT("Chaos_Thread_Fps", Fps, "ChaosThread", "FPS", FRAMEPRO_COLOUR(255,255,255));
	FRAMEPRO_CUSTOM_STAT("Chaos_Thread_EffectiveFps", EffectiveFps, "ChaosThread", "FPS", FRAMEPRO_COLOUR(255,255,255));
	FRAMEPRO_CUSTOM_STAT("Chaos_Thread_Time", AvgUpdateMs, "ChaosThread", "ms", FRAMEPRO_COLOUR(255,255,255));
	FRAMEPRO_CUSTOM_STAT("Chaos_Thread_EffectiveTime", AvgEffectiveUpdateMs, "ChaosThread", "ms", FRAMEPRO_COLOUR(255,255,255));

	FRAMEPRO_CUSTOM_STAT("Chaos_Thread_NumActiveParticles", PerSolverStats.NumActiveParticles, "ChaosThread", "Particles", FRAMEPRO_COLOUR(255,255,255));
	FRAMEPRO_CUSTOM_STAT("Chaos_Thread_NumConstraints", PerSolverStats.NumActiveConstraints, "ChaosThread", "Constraints", FRAMEPRO_COLOUR(255,255,255));
	FRAMEPRO_CUSTOM_STAT("Chaos_Thread_NumAllocatedParticles", PerSolverStats.NumAllocatedParticles, "ChaosThread", "Particles", FRAMEPRO_COLOUR(255,255,255));
	FRAMEPRO_CUSTOM_STAT("Chaos_Thread_NumPaticleIslands", PerSolverStats.NumParticleIslands, "ChaosThread", "Islands", FRAMEPRO_COLOUR(255,255,255));

	const int32 NumSolvers = Solvers.Num();
#if 0
	for(int32 SolverIndex = 0; SolverIndex < NumSolvers; ++SolverIndex)
	{
		Chaos::PBDRigidsSolver* Solver = Solvers[SolverIndex];

		const Chaos::TBoundingVolume<Chaos::TPBDRigidParticles<float, 3>, float, 3>* Hierarchy = Solver->GetSpatialAcceleration();

		if(Hierarchy)
		{
			FRAMEPRO_CUSTOM_STAT("Chaos_Thread_Hierarchy_NumObjects", Hierarchy->GlobalObjects().Num(), "ChaosThread", "Objects");
		}
		Solver->ReleaseSpatialAcceleration();
	}
#endif

#endif 

#endif
}

#if WITH_EDITOR
void FChaosSolversModule::PauseSolvers()
{
	bPauseSolvers = true;
	UE_LOG(LogChaosDebug, Verbose, TEXT("Pausing solvers."));
	// Sync physics to allow last minute updates
	if(IsPersistentTaskRunning())
	{
		SyncTask(true);
	}
}

void FChaosSolversModule::ResumeSolvers()
{
	bPauseSolvers = false;
	UE_LOG(LogChaosDebug, Verbose, TEXT("Resuming solvers."));
}

void FChaosSolversModule::SingleStepSolvers()
{
	bPauseSolvers = true;
	SingleStepCounter.Increment();
	UE_LOG(LogChaosDebug, Verbose, TEXT("Single-stepping solvers."));
	// Sync physics to allow last minute updates
	if(IsPersistentTaskRunning())
	{
		SyncTask(true);
	}
}

bool FChaosSolversModule::ShouldStepSolver(int32& InOutSingleStepCounter) const
{
	const int32 counter = SingleStepCounter.GetValue();
	const bool bShouldStepSolver = !(bPauseSolvers && InOutSingleStepCounter == counter);
	InOutSingleStepCounter = counter;
	return bShouldStepSolver;
}
#endif  // #if WITH_EDITOR

void FChaosSolversModule::ChangeBufferMode(Chaos::EMultiBufferMode BufferMode)
{
	for (Chaos::FPhysicsSolver* Solver : Solvers)
	{
		if (Dispatcher)
		{
			Dispatcher->EnqueueCommandImmediate(Solver, [InBufferMode = BufferMode]
			(Chaos::FPhysicsSolver* InSolver)
			{
				InSolver->ChangeBufferMode(InBufferMode);
			});
		}
		else
		{
			Solver->ChangeBufferMode(BufferMode);
		}
	}

}

Chaos::EThreadingMode FChaosSolversModule::GetDesiredThreadingMode() const
{
	const bool bForceSingleThread = !FApp::ShouldUseThreadingForPerformance();

	// If the platform isn't using threads for perf - force Chaos to
	// run single threaded no matter the selected mode.
	if(bForceSingleThread)
	{
		return Chaos::EThreadingMode::SingleThread;
	}

	return GetSettingsProvider().GetDefaultThreadingMode();
}

Chaos::EMultiBufferMode FChaosSolversModule::GetDesiredBufferingMode() const
{
	return GetBufferModeFromThreadingModel(GetDesiredThreadingMode());
}

void FChaosSolversModule::OnUpdateMaterial(Chaos::FMaterialHandle InHandle)
{
	check(Dispatcher);

	// Grab the material
	Chaos::FChaosPhysicsMaterial* Material = InHandle.Get();

	if(ensure(Material))
	{
		for(Chaos::FPhysicsSolver* Solver : Solvers)
		{
			// Send a copy of the material to each solver
			Dispatcher->EnqueueCommandImmediate(Solver, [InHandle, MaterialCopy = *Material](Chaos::FPhysicsSolver* InSolver)
			{
				InSolver->UpdateMaterial(InHandle, MaterialCopy);
			});
		}
	}
}

void FChaosSolversModule::OnCreateMaterial(Chaos::FMaterialHandle InHandle)
{
	check(Dispatcher);

	// Grab the material
	Chaos::FChaosPhysicsMaterial* Material = InHandle.Get();

	if(ensure(Material))
	{
		for(Chaos::FPhysicsSolver* Solver : Solvers)
		{
			// Send a copy of the material to each solver
			Dispatcher->EnqueueCommandImmediate(Solver, [InHandle, MaterialCopy = *Material](Chaos::FPhysicsSolver* InSolver)
			{
				InSolver->CreateMaterial(InHandle, MaterialCopy);
			});
		}
	}
}

void FChaosSolversModule::OnDestroyMaterial(Chaos::FMaterialHandle InHandle)
{
	//check(Dispatcher);
	if (!Dispatcher)
	{
		return;
	}

	// Grab the material
	Chaos::FChaosPhysicsMaterial* Material = InHandle.Get();

	if(ensure(Material))
	{
		for(Chaos::FPhysicsSolver* Solver : Solvers)
		{
			// Notify each solver
			Dispatcher->EnqueueCommandImmediate(Solver, [InHandle](Chaos::FPhysicsSolver* InSolver)
			{
				InSolver->DestroyMaterial(InHandle);
			});
		}
	}
}

const IChaosSettingsProvider& FChaosSolversModule::GetSettingsProvider() const
{
	return SettingsProvider ? *SettingsProvider : Chaos::GDefaultChaosSettings;
}

FChaosScopedPhysicsThreadLock::FChaosScopedPhysicsThreadLock()
	: FChaosScopedPhysicsThreadLock(MAX_uint32)
{

}

FChaosScopedPhysicsThreadLock::FChaosScopedPhysicsThreadLock(uint32 InMsToWait)
	: CompleteEvent(nullptr)
	, PTStallEvent(nullptr)
	, Module(nullptr)
	, bGotLock(false)
{
	Module = FChaosSolversModule::GetModule();
	checkSlow(Module && Module->GetDispatcher());

	Chaos::IDispatcher* PhysDispatcher = Module->GetDispatcher();
	if(PhysDispatcher->GetMode() == Chaos::EThreadingMode::DedicatedThread)
	{
		CompleteEvent = FPlatformProcess::GetSynchEventFromPool(false);
		PTStallEvent = FPlatformProcess::GetSynchEventFromPool(false);

		// Request a halt on the physics thread
		PhysDispatcher->EnqueueCommandImmediate([PTStall = PTStallEvent, GTSync = CompleteEvent](Chaos::FPersistentPhysicsTask* PhysThread)
		{
			PTStall->Trigger();
			GTSync->Wait();

			FPlatformProcess::ReturnSynchEventToPool(GTSync);
			FPlatformProcess::ReturnSynchEventToPool(PTStall);
		});

		{
			SCOPE_CYCLE_COUNTER(STAT_LockWaits);
			// Wait for the physics thread to actually stall
			bGotLock = PTStallEvent->Wait(InMsToWait);
		}

		if(!bGotLock)
		{
			// Trigger this if we didn't get a lock to avoid blocking the physics thread
			CompleteEvent->Trigger();
		}
	}
	else
	{
		CompleteEvent = nullptr;
		PTStallEvent = nullptr;
	}
}

FChaosScopedPhysicsThreadLock::~FChaosScopedPhysicsThreadLock()
{
	if(CompleteEvent && PTStallEvent && bGotLock)
	{
		CompleteEvent->Trigger();
	}

	// Can't return these here until the physics thread wakes up,
	// the physics thread will return these events (see FChaosScopedPhysicsLock::FChaosScopedPhysicsLock)
	CompleteEvent = nullptr;
	PTStallEvent = nullptr;
	Module = nullptr;
}

bool FChaosScopedPhysicsThreadLock::DidGetLock() const
{
	return bGotLock;
}

IMPLEMENT_MODULE(FChaosSolversModule, ChaosSolvers);

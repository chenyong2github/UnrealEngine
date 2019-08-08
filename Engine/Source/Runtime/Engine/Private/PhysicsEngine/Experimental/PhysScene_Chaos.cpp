// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#if INCLUDE_CHAOS

#include "Physics/Experimental/PhysScene_Chaos.h"

#include "PhysicsSolver.h"
#include "ChaosSolversModule.h"
#include "ChaosLog.h"
#include "ChaosStats.h"

#include "CoreMinimal.h"
#include "GameDelegates.h"

#include "Async/AsyncWork.h"
#include "Async/ParallelFor.h"
#include "Engine/Engine.h"
#include "Field/FieldSystem.h"
#include "Framework/Dispatcher.h"
#include "Framework/PersistentTask.h"
#include "Framework/PhysicsTickTask.h"
#include "Misc/CoreDelegates.h"
#include "Misc/ScopeLock.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "Components/PrimitiveComponent.h"

#include "PhysicsProxy/FieldSystemPhysicsProxy.h"
#include "PhysicsProxy/GeometryCollectionPhysicsProxy.h"
#include "PhysicsProxy/SkeletalMeshPhysicsProxy.h"
#include "PhysicsProxy/StaticMeshPhysicsProxy.h"
#include "Chaos/Framework/SingleParticlePhysicsProxy.h"
#include "Chaos/UniformGrid.h"
#include "Chaos/BoundingVolume.h"
#include "Chaos/ISpatialAcceleration.h"
#include "Chaos/Framework/DebugSubstep.h"
#include "Chaos/PBDSpringConstraints.h"
#include "Chaos/PerParticleGravity.h"

#if !UE_BUILD_SHIPPING
#include "Engine/World.h"
#include "DrawDebugHelpers.h"

TAutoConsoleVariable<int32> CVar_ChaosDrawHierarchyEnable(TEXT("P.Chaos.DrawHierarchy.Enable"), 0, TEXT("Enable / disable drawing of the physics hierarchy"));
TAutoConsoleVariable<int32> CVar_ChaosDrawHierarchyCells(TEXT("P.Chaos.DrawHierarchy.Cells"), 0, TEXT("Enable / disable drawing of the physics hierarchy cells"));
TAutoConsoleVariable<int32> CVar_ChaosDrawHierarchyBounds(TEXT("P.Chaos.DrawHierarchy.Bounds"), 1, TEXT("Enable / disable drawing of the physics hierarchy bounds"));
TAutoConsoleVariable<int32> CVar_ChaosDrawHierarchyObjectBounds(TEXT("P.Chaos.DrawHierarchy.ObjectBounds"), 1, TEXT("Enable / disable drawing of the physics hierarchy object bounds"));
TAutoConsoleVariable<int32> CVar_ChaosDrawHierarchyCellElementThresh(TEXT("P.Chaos.DrawHierarchy.CellElementThresh"), 128, TEXT("Num elements to consider \"high\" for cell colouring when rendering."));
TAutoConsoleVariable<int32> CVar_ChaosDrawHierarchyDrawEmptyCells(TEXT("P.Chaos.DrawHierarchy.DrawEmptyCells"), 1, TEXT("Whether to draw cells that are empty when cells are enabled."));

#endif

#if WITH_EDITOR
#include "Editor.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogFPhysScene_ChaosSolver, Log, All);

void DumpHierarchyStats(const TArray<FString>& Args)
{
#if !UE_BUILD_SHIPPING
	if(FChaosSolversModule* Module = FChaosSolversModule::GetModule())
	{
		int32 MaxElems = 0;
		Module->DumpHierarchyStats(&MaxElems);

		if(Args.Num() > 0 && Args[0] == TEXT("UPDATERENDER"))
		{
			CVar_ChaosDrawHierarchyCellElementThresh->Set(MaxElems);
		}
	}
#endif
}

static FAutoConsoleCommand Command_DumpHierarchyStats(TEXT("p.chaos.dumphierarcystats"), TEXT("Outputs current collision hierarchy stats to the output log"), FConsoleCommandWithArgsDelegate::CreateStatic(DumpHierarchyStats));

#if !UE_BUILD_SHIPPING
class FSpacialDebugDraw : public Chaos::ISpacialDebugDrawInterface<float>
{
public:

	FSpacialDebugDraw(UWorld* InWorld)
		: World(InWorld)
	{

	}

	virtual void Box(const Chaos::TBox<float, 3>& InBox, const Chaos::TVector<float, 3>& InLinearColor, float InThickness) override
	{
		DrawDebugBox(World, InBox.Center(), InBox.Extents(), FQuat::Identity, FLinearColor(InLinearColor).ToFColor(true), false, -1.0f, SDPG_Foreground, InThickness);
	}


	virtual void Line(const Chaos::TVector<float, 3>& InBegin, const Chaos::TVector<float, 3>& InEnd, const Chaos::TVector<float, 3>& InLinearColor, float InThickness) override
	{
		DrawDebugLine(World, InBegin, InEnd, FLinearColor(InLinearColor).ToFColor(true), false, -1.0f, SDPG_Foreground, InThickness);
	}

private:
	UWorld* World;
};
#endif

class FPhysicsThreadSyncCaller : public FTickableGameObject
{
public:
#if CHAOS_WITH_PAUSABLE_SOLVER
	DECLARE_MULTICAST_DELEGATE(FOnUpdateWorldPause);
	FOnUpdateWorldPause OnUpdateWorldPause;
#endif

	FPhysicsThreadSyncCaller()
	{
		ChaosModule = FModuleManager::Get().GetModulePtr<FChaosSolversModule>("ChaosSolvers");
		check(ChaosModule);

		WorldCleanupHandle = FWorldDelegates::OnPostWorldCleanup.AddRaw(this, &FPhysicsThreadSyncCaller::OnWorldDestroyed);
	}

	~FPhysicsThreadSyncCaller()
	{
		if(WorldCleanupHandle.IsValid())
		{
			FWorldDelegates::OnPostWorldCleanup.Remove(WorldCleanupHandle);
		}
	}

	virtual void Tick(float DeltaTime) override
	{
		if(ChaosModule->IsPersistentTaskRunning())
		{
			ChaosModule->SyncTask();

#if !UE_BUILD_SHIPPING
			DebugDrawSolvers();
#endif
		}

#if CHAOS_WITH_PAUSABLE_SOLVER
		// Check each physics scene's world status and update the corresponding solver's pause state
		OnUpdateWorldPause.Broadcast();
#endif
	}

	virtual TStatId GetStatId() const override
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(PhysicsThreadSync, STATGROUP_Tickables);
	}

	virtual bool IsTickableInEditor() const override
	{
		return false;
	}

private:

	void OnWorldDestroyed(UWorld* InWorld, bool bSessionEnded, bool bCleanupResources)
	{
		// This should really only sync if it's the right world, but for now always sync on world destroy.
		if(ChaosModule->IsPersistentTaskRunning())
		{
			ChaosModule->SyncTask(true);
		}
	}

#if !UE_BUILD_SHIPPING
	void DebugDrawSolvers()
	{
		using namespace Chaos;
		const bool bDrawHier = CVar_ChaosDrawHierarchyEnable.GetValueOnGameThread() != 0;
		const bool bDrawCells = CVar_ChaosDrawHierarchyCells.GetValueOnGameThread() != 0;
		const bool bDrawEmptyCells = CVar_ChaosDrawHierarchyDrawEmptyCells.GetValueOnGameThread() != 0;
		const bool bDrawBounds = CVar_ChaosDrawHierarchyBounds.GetValueOnGameThread() != 0;
		const bool bDrawObjectBounds = CVar_ChaosDrawHierarchyObjectBounds.GetValueOnGameThread() != 0;

		UWorld* WorldPtr = nullptr;
		const TIndirectArray<FWorldContext>& WorldContexts = GEngine->GetWorldContexts();
		for(const FWorldContext& Context : WorldContexts)
		{
			UWorld* TestWorld = Context.World();
			if(TestWorld && (Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE))
			{
				WorldPtr = TestWorld;
			}
		}

		if(!WorldPtr)
		{
			// Can't debug draw without a valid world
			return;
		}

		FSpacialDebugDraw DrawInterface(WorldPtr);

		const TArray<FPhysicsSolver*>& Solvers = ChaosModule->GetSolvers();

		for(FPhysicsSolver* Solver : Solvers)
		{
			if(bDrawHier)
			{
#if TODO_REIMPLEMENT_SPATIAL_ACCELERATION_ACCESS
				if (const ISpatialAcceleration<float, 3>* SpatialAcceleration = Solver->GetSpatialAcceleration())
				{
					SpatialAcceleration->DebugDraw(&DrawInterface);
					Solver->ReleaseSpatialAcceleration();
				}
#endif
				
#if 0
				if (const Chaos::TBoundingVolume<TPBDRigidParticles<float, 3>, float, 3>* BV = SpatialAcceleration->Cast<TBoundingVolume<TPBDRigidParticles<float, 3>, float, 3>>())
				{

					const TUniformGrid<float, 3>& Grid = BV->GetGrid();

					if (bDrawBounds)
					{
						const FVector Min = Grid.MinCorner();
						const FVector Max = Grid.MaxCorner();

						DrawDebugBox(WorldPtr, (Min + Max) / 2, (Max - Min) / 2, FQuat::Identity, FColor::Cyan, false, -1.0f, SDPG_Foreground, 1.0f);
					}

					if (bDrawObjectBounds)
					{
						const TArray<TBox<float, 3>>& Boxes = BV->GetWorldSpaceBoxes();
						for (const TBox<float, 3>& Box : Boxes)
						{
							DrawDebugBox(WorldPtr, Box.Center(), Box.Extents() / 2.0f, FQuat::Identity, FColor::Cyan, false, -1.0f, SDPG_Foreground, 1.0f);
						}
					}

					if (bDrawCells)
					{
						// Reduce the extent very slightly to differentiate cell colors
						const FVector CellExtent = Grid.Dx() * 0.95;

						const TVector<int32, 3>& CellCount = Grid.Counts();
						for (int32 CellsX = 0; CellsX < CellCount[0]; ++CellsX)
						{
							for (int32 CellsY = 0; CellsY < CellCount[1]; ++CellsY)
							{
								for (int32 CellsZ = 0; CellsZ < CellCount[2]; ++CellsZ)
								{
									const TArray<int32>& CellList = BV->GetElements()(CellsX, CellsY, CellsZ);
									const int32 NumEntries = CellList.Num();

									const float TempFraction = FMath::Min<float>(NumEntries / (float)CVar_ChaosDrawHierarchyCellElementThresh.GetValueOnGameThread(), 1.0f);

									const FColor CellColor = FColor::MakeRedToGreenColorFromScalar(1.0f - TempFraction);

									if (NumEntries > 0 || bDrawEmptyCells)
									{
										DrawDebugBox(WorldPtr, Grid.Location(TVector<int32, 3>(CellsX, CellsY, CellsZ)), CellExtent / 2.0f, FQuat::Identity, CellColor, false, -1.0f, SDPG_Foreground, 0.5f);
									}
								}
							}
						}
					}
				}
#endif
			}
		}
	}
#endif

	FChaosSolversModule* ChaosModule;
	FDelegateHandle WorldCleanupHandle;
};
static FPhysicsThreadSyncCaller* SyncCaller;

#if WITH_EDITOR
// Singleton class to register pause/resume/single-step/pre-end handles to the editor
// and issue the pause/resume/single-step commands to the Chaos' module.
class FPhysScene_ChaosPauseHandler final
{
public:
	explicit FPhysScene_ChaosPauseHandler(FChaosSolversModule* InChaosModule)
		: ChaosModule(InChaosModule)
	{
		check(InChaosModule);
		// Add editor pause/step handles
		FEditorDelegates::BeginPIE.AddRaw(this, &FPhysScene_ChaosPauseHandler::ResumeSolvers);
		FEditorDelegates::EndPIE.AddRaw(this, &FPhysScene_ChaosPauseHandler::PauseSolvers);
		FEditorDelegates::PausePIE.AddRaw(this, &FPhysScene_ChaosPauseHandler::PauseSolvers);
		FEditorDelegates::ResumePIE.AddRaw(this, &FPhysScene_ChaosPauseHandler::ResumeSolvers);
		FEditorDelegates::SingleStepPIE.AddRaw(this, &FPhysScene_ChaosPauseHandler::SingleStepSolvers);
	}

	~FPhysScene_ChaosPauseHandler()
	{
		// Remove editor pause/step delegates
		FEditorDelegates::BeginPIE.RemoveAll(this);
		FEditorDelegates::EndPIE.RemoveAll(this);
		FEditorDelegates::PausePIE.RemoveAll(this);
		FEditorDelegates::ResumePIE.RemoveAll(this);
		FEditorDelegates::SingleStepPIE.RemoveAll(this);
	}

private:
	void PauseSolvers(bool /*bIsSimulating*/) { ChaosModule->PauseSolvers(); }
	void ResumeSolvers(bool /*bIsSimulating*/) { ChaosModule->ResumeSolvers(); }
	void SingleStepSolvers(bool /*bIsSimulating*/) { ChaosModule->SingleStepSolvers(); }

private:
	FChaosSolversModule* ChaosModule;
};
static TUniquePtr<FPhysScene_ChaosPauseHandler> PhysScene_ChaosPauseHandler;
#endif

static void CopyParticleData(Chaos::TPBDRigidParticles<float, 3>& ToParticles, const int32 ToIndex, Chaos::TPBDRigidParticles<float, 3>& FromParticles, const int32 FromIndex)
{
	ToParticles.X(ToIndex) = FromParticles.X(FromIndex);
	ToParticles.R(ToIndex) = FromParticles.R(FromIndex);
	ToParticles.V(ToIndex) = FromParticles.V(FromIndex);
	ToParticles.W(ToIndex) = FromParticles.W(FromIndex);
	ToParticles.M(ToIndex) = FromParticles.M(FromIndex);
	ToParticles.InvM(ToIndex) = FromParticles.InvM(FromIndex);
	ToParticles.I(ToIndex) = FromParticles.I(FromIndex);
	ToParticles.InvI(ToIndex) = FromParticles.InvI(FromIndex);
	ToParticles.SetGeometry(ToIndex, FromParticles.Geometry(FromIndex));	//question: do we need to deal with dynamic geometry?
	ToParticles.CollisionParticles(ToIndex) = MoveTemp(FromParticles.CollisionParticles(FromIndex));
	ToParticles.DisabledRef(ToIndex) = FromParticles.Disabled(FromIndex);
	ToParticles.SetSleeping(ToIndex, FromParticles.Sleeping(FromIndex));
}

/** Struct to remember a pending component transform change */
struct FPhysScenePendingComponentTransform_Chaos
{
	/** Component to move */
	TWeakObjectPtr<UPrimitiveComponent> OwningComp;
	/** New transform from physics engine */
	FTransform NewTransform;

	FPhysScenePendingComponentTransform_Chaos(UPrimitiveComponent* InOwningComp, const FTransform& InNewTransform)
		: OwningComp(InOwningComp)
		, NewTransform(InNewTransform)
	{}
};

FPhysScene_Chaos::FPhysScene_Chaos(AActor* InSolverActor)
	: ChaosModule(nullptr)
	, SceneSolver(nullptr)
	, SolverActor(InSolverActor)
#if WITH_EDITOR
	, SingleStepCounter(0)
#endif
#if CHAOS_WITH_PAUSABLE_SOLVER
	, bIsWorldPaused(false)
#endif
{
	ChaosModule = FModuleManager::Get().GetModulePtr<FChaosSolversModule>("ChaosSolvers");
	check(ChaosModule);

	SceneSolver = ChaosModule->CreateSolver();
	check(SceneSolver);

	// If we're running the physics thread, hand over the solver to it - we are no longer
	// able to access the solver on the game thread and should only use commands
	if(ChaosModule->GetDispatcher() && ChaosModule->GetDispatcher()->GetMode() == Chaos::EThreadingMode::DedicatedThread)
	{
		// Should find a better way to spawn this. Engine module has no apeiron singleton right now.
		// this caller will tick after all worlds have ticked and tell the apeiron module to sync
		// all of the active proxies it has from the physics thread
		if(!SyncCaller)
		{
			SyncCaller = new FPhysicsThreadSyncCaller();
		}

#if CHAOS_WITH_PAUSABLE_SOLVER
		// Hook up this object to the check pause delegate
		SyncCaller->OnUpdateWorldPause.AddRaw(this, &FPhysScene_Chaos::OnUpdateWorldPause);
#endif
	}

	// #BGallagher Temporary while we're using the global scene singleton. Shouldn't be required
	// once we have a better lifecycle for the scenes.
	FCoreDelegates::OnPreExit.AddRaw(this, &FPhysScene_Chaos::Shutdown);

	PhysicsProxyToComponentMap.Reset();
	ComponentToPhysicsProxyMap.Reset();

#if WITH_EDITOR
	FGameDelegates::Get().GetEndPlayMapDelegate().AddRaw(this, &FPhysScene_Chaos::OnWorldEndPlay);

	if(!PhysScene_ChaosPauseHandler)
	{
		PhysScene_ChaosPauseHandler = MakeUnique<FPhysScene_ChaosPauseHandler>(ChaosModule);
	}
#endif
}

FPhysScene_Chaos::~FPhysScene_Chaos()
{
	Shutdown();
	
	FCoreDelegates::OnPreExit.RemoveAll(this);

#if WITH_EDITOR
	FGameDelegates::Get().GetEndPlayMapDelegate().RemoveAll(this);
#endif

#if CHAOS_WITH_PAUSABLE_SOLVER
	if (SyncCaller)
	{
		SyncCaller->OnUpdateWorldPause.RemoveAll(this);
	}
#endif
}

bool FPhysScene_Chaos::IsTickable() const
{
	const bool bDedicatedThread = ChaosModule->IsPersistentTaskRunning();

#if TODO_REIMPLEMENT_SOLVER_ENABLING
	return !bDedicatedThread && GetSolver()->Enabled();
#else
	return false;
#endif
}

void FPhysScene_Chaos::Tick(float DeltaTime)
{
#if WITH_EDITOR
	// Check the editor pause status and update this object's single-step counter.
	// This check cannot be moved to IsTickable() since this is a test/update operation
	// and needs to happen only once per tick.
	if (!ChaosModule->ShouldStepSolver(SingleStepCounter)) { return; }
#endif

	Chaos::FPhysicsSolver* Solver = GetSolver();

#if CHAOS_WITH_PAUSABLE_SOLVER
	// Update solver depending on the pause status of the actor's world attached to this scene
	OnUpdateWorldPause();

#if TODO_REIMPLEMENT_SOLVER_PAUSING
	// Return now if this solver is paused
	if (Solver->Paused()) { return; }
#endif
#endif

	float SafeDelta = FMath::Clamp(DeltaTime, 0.0f, UPhysicsSettings::Get()->MaxPhysicsDeltaTime);

	UE_LOG(LogFPhysScene_ChaosSolver, Verbose, TEXT("FPhysScene_Chaos::Tick(%3.5f)"), SafeDelta);
	Solver->AdvanceSolverBy(SafeDelta);

	Solver->ForEachPhysicsProxyParallel([](auto* Object)
	{
		// #BGallagher TODO Just use one side of the buffer for single-thread tick
		Object->BufferPhysicsResults();
		Object->FlipBuffer();
	});

	Solver->ForEachPhysicsProxy([](auto* Object)
	{
		Object->PullFromPhysicsState();
	});


}

Chaos::FPhysicsSolver* FPhysScene_Chaos::GetSolver() const
{
	return SceneSolver;
}

AActor* FPhysScene_Chaos::GetSolverActor() const
{
	return SolverActor.Get();
}

Chaos::IDispatcher* FPhysScene_Chaos::GetDispatcher() const
{
	return ChaosModule ? ChaosModule->GetDispatcher() : nullptr;
}

template<typename ObjectType>
void AddPhysicsProxy(ObjectType* InObject, Chaos::FPhysicsSolver* InSolver, Chaos::IDispatcher* InDispatcher)
{
	check(IsInGameThread() && InSolver);
	//const bool bDedicatedThread = ChaosModule->IsPersistentTaskEnabled();

	InObject->SetSolver(InSolver);
	InObject->Initialize();

	if(/*bDedicatedThread && */InDispatcher)
	{
		// Pass the proxy off to the physics thread
		InDispatcher->EnqueueCommandImmediate([InObject, InSolver](Chaos::FPersistentPhysicsTask* PhysThread)
		{
#if CHAOS_PARTICLEHANDLE_TODO
			InSolver->RegisterObject(InObject);
#endif
		});
	}
}

void FPhysScene_Chaos::AddObject(UPrimitiveComponent* Component, FSkeletalMeshPhysicsProxy* InObject)
{
	AddToComponentMaps(Component, InObject);

	check(false);
#if 0
	Chaos::FPhysicsSolver* Solver = GetSolver();
	Solver->RegisterObject(InObject);

	AddPhysicsProxy(InObject, Solver, GetDispatcher());
#endif
}

void FPhysScene_Chaos::AddObject(UPrimitiveComponent* Component, FStaticMeshPhysicsProxy* InObject)
{
	AddToComponentMaps(Component, InObject);

	check(false);
#if 0
	Chaos::FPhysicsSolver* Solver = GetSolver();
	Solver->RegisterObject(InObject);

	AddPhysicsProxy(InObject, Solver, GetDispatcher());
#endif
}

void FPhysScene_Chaos::AddObject(UPrimitiveComponent* Component, FGeometryParticlePhysicsProxy* InObject)
{
	AddToComponentMaps(Component, InObject);

	check(false);
#if 0
	Chaos::FPhysicsSolver* Solver = GetSolver();
	Solver->RegisterObject(InObject);

	AddPhysicsProxy(InObject, Solver, GetDispatcher());
#endif
}

void FPhysScene_Chaos::AddObject(UPrimitiveComponent* Component, FGeometryCollectionPhysicsProxy* InObject)
{
	AddToComponentMaps(Component, InObject);

	check(false);
#if 0
	Chaos::FPhysicsSolver* Solver = GetSolver();
	Solver->RegisterObject(InObject);

	AddPhysicsProxy(InObject, Solver, GetDispatcher());
#endif
}

void FPhysScene_Chaos::AddObject(UPrimitiveComponent* Component, FFieldSystemPhysicsProxy* InObject)
{
	AddToComponentMaps(Component, InObject);

	check(false);
#if 0
	Chaos::FPhysicsSolver* CurrSceneSolver = GetSolver();

	check(IsInGameThread() && CurrSceneSolver);
	//const bool bDedicatedThread = ChaosModule->IsPersistentTaskEnabled();

	InObject->SetSolver(CurrSceneSolver);
	
	Chaos::IDispatcher* Dispatcher = GetDispatcher();

	const TArray<Chaos::FPhysicsSolver*>& Solvers = ChaosModule->GetSolvers();
	for(Chaos::FPhysicsSolver* Solver : Solvers)
	{
		if(true || Solver->HasActiveParticles())
		{
			Solver->RegisterObject(InObject);

			if(/*bDedicatedThread && */Dispatcher)
			{
				// Pass the proxy off to the physics thread
				Dispatcher->EnqueueCommand([InObject, InSolver = Solver](Chaos::FPersistentPhysicsTask* PhysThread)
				{
#if CHAOS_PARTICLEHANDLE_TODO
					InSolver->RegisterObject(InObject);
#endif
				});
			}
		}
	}
#endif
}

template<typename ObjectType>
void RemovePhysicsProxy(ObjectType* InObject, Chaos::FPhysicsSolver* InSolver, FChaosSolversModule* InModule)
{
	check(IsInGameThread());

	Chaos::IDispatcher* PhysDispatcher = InModule->GetDispatcher();
	check(PhysDispatcher);

	const bool bDedicatedThread = PhysDispatcher->GetMode() == Chaos::EThreadingMode::DedicatedThread;

	if(PhysDispatcher)
	{
		// Remove the object from the solver
		PhysDispatcher->EnqueueCommandImmediate([InObject, InSolver, bDedicatedThread](Chaos::FPersistentPhysicsTask* PhysThread)
		{
#if CHAOS_PARTICLEHANDLE_TODO
			InSolver->UnregisterObject(InObject);
#endif
			InObject->OnRemoveFromScene();

			if (!bDedicatedThread)
			{
				InObject->SyncBeforeDestroy();
				delete InObject;
			}

		});
	}
}

void FPhysScene_Chaos::RemoveObject(FSkeletalMeshPhysicsProxy* InObject)
{
	check(false);
#if 0
	Chaos::FPhysicsSolver* Solver = InObject->GetSolver();
	const int32 NumRemoved = Solver->UnregisterObject(InObject);

	if(NumRemoved == 0)
	{
		UE_LOG(LogChaos, Warning, TEXT("Attempted to remove an object that wasn't found in its solver's gamethread storage - it's likely the solver has been mistakenly changed."));
	}

	RemoveFromComponentMaps(InObject);

	RemovePhysicsProxy(InObject, Solver, ChaosModule);
#endif
}

void FPhysScene_Chaos::RemoveObject(FStaticMeshPhysicsProxy* InObject)
{
	check(false);
#if 0
	Chaos::FPhysicsSolver* Solver = InObject->GetSolver();

	const int32 NumRemoved = Solver->UnregisterObject(InObject);

	if(NumRemoved == 0)
	{
		UE_LOG(LogChaos, Warning, TEXT("Attempted to remove an object that wasn't found in its solver's gamethread storage - it's likely the solver has been mistakenly changed."));
	}

	RemoveFromComponentMaps(InObject);

	RemovePhysicsProxy(InObject, Solver, ChaosModule);
#endif
}

void FPhysScene_Chaos::RemoveObject(FGeometryParticlePhysicsProxy* InObject)
{
	check(false);
#if 0
	Chaos::FPhysicsSolver* Solver = InObject->GetSolver();

	const int32 NumRemoved = Solver->UnregisterObject(InObject);

	if (NumRemoved == 0)
	{
		UE_LOG(LogChaos, Warning, TEXT("Attempted to remove an object that wasn't found in its solver's gamethread storage - it's likely the solver has been mistakenly changed."));
	}

	RemoveFromComponentMaps(InObject);

	RemovePhysicsProxy(InObject, Solver, ChaosModule);
#endif
}

void FPhysScene_Chaos::RemoveObject(FGeometryCollectionPhysicsProxy* InObject)
{
	check(false);
#if 0
	Chaos::FPhysicsSolver* Solver = InObject->GetSolver();

	const int32 NumRemoved = Solver->UnregisterObject(InObject);

	if(NumRemoved == 0)
	{
		UE_LOG(LogChaos, Warning, TEXT("Attempted to remove an object that wasn't found in its solver's gamethread storage - it's likely the solver has been mistakenly changed."));
	}

	RemoveFromComponentMaps(InObject);

	RemovePhysicsProxy(InObject, Solver, ChaosModule);
#endif
}

void FPhysScene_Chaos::RemoveObject(FFieldSystemPhysicsProxy* InObject)
{
	check(false);
#if 0
	if(!InObject)
	{
		return;
	}

	Chaos::FPhysicsSolver* CurrSceneSolver = GetSolver();

	CurrSceneSolver->UnregisterObject(InObject);

	check(IsInGameThread());

	Chaos::IDispatcher* Dispatcher = GetDispatcher();
	const bool bDedicatedThread = Dispatcher->GetMode() == Chaos::EThreadingMode::DedicatedThread;

	const TArray<Chaos::FPhysicsSolver*>& Solvers = ChaosModule->GetSolvers();
	for(Chaos::FPhysicsSolver* Solver : Solvers)
	{
		if(true || Solver->HasActiveParticles())
		{
			// @todo(question) : Is the RegisterObject a bug? Seems like it should be removing. [brice->benn]
			Solver->RegisterObject(InObject);

			if(/*bDedicatedThread && */Dispatcher)
			{
				// Pass the proxy off to the physics thread
				Dispatcher->EnqueueCommand([InObject, InSolver = Solver](Chaos::FPersistentPhysicsTask* PhysThread)
				{
					InSolver->UnregisterObject(InObject);
				});
			}
		}
	}

	RemoveFromComponentMaps(InObject);

	if(!bDedicatedThread)
	{
		delete InObject;
	}
#endif
}

void FPhysScene_Chaos::UnregisterEvent(const Chaos::EEventType& EventID)
{
	check(IsInGameThread());

	Chaos::IDispatcher* Dispatcher = GetDispatcher();
	Chaos::FPBDRigidsSolver* Solver = GetSolver();

	if (Dispatcher)
	{
		Dispatcher->EnqueueCommandImmediate([EventID, InSolver = Solver](Chaos::FPersistentPhysicsTask* PhysThread)
		{
			InSolver->GetEventManager()->UnregisterEvent(EventID);
		});
	}
}

void FPhysScene_Chaos::UnregisterEventHandler(const Chaos::EEventType& EventID, const void* Handler)
{
	check(IsInGameThread());

	Chaos::IDispatcher* Dispatcher = GetDispatcher();
	Chaos::FPBDRigidsSolver* Solver = GetSolver();

	if (Dispatcher)
	{
		Dispatcher->EnqueueCommandImmediate([EventID, Handler, InSolver = Solver](Chaos::FPersistentPhysicsTask* PhysThread)
		{
			InSolver->GetEventManager()->UnregisterHandler(EventID, Handler);
		});
	}
}

void FPhysScene_Chaos::Shutdown()
{
	if(ChaosModule)
	{
		// Destroy our solver
		ChaosModule->DestroySolver(GetSolver());
	}

	ChaosModule = nullptr;
	SceneSolver = nullptr;

	PhysicsProxyToComponentMap.Reset();
	ComponentToPhysicsProxyMap.Reset();
}

void FPhysScene_Chaos::AddReferencedObjects(FReferenceCollector& Collector)
{
#if WITH_EDITOR
	for(UObject* Obj : PieModifiedObjects)
	{
		Collector.AddReferencedObject(Obj);
	}

	for (TPair<IPhysicsProxyBase*, UPrimitiveComponent*>& Pair : PhysicsProxyToComponentMap)
	{
		Collector.AddReferencedObject(Pair.Get<1>());
	}
#endif
}

#if CHAOS_WITH_PAUSABLE_SOLVER
void FPhysScene_Chaos::OnUpdateWorldPause()
{
	// Check game pause
	bool bIsPaused = false;
	const AActor* const Actor = GetSolverActor();
	if (Actor)
	{
		const UWorld* World = Actor->GetWorld();
		if (World)
		{
			// Use a simpler version of the UWorld::IsPaused() implementation that doesn't take the editor pause into account.
			// This is because OnUpdateWorldPause() is usually called within a tick update that happens well after that 
			// the single step flag has been used and cleared up, and the solver will stay paused otherwise.
			// The editor single step is handled separately with an editor delegate that pauses/single-steps all threads at once.
			const AWorldSettings* const Info = World->GetWorldSettings(/*bCheckStreamingPersistent=*/false, /*bChecked=*/false);
			bIsPaused = ((Info && Info->GetPauserPlayerState() && World->TimeSeconds >= World->PauseDelay) ||
				(World->bRequestedBlockOnAsyncLoading && World->GetNetMode() == NM_Client) ||
				(World && GEngine->ShouldCommitPendingMapChange(World)));
		}
	}

#if TODO_REIMPLEMENT_SOLVER_PAUSING
	if (bIsWorldPaused != bIsPaused)
	{
		bIsWorldPaused = bIsPaused;
		// Update solver pause status
		Chaos::IDispatcher* const PhysDispatcher = ChaosModule->GetDispatcher();
		if (PhysDispatcher)
		{
			UE_LOG(LogFPhysScene_ChaosSolver, Verbose, TEXT("FPhysScene_Chaos::OnUpdateWorldPause() pause status changed for actor %s, bIsPaused = %d"), Actor ? *Actor->GetName() : TEXT("None"), bIsPaused);
			PhysDispatcher->EnqueueCommandImmediate(SceneSolver, [bIsPaused](Chaos::FPhysicsSolver* Solver)
			{
				Solver->SetPaused(bIsPaused);
			});
		}
	}
#endif
}
#endif  // #if CHAOS_WITH_PAUSABLE_SOLVER

#if WITH_EDITOR
void FPhysScene_Chaos::OnWorldEndPlay()
{
	// Mark PIE modified objects dirty - couldn't do this during the run because
	// it's silently ignored
	for(UObject* Obj : PieModifiedObjects)
	{
		Obj->Modify();
	}

	PieModifiedObjects.Reset();
}

void FPhysScene_Chaos::AddPieModifiedObject(UObject* InObj)
{
	if(GIsPlayInEditorWorld)
	{
		PieModifiedObjects.AddUnique(InObj);
	}
}
#endif

void FPhysScene_Chaos::AddToComponentMaps(UPrimitiveComponent* Component, IPhysicsProxyBase* InObject)
{
	if (Component != nullptr && InObject != nullptr)
	{
		PhysicsProxyToComponentMap.Add(InObject, Component);
		ComponentToPhysicsProxyMap.Add(Component, InObject);
	}
}

void FPhysScene_Chaos::RemoveFromComponentMaps(IPhysicsProxyBase* InObject)
{
	UPrimitiveComponent** const Component = PhysicsProxyToComponentMap.Find(InObject);
	if (Component)
	{
		ComponentToPhysicsProxyMap.Remove(*Component);
	}

	PhysicsProxyToComponentMap.Remove(InObject);
}

#if WITH_CHAOS

FPhysScene_ChaosInterface::FPhysScene_ChaosInterface(const AWorldSettings* InSettings /*= nullptr*/)
{
	//Initialize unique ptrs that are just here to allow forward declare. This should be reworked todo(ocohen)
#if TODO_FIX_REFERENCES_TO_ADDARRAY
	Scene.GetSolver()->GetEvolution()->GetParticles().AddArray(&BodyInstances);
#endif
	// @todo: What do we do with MGravity?
	Scene.GetSolver()->GetEvolution()->AddForceFunction(Chaos::Utilities::GetRigidsGravityFunction(Chaos::TVector<float, 3>(0, 0, -1), 980.f));

	Scene.GetSolver()->PhysSceneHack = this;

}

void FPhysScene_ChaosInterface::OnWorldBeginPlay()
{
	Chaos::FPhysicsSolver* Solver = Scene.GetSolver();
	if (Solver)
	{
		Solver->SetEnabled(true);
	}
}

void FPhysScene_ChaosInterface::OnWorldEndPlay()
{
	Chaos::FPhysicsSolver* Solver = Scene.GetSolver();
	if (Solver)
	{
		Solver->SetEnabled(false);
	}
}

void FPhysScene_ChaosInterface::AddActorsToScene_AssumesLocked(TArray<FPhysicsActorHandle>& InHandles)
{
	Chaos::FPhysicsSolver* Solver = Scene.GetSolver();
	Chaos::IDispatcher* Dispatcher = Scene.GetDispatcher();
	for (FPhysicsActorHandle& Handle : InHandles)
	{
		FPhysicsInterface::AddActorToSolver(Handle, Solver, Dispatcher);
	}
}

void FPhysScene_ChaosInterface::AddAggregateToScene(const FPhysicsAggregateHandle& InAggregate)
{

}

void FPhysScene_ChaosInterface::SetOwningWorld(UWorld* InOwningWorld)
{
	MOwningWorld = InOwningWorld;
}

UWorld* FPhysScene_ChaosInterface::GetOwningWorld()
{
	return MOwningWorld;
}

const UWorld* FPhysScene_ChaosInterface::GetOwningWorld() const
{
	return MOwningWorld;
}

Chaos::FPhysicsSolver* FPhysScene_ChaosInterface::GetSolver()
{
	return Scene.GetSolver();
}

const Chaos::FPhysicsSolver* FPhysScene_ChaosInterface::GetSolver() const
{
	return Scene.GetSolver();
}

FPhysicsReplication* FPhysScene_ChaosInterface::GetPhysicsReplication()
{
	return nullptr;
}

void FPhysScene_ChaosInterface::RemoveBodyInstanceFromPendingLists_AssumesLocked(FBodyInstance* BodyInstance, int32 SceneType)
{

}

void FPhysScene_ChaosInterface::AddCustomPhysics_AssumesLocked(FBodyInstance* BodyInstance, FCalculateCustomPhysics& CalculateCustomPhysics)
{
	CalculateCustomPhysics.ExecuteIfBound(MDeltaTime, BodyInstance);
}

void FPhysScene_ChaosInterface::AddForce_AssumesLocked(FBodyInstance* BodyInstance, const FVector& Force, bool bAllowSubstepping, bool bAccelChange)
{
	// #todo : Implement
}

void FPhysScene_ChaosInterface::AddForceAtPosition_AssumesLocked(FBodyInstance* BodyInstance, const FVector& Force, const FVector& Position, bool bAllowSubstepping, bool bIsLocalForce /*= false*/)
{
	// #todo : Implement
	//AddTorque(Chaos::TVector<float, 3>::CrossProduct(Position - Scene.GetSolver()->GetRigidParticles().X(GetIndexFromId(Id)), Force), Id);
	//AddForce(Force, Id);
}

void FPhysScene_ChaosInterface::AddRadialForceToBody_AssumesLocked(FBodyInstance* BodyInstance, const FVector& Origin, const float Radius, const float Strength, const uint8 Falloff, bool bAccelChange, bool bAllowSubstepping)
{
	// #todo : Implement
#if 0
	Chaos::TVector<float, 3> Direction = (static_cast<FVector>(Scene.GetSolver()->GetRigidParticles().X(Index)) - Origin);
	Chaos::TVector<float, 3> Force(0);
	const float Distance = Direction.Size();

	if(Distance > Radius)
	{
		return;
	}

	Direction = Direction.GetSafeNormal();
	
	check(Falloff == RIF_Constant || Falloff == RIF_Linear);

	if(Falloff == RIF_Constant)
	{
		Force = Strength * Direction;
	}

	if(Falloff == RIF_Linear)
	{
		Force = (Radius - Distance) / Radius * Strength * Direction;
	}

	AddForce(bAccelChange ? (Force * Scene.GetSolver()->GetRigidParticles().M(Index)) : Force, Id);
#endif
}

void FPhysScene_ChaosInterface::ClearForces_AssumesLocked(FBodyInstance* BodyInstance, bool bAllowSubstepping)
{
}

void FPhysScene_ChaosInterface::AddTorque_AssumesLocked(FBodyInstance* BodyInstance, const FVector& Torque, bool bAllowSubstepping, bool bAccelChange)
{
	// #todo : Implement
	// AddTorque(Torque, BodyInstance->ActorHandle.GetId());
}

void FPhysScene_ChaosInterface::ClearTorques_AssumesLocked(FBodyInstance* BodyInstance, bool bAllowSubstepping)
{
	// #todo : Implement
}

void FPhysScene_ChaosInterface::SetKinematicTarget_AssumesLocked(FBodyInstance* BodyInstance, const FTransform& TargetTM, bool bAllowSubstepping)
{
	// #todo : Implement
	//for now just pass it into actor directly
	FPhysInterface_Chaos::SetKinematicTarget_AssumesLocked(BodyInstance->GetPhysicsActorHandle(), TargetTM);
}

bool FPhysScene_ChaosInterface::GetKinematicTarget_AssumesLocked(const FBodyInstance* BodyInstance, FTransform& OutTM) const
{
	OutTM = FPhysicsInterface::GetKinematicTarget_AssumesLocked(BodyInstance->ActorHandle);
	return true;
}

void FPhysScene_ChaosInterface::DeferredAddCollisionDisableTable(uint32 SkelMeshCompID, TMap<struct FRigidBodyIndexPair, bool> * CollisionDisableTable)
{

}

void FPhysScene_ChaosInterface::DeferredRemoveCollisionDisableTable(uint32 SkelMeshCompID)
{

}

void FPhysScene_ChaosInterface::MarkForPreSimKinematicUpdate(USkeletalMeshComponent* InSkelComp, ETeleportType InTeleport, bool bNeedsSkinning)
{

}

void FPhysScene_ChaosInterface::ClearPreSimKinematicUpdate(USkeletalMeshComponent* InSkelComp)
{

}

void FPhysScene_ChaosInterface::AddPendingOnConstraintBreak(FConstraintInstance* ConstraintInstance, int32 SceneType)
{

}

void FPhysScene_ChaosInterface::AddPendingSleepingEvent(FBodyInstance* BI, ESleepEvent SleepEventType, int32 SceneType)
{

}

TArray<FCollisionNotifyInfo>& FPhysScene_ChaosInterface::GetPendingCollisionNotifies(int32 SceneType)
{
	return MNotifies;
}

bool FPhysScene_ChaosInterface::SupportsOriginShifting()
{
	return false;
}

void FPhysScene_ChaosInterface::ApplyWorldOffset(FVector InOffset)
{
	check(InOffset.Size() == 0);
}

void FPhysScene_ChaosInterface::SetUpForFrame(const FVector* NewGrav, float InDeltaSeconds /*= 0.0f*/, float InMaxPhysicsDeltaTime /*= 0.0f*/)
{
	SetGravity(*NewGrav);
	MDeltaTime = InDeltaSeconds;
}

void FPhysScene_ChaosInterface::StartFrame()
{
	using namespace Chaos;

	SCOPE_CYCLE_COUNTER(STAT_Scene_StartFrame);

	FChaosSolversModule* SolverModule = FChaosSolversModule::GetModule();
	checkSlow(SolverModule);

	Chaos::IDispatcher* Dispatcher = SolverModule->GetDispatcher();

	switch(Dispatcher->GetMode())
	{
	case EChaosThreadingMode::SingleThread:
	{
		// Here we can directly tick the scene. Single threaded mode doesn't buffer any commands
		// that would require pumping here - everything is done on demand.
		Scene.Tick(MDeltaTime);
	}
		break;
	case EChaosThreadingMode::TaskGraph:
	{
		check(!CompletionEvent.GetReference())

		{
			const TArray<FPhysicsSolver*>& SolverList = SolverModule->GetSolvers();
			TArray<FPhysicsSolver*> ActiveSolvers;
			ActiveSolvers.Reserve(SolverList.Num());

			// #BG calculate active solver list once as we dispatch our first task
			for (FPhysicsSolver* Solver : SolverList)
			{
				if (Solver->HasActiveParticles())
				{
					ActiveSolvers.Add(Solver);
				}
			}

			const int32 NumActiveSolvers = ActiveSolvers.Num();

			PhysicsParallelFor(NumActiveSolvers, [&](int32 Index)
			{
				FPhysicsSolver* Solver = ActiveSolvers[Index];
				Solver->UpdatePhysicsThreadStructures();
			});
		}

		FGraphEventRef SimulationCompleteEvent = FGraphEvent::CreateGraphEvent();

		// Need to fire off a parallel task to handle running physics commands and
		// ticking the scene while the engine continues on until TG_EndPhysics
		// (this should happen in TG_StartPhysics)
		PhysicsTickTask = TGraphTask<FPhysicsTickTask>::CreateTask(nullptr, ENamedThreads::GameThread).ConstructAndDispatchWhenReady(SimulationCompleteEvent, MDeltaTime);

		// Setup post simulate tasks
		if(PhysicsTickTask.GetReference())
		{
			FGraphEventArray PostSimPrerequisites;
			PostSimPrerequisites.Add(SimulationCompleteEvent);

			DECLARE_CYCLE_STAT(TEXT("FDelegateGraphTask.CompletePhysicsSimulation"), STAT_FDelegateGraphTask_CompletePhysicsSimulation, STATGROUP_TaskGraphTasks);

			// Completion event runs in parallel and will flip out our buffers, gamethread work can be done in EndFrame (Called by world after this completion event finishes)
			CompletionEvent = FDelegateGraphTask::CreateAndDispatchWhenReady(FDelegateGraphTask::FDelegate::CreateRaw(this, &FPhysScene_ChaosInterface::CompleteSceneSimulation), GET_STATID(STAT_FDelegateGraphTask_CompletePhysicsSimulation), &PostSimPrerequisites, ENamedThreads::GameThread, ENamedThreads::AnyHiPriThreadHiPriTask);
		}
	}
		break;

	// No action for dedicated thread, the module will sync independently from the scene in
	// this case. (See FChaosSolversModule::SyncTask and FPhysicsThreadSyncCaller)
	case EChaosThreadingMode::DedicatedThread:
	default:
		break;
	}
}

void FPhysScene_ChaosInterface::EndFrame(ULineBatchComponent* InLineBatcher)
{
	using namespace Chaos;

	SCOPE_CYCLE_COUNTER(STAT_Scene_EndFrame);

	FChaosSolversModule* SolverModule = FChaosSolversModule::GetModule();
	checkSlow(SolverModule);

	Chaos::IDispatcher* Dispatcher = SolverModule->GetDispatcher();

	switch(Dispatcher->GetMode())
	{
	case EChaosThreadingMode::SingleThread:
	{
		SyncBodies();
		Scene.GetSolver()->SyncEvents_GameThread();
	}
	break;
	case EChaosThreadingMode::TaskGraph:
	{
		check(CompletionEvent->IsComplete());
		//check(PhysicsTickTask->IsComplete());
		CompletionEvent = nullptr;
		PhysicsTickTask = nullptr;

		// Flip the buffers over to the game thread and sync
		{
			SCOPE_CYCLE_COUNTER(STAT_FlipResults);

			const TArray<FPhysicsSolver*>& SolverList = SolverModule->GetSolvers();
			TArray<FPhysicsSolver*> ActiveSolvers;
			ActiveSolvers.Reserve(SolverList.Num());

			// #BG calculate active solver list once as we dispatch our first task
			for(FPhysicsSolver* Solver : SolverList)
			{
				if(Solver->HasActiveParticles())
				{
					ActiveSolvers.Add(Solver);
				}
			}

			const int32 NumActiveSolvers = ActiveSolvers.Num();

			PhysicsParallelFor(NumActiveSolvers, [&](int32 Index)
			{
				FPhysicsSolver* Solver = ActiveSolvers[Index];

				Solver->ForEachPhysicsProxy([](auto* Object)
				{
					Object->FlipBuffer();
				});

				Solver->ForEachPhysicsProxy([](auto* Object)
				{
					Object->PullFromPhysicsState();
				});

			});

			for (FPhysicsSolver* Solver : ActiveSolvers)
			{
				SyncBodies(Solver);
				Solver->SyncEvents_GameThread();
			}
		}
	}
		break;

	// No action for dedicated thread, the module will sync independently from the scene in
	// this case. (See FChaosSolversModule::SyncTask and FPhysicsThreadSyncCaller)
	case EChaosThreadingMode::DedicatedThread:
	default:
		break;
	}
}

void FPhysScene_ChaosInterface::WaitPhysScenes()
{

}

FGraphEventRef FPhysScene_ChaosInterface::GetCompletionEvent()
{
	return CompletionEvent;
}

bool FPhysScene_ChaosInterface::HandleExecCommands(const TCHAR* Cmd, FOutputDevice* Ar)
{
	return false;
}

void FPhysScene_ChaosInterface::ListAwakeRigidBodies(bool bIncludeKinematic)
{

}

int32 FPhysScene_ChaosInterface::GetNumAwakeBodies() const
{
	Chaos::FPhysicsSolver* Solver = Scene.GetSolver();
	int32 Count = 0;
#if TODO_REIMPLEMENT_GET_RIGID_PARTICLES
	uint32 ParticlesSize = Solver->GetRigidParticles().Size();
	for(uint32 ParticleIndex = 0; ParticleIndex < ParticlesSize; ++ParticleIndex)
	{
		if(!(Solver->GetRigidParticles().Disabled(ParticleIndex) || Solver->GetRigidParticles().Sleeping(ParticleIndex)))
		{
			Count++;
		}
	}
#endif
	return Count;
}

void FPhysScene_ChaosInterface::StartAsync()
{

}

bool FPhysScene_ChaosInterface::HasAsyncScene() const
{
	return false;
}

void FPhysScene_ChaosInterface::SetPhysXTreeRebuildRate(int32 RebuildRate)
{

}

void FPhysScene_ChaosInterface::EnsureCollisionTreeIsBuilt(UWorld* World)
{

}

void FPhysScene_ChaosInterface::KillVisualDebugger()
{

}

// bool FPhysScene_ChaosInterface::ExecPxVis(uint32 SceneType, const TCHAR* Cmd, FOutputDevice* Ar)
// {
// 	return true;
// }
// 
// bool FPhysScene_ChaosInterface::ExecApexVis(uint32 SceneType, const TCHAR* Cmd, FOutputDevice* Ar)
// {
// 	return true;
// }


void FPhysScene_ChaosInterface::SyncBodies()
{
	TArray<FPhysScenePendingComponentTransform_Chaos> PendingTransforms;

#if TODO_REIMPLEMENT_GET_RIGID_PARTICLES
	for(uint32 Index = 0; Index < Scene.GetSolver()->GetRigidParticles().Size(); ++Index)
	{
		if(BodyInstances[Index])
		{
			Chaos::TRigidTransform<float, 3> NewTransform(Scene.GetSolver()->GetRigidParticles().X(Index), Scene.GetSolver()->GetRigidParticles().R(Index));
			FPhysScenePendingComponentTransform_Chaos NewEntry(BodyInstances[Index]->OwnerComponent.Get(), NewTransform);
			PendingTransforms.Add(NewEntry);
		}
	}
#endif

	for(FPhysScenePendingComponentTransform_Chaos& Entry : PendingTransforms)
	{
		UPrimitiveComponent* OwnerComponent = Entry.OwningComp.Get();
		if(OwnerComponent != nullptr)
		{
			AActor* Owner = OwnerComponent->GetOwner();

			if(!Entry.NewTransform.EqualsNoScale(OwnerComponent->GetComponentTransform()))
			{
				const FVector MoveBy = Entry.NewTransform.GetLocation() - OwnerComponent->GetComponentTransform().GetLocation();
				const FQuat NewRotation = Entry.NewTransform.GetRotation();

				OwnerComponent->MoveComponent(MoveBy, NewRotation, false, NULL, MOVECOMP_SkipPhysicsMove);
			}

			if(Owner != NULL && !Owner->IsPendingKill())
			{
				Owner->CheckStillInWorld();
			}
		}
	}
}

void FPhysScene_ChaosInterface::SyncBodies(Chaos::FPhysicsSolver* Solver)
{
	TArray<FPhysScenePendingComponentTransform_Chaos> PendingTransforms;

	Solver->ForEachPhysicsProxy([this, Solver, &PendingTransforms](auto* Object)
	{
		FBodyInstance* BodyInstance = FPhysicsUserData::Get<FBodyInstance>(Object->GetUserData());
		if (BodyInstance)
		{
			if (BodyInstance->InstanceBodyIndex == INDEX_NONE && BodyInstance->OwnerComponent.IsValid())
			{
				Chaos::TRigidTransform<float, 3> NewTransform(Object->GetTransform());
				FPhysScenePendingComponentTransform_Chaos NewEntry(BodyInstance->OwnerComponent.Get(), NewTransform);
				PendingTransforms.Add(NewEntry);
			}
		}
	});

	for(FPhysScenePendingComponentTransform_Chaos& Entry : PendingTransforms)
	{
		UPrimitiveComponent* OwnerComponent = Entry.OwningComp.Get();
		if(OwnerComponent != nullptr)
		{
			AActor* Owner = OwnerComponent->GetOwner();

			if(!Entry.NewTransform.EqualsNoScale(OwnerComponent->GetComponentTransform()))
			{
				const FVector MoveBy = Entry.NewTransform.GetLocation() - OwnerComponent->GetComponentTransform().GetLocation();
				const FQuat NewRotation = Entry.NewTransform.GetRotation();

				OwnerComponent->MoveComponent(MoveBy, NewRotation, false, NULL, MOVECOMP_SkipPhysicsMove);
			}

			if(Owner != NULL && !Owner->IsPendingKill())
			{
				Owner->CheckStillInWorld();
			}
		}
	}
}



FPhysicsConstraintReference_Chaos 
FPhysScene_ChaosInterface::AddSpringConstraint(const TArray< TPair<FPhysicsActorHandle, FPhysicsActorHandle> >& Constraint)
{
	// #todo : Implement
	return FPhysicsConstraintReference_Chaos();
}

void FPhysScene_ChaosInterface::RemoveSpringConstraint(const FPhysicsConstraintReference_Chaos& Constraint)
{
	// #todo : Implement
}

void FPhysScene_ChaosInterface::CompleteSceneSimulation(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	using namespace Chaos;

	// Cache our results to the threaded buffer.
	{
		SCOPE_CYCLE_COUNTER(STAT_BufferPhysicsResults);

		FChaosSolversModule* Module = FChaosSolversModule::GetModule();

		check(Module);

		const TArray<FPhysicsSolver*>& SolverList = Module->GetSolvers();
		TArray<FPhysicsSolver*> ActiveSolvers;
		ActiveSolvers.Reserve(SolverList.Num());

		// #BG calculate active solver list once as we dispatch our first task
		for(FPhysicsSolver* Solver : SolverList)
		{
			if(Solver->HasActiveParticles())
			{
				ActiveSolvers.Add(Solver);
			}
		}

		const int32 NumActiveSolvers = ActiveSolvers.Num();

		PhysicsParallelFor(NumActiveSolvers, [&](int32 Index)
		{
			FPhysicsSolver* Solver = ActiveSolvers[Index];

			Solver->ForEachPhysicsProxyParallel([](auto* Object)
			{
				Object->BufferPhysicsResults();
			});
		});
	}
}

TSharedPtr<IPhysicsReplicationFactory> FPhysScene_ChaosInterface::PhysicsReplicationFactory;

#endif // WITH_CHAOS

#endif // INCLUDE_CHAOS

// Copyright Epic Games, Inc. All Rights Reserved.

#include "Physics/Experimental/PhysScene_Chaos.h"


#include "CoreMinimal.h"
#include "GameDelegates.h"

#include "Async/AsyncWork.h"
#include "Async/ParallelFor.h"
#include "Engine/Engine.h"

#include "Misc/CoreDelegates.h"
#include "Misc/ScopeLock.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "PhysicsReplication.h"
#include "Physics/Experimental/PhysicsUserData_Chaos.h"
#include "ProfilingDebugging/CsvProfiler.h"

#define CHAOS_INCLUDE_LEVEL_1
#include "PhysicsSolver.h"
#include "ChaosSolversModule.h"
#include "ChaosLog.h"
#include "ChaosStats.h"

#include "Field/FieldSystem.h"
#include "Framework/Dispatcher.h"
#include "Framework/PersistentTask.h"
#include "Framework/PhysicsTickTask.h"

#include "PhysicsProxy/FieldSystemPhysicsProxy.h"
#include "PhysicsProxy/GeometryCollectionPhysicsProxy.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "PhysicsProxy/SkeletalMeshPhysicsProxy.h"
#include "PhysicsProxy/StaticMeshPhysicsProxy.h"
#include "Chaos/UniformGrid.h"
#include "Chaos/BoundingVolume.h"
#include "Chaos/Framework/DebugSubstep.h"
#include "Chaos/PBDSpringConstraints.h"
#include "Chaos/PerParticleGravity.h"
#include "PBDRigidActiveParticlesBuffer.h"
#include "Chaos/GeometryParticlesfwd.h"
#include "Chaos/Box.h"
#include "ChaosSolvers/Public/EventsData.h"
#include "ChaosSolvers/Public/EventManager.h"
#undef CHAOS_INCLUDE_LEVEL_1


#if !UE_BUILD_SHIPPING
#include "Engine/World.h"
#include "DrawDebugHelpers.h"

TAutoConsoleVariable<int32> CVar_ChaosDrawHierarchyEnable(TEXT("P.Chaos.DrawHierarchy.Enable"), 0, TEXT("Enable / disable drawing of the physics hierarchy"));
TAutoConsoleVariable<int32> CVar_ChaosDrawHierarchyCells(TEXT("P.Chaos.DrawHierarchy.Cells"), 0, TEXT("Enable / disable drawing of the physics hierarchy cells"));
TAutoConsoleVariable<int32> CVar_ChaosDrawHierarchyBounds(TEXT("P.Chaos.DrawHierarchy.Bounds"), 1, TEXT("Enable / disable drawing of the physics hierarchy bounds"));
TAutoConsoleVariable<int32> CVar_ChaosDrawHierarchyObjectBounds(TEXT("P.Chaos.DrawHierarchy.ObjectBounds"), 1, TEXT("Enable / disable drawing of the physics hierarchy object bounds"));
TAutoConsoleVariable<int32> CVar_ChaosDrawHierarchyCellElementThresh(TEXT("P.Chaos.DrawHierarchy.CellElementThresh"), 128, TEXT("Num elements to consider \"high\" for cell colouring when rendering."));
TAutoConsoleVariable<int32> CVar_ChaosDrawHierarchyDrawEmptyCells(TEXT("P.Chaos.DrawHierarchy.DrawEmptyCells"), 1, TEXT("Whether to draw cells that are empty when cells are enabled."));
TAutoConsoleVariable<int32> CVar_ChaosUpdateKinematicsOnDeferredSkelMeshes(TEXT("P.Chaos.UpdateKinematicsOnDeferredSkelMeshes"), 1, TEXT("Whether to defer update kinematics for skeletal meshes."));

#endif

TAutoConsoleVariable<int32> CVar_ChaosSimulationEnable(TEXT("P.Chaos.Simulation.Enable"), 1, TEXT("Enable / disable chaos simulation. If disabled, physics will not tick."));

DECLARE_CYCLE_STAT(TEXT("Update Kinematics On Deferred SkelMeshes"), STAT_UpdateKinematicsOnDeferredSkelMeshesChaos, STATGROUP_Physics);

#if WITH_CHAOS
CSV_DEFINE_CATEGORY(ChaosPhysics, true);
#endif // WITH_CHAOS

#if WITH_EDITOR
#include "Editor.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogFPhysScene_ChaosSolver, Log, All);

#if WITH_CHAOS
Chaos::FCollisionModifierCallback FPhysScene_ChaosInterface::CollisionModifierCallback;
#endif // WITH_CHAOS

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

	virtual void Box(const Chaos::TAABB<float, 3>& InBox, const Chaos::TVector<float, 3>& InLinearColor, float InThickness) override
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

		const TArray<FPhysicsSolver*>& Solvers = ChaosModule->GetAllSolvers();

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
						const TArray<TAABB<float, 3>>& Boxes = BV->GetWorldSpaceBoxes();
						for (const TAABB<float, 3>& Box : Boxes)
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
	FVector NewTranslation;
	FQuat NewRotation;
	bool bHasValidTransform;
	bool bHasWakeEvent;
	
	FPhysScenePendingComponentTransform_Chaos(UPrimitiveComponent* InOwningComp, const FVector& InNewTranslation, const FQuat& InNewRotation, const bool InHasWakeEvent)
		: OwningComp(InOwningComp)
		, NewTranslation(InNewTranslation)
		, NewRotation(InNewRotation)
		, bHasValidTransform(true)
		, bHasWakeEvent(InHasWakeEvent)
	{}

	FPhysScenePendingComponentTransform_Chaos(UPrimitiveComponent* InOwningComp)
		: OwningComp(InOwningComp)
		, bHasValidTransform(false)
		, bHasWakeEvent(true)
	{}

};

FPhysScene_Chaos::FPhysScene_Chaos(AActor* InSolverActor
#if CHAOS_CHECKED
	, const FName& DebugName
#endif
)
	: PhysicsReplication(nullptr)
	, ChaosModule(nullptr)
	, SceneSolver(nullptr)
	, SolverActor(InSolverActor)
#if WITH_EDITOR
	, SingleStepCounter(0)
#endif
#if CHAOS_WITH_PAUSABLE_SOLVER
	, bIsWorldPaused(false)
#endif
{
	LLM_SCOPE(ELLMTag::Chaos);

	ChaosModule = FModuleManager::Get().GetModulePtr<FChaosSolversModule>("ChaosSolvers");
	check(ChaosModule);

	UWorld* WorldPtr = SolverActor.Get() ? SolverActor->GetWorld() : nullptr;

	SceneSolver = ChaosModule->CreateSolver(WorldPtr
#if CHAOS_CHECKED
	, DebugName
#endif
);
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
	if(!PhysScene_ChaosPauseHandler)
	{
		PhysScene_ChaosPauseHandler = MakeUnique<FPhysScene_ChaosPauseHandler>(ChaosModule);
	}
#endif

	Chaos::FEventManager* EventManager = SceneSolver->GetEventManager();
	EventManager->RegisterHandler<Chaos::FCollisionEventData>(Chaos::EEventType::Collision, this, &FPhysScene_Chaos::HandleCollisionEvents);
}

FPhysScene_Chaos::~FPhysScene_Chaos()
{
#if WITH_CHAOS
	if (IPhysicsReplicationFactory* RawReplicationFactory = FPhysScene_ChaosInterface::PhysicsReplicationFactory.Get())
	{
		RawReplicationFactory->Destroy(PhysicsReplication);
	}
	else if(PhysicsReplication)
	{
		delete PhysicsReplication;
	}
#endif

	if (SceneSolver)
	{
		Chaos::FEventManager* EventManager = SceneSolver->GetEventManager();
		EventManager->UnregisterHandler(Chaos::EEventType::Collision, this);
	}

	Shutdown();
	
	FCoreDelegates::OnPreExit.RemoveAll(this);

#if CHAOS_WITH_PAUSABLE_SOLVER
	if (SyncCaller)
	{
		SyncCaller->OnUpdateWorldPause.RemoveAll(this);
	}
#endif
}

#if WITH_EDITOR && WITH_CHAOS
bool FPhysScene_ChaosInterface::IsOwningWorldEditor() const
{
	const UWorld* WorldPtr = GetOwningWorld();
	const TIndirectArray<FWorldContext>& WorldContexts = GEngine->GetWorldContexts();
	for (const FWorldContext& Context : WorldContexts)
	{
		if (WorldPtr)
		{
			if (WorldPtr == Context.World())
			{
				if (Context.WorldType == EWorldType::Editor)
				{
					return true;
				}
			}
		}
	}

	return false;
}
#endif

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
	SCOPE_CYCLE_COUNTER(STAT_ChaosTick);
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(Physics);
	LLM_SCOPE(ELLMTag::Chaos);

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
}

Chaos::FPhysicsSolver* FPhysScene_Chaos::GetSolver() const
{
	return SceneSolver;
}

AActor* FPhysScene_Chaos::GetSolverActor() const
{
	return SolverActor.Get();
}

void FPhysScene_Chaos::RegisterForCollisionEvents(UPrimitiveComponent* Component)
{
	CollisionEventRegistrations.AddUnique(Component);
}

void FPhysScene_Chaos::UnRegisterForCollisionEvents(UPrimitiveComponent* Component)
{
	CollisionEventRegistrations.Remove(Component);
}

Chaos::IDispatcher* FPhysScene_Chaos::GetDispatcher() const
{
	return ChaosModule ? ChaosModule->GetDispatcher() : nullptr;
}

template<typename ObjectType>
void AddPhysicsProxy(ObjectType* InObject, Chaos::FPhysicsSolver* InSolver, Chaos::IDispatcher* InDispatcher)
{
	ensure(false);
}

void FPhysScene_Chaos::AddObject(UPrimitiveComponent* Component, FSkeletalMeshPhysicsProxy* InObject)
{
	AddToComponentMaps(Component, InObject);
	ensure(false);
}

void FPhysScene_Chaos::AddObject(UPrimitiveComponent* Component, FStaticMeshPhysicsProxy* InObject)
{
	AddToComponentMaps(Component, InObject);
	ensure(false);
}

void FPhysScene_Chaos::AddObject(UPrimitiveComponent* Component, FGeometryParticlePhysicsProxy* InObject)
{
	AddToComponentMaps(Component, InObject);
	ensure(false);
}

void FPhysScene_Chaos::AddObject(UPrimitiveComponent* Component, FGeometryCollectionPhysicsProxy* InObject)
{
	AddToComponentMaps(Component, InObject);

	Chaos::FPhysicsSolver* Solver = GetSolver();
	Solver->RegisterObject(InObject);
}

void FPhysScene_Chaos::AddObject(UPrimitiveComponent* Component, FFieldSystemPhysicsProxy* InObject)
{
	AddToComponentMaps(Component, InObject);

	Chaos::FPhysicsSolver* CurrSceneSolver = GetSolver();

	InObject->SetSolver(CurrSceneSolver);
	InObject->Initialize();

	if (Chaos::IDispatcher* Dispatcher = GetDispatcher())
	{
		TArray<Chaos::FPhysicsSolver*> WorldSolverList = ChaosModule->GetAllSolvers();

		for(Chaos::FPhysicsSolver* Solver : WorldSolverList)
		{
			if(true || Solver->HasActiveParticles())
			{
				Solver->RegisterObject(InObject);

				if(/*bDedicatedThread && */Dispatcher)
				{
					// Pass the proxy off to the physics thread
					Dispatcher->EnqueueCommandImmediate([InObject, InSolver = Solver](Chaos::FPersistentPhysicsTask* PhysThread)
						{
							InSolver->RegisterObject(InObject);
						});
				}
			}
		}
	}
}

void FPhysScene_Chaos::RemoveActorFromAccelerationStructure(FPhysicsActorHandle& Actor)
{
#if WITH_CHAOS
	if (GetSpacialAcceleration() && Actor->UniqueIdx().IsValid())
	{
		ExternalDataLock.WriteLock();
		Chaos::TAccelerationStructureHandle<float, 3> AccelerationHandle(Actor);
		GetSpacialAcceleration()->RemoveElementFrom(AccelerationHandle, Actor->SpatialIdx());
		ExternalDataLock.WriteUnlock();
	}
#endif
}

void FPhysScene_Chaos::UpdateActorInAccelerationStructure(const FPhysicsActorHandle& Actor)
{
#if WITH_CHAOS
	using namespace Chaos;

	if (GetSpacialAcceleration())
	{
		ExternalDataLock.WriteLock();

		auto SpatialAcceleration = GetSpacialAcceleration();

		if (SpatialAcceleration)
		{

			TAABB<FReal, 3> WorldBounds;
			const bool bHasBounds = Actor->Geometry()->HasBoundingBox();
			if (bHasBounds)
			{
				WorldBounds = Actor->Geometry()->BoundingBox().TransformedAABB(TRigidTransform<FReal, 3>(Actor->X(), Actor->R()));
			}


			Chaos::TAccelerationStructureHandle<float, 3> AccelerationHandle(Actor);
			SpatialAcceleration->UpdateElementIn(AccelerationHandle, WorldBounds, bHasBounds, Actor->SpatialIdx());
		}

		ExternalDataLock.WriteUnlock();
	}
#endif
}

void FPhysScene_Chaos::UpdateActorsInAccelerationStructure(const TArrayView<FPhysicsActorHandle>& Actors)
{
#if WITH_CHAOS
	using namespace Chaos;

	if (GetSpacialAcceleration())
	{
		ExternalDataLock.WriteLock();

		auto SpatialAcceleration = GetSpacialAcceleration();

		if (SpatialAcceleration)
		{
			int32 NumActors = Actors.Num();
			for (int32 ActorIndex = 0; ActorIndex < NumActors; ++ActorIndex)
			{
				const FPhysicsActorHandle& Actor = Actors[ActorIndex];
				if (Actor != nullptr)
				{
					// @todo(chaos): dedupe code in UpdateActorInAccelerationStructure
					TAABB<FReal, 3> WorldBounds;
					const bool bHasBounds = Actor->Geometry()->HasBoundingBox();
					if (bHasBounds)
					{
						WorldBounds = Actor->Geometry()->BoundingBox().TransformedAABB(TRigidTransform<FReal, 3>(Actor->X(), Actor->R()));
					}

					Chaos::TAccelerationStructureHandle<float, 3> AccelerationHandle(Actor);
					SpatialAcceleration->UpdateElementIn(AccelerationHandle, WorldBounds, bHasBounds, Actor->SpatialIdx());
				}
			}
		}

		ExternalDataLock.WriteUnlock();
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

	// Remove the object from the solver
	PhysDispatcher->EnqueueCommandImmediate(InSolver, [InObject, bDedicatedThread](Chaos::FPBDRigidsSolver* InnerSolver)
	{
#if CHAOS_PARTICLEHANDLE_TODO
		InnerSolver->UnregisterObject(InObject);
#endif
		InObject->OnRemoveFromScene();

		if (!bDedicatedThread)
		{
			InObject->SyncBeforeDestroy();
			delete InObject;
		}

	});
}

void FPhysScene_Chaos::RemoveObject(FSkeletalMeshPhysicsProxy* InObject)
{
	ensure(false);
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
	ensure(false);
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
	ensure(false);
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
	Chaos::FPhysicsSolver* Solver = InObject->GetSolver();
	if(Solver && !Solver->UnregisterObject(InObject))
	{
		UE_LOG(LogChaos, Warning, TEXT("Attempted to remove an object that wasn't found in its solver's gamethread storage - it's likely the solver has been mistakenly changed."));
	}
	RemoveFromComponentMaps(InObject);
	RemovePhysicsProxy(InObject, Solver, ChaosModule);
}

void FPhysScene_Chaos::RemoveObject(FFieldSystemPhysicsProxy* InObject)
{
	Chaos::FPhysicsSolver* CurrSceneSolver = InObject->GetSolver();
	if(CurrSceneSolver)
	{
		if(!CurrSceneSolver->UnregisterObject(InObject))
		{
			UE_LOG(LogChaos, Warning, TEXT("Attempted to remove an object that wasn't found in its solver's gamethread storage - it's likely the solver has been mistakenly changed."));
		}
		RemoveFromComponentMaps(InObject);

		if(Chaos::IDispatcher* Dispatcher = GetDispatcher())
		{
			TArray<Chaos::FPhysicsSolver*> SolverList = ChaosModule->GetAllSolvers();

			for(Chaos::FPhysicsSolver* Solver : SolverList)
			{
				if(true || Solver->HasActiveParticles())
				{
					Solver->UnregisterObject(InObject);

					if(/*bDedicatedThread && */Dispatcher)
					{
						// Pass the proxy off to the physics thread
						Dispatcher->EnqueueCommandImmediate([InObject, InSolver = Solver](Chaos::FPersistentPhysicsTask* PhysThread)
							{
								InSolver->UnregisterObject(InObject);
							});
					}
				}
			}
		}
	}
	else
	{
		UE_LOG(LogChaos, Warning, TEXT("Attempted to remove an object but no solver had been set."));
	}
}

#if XGE_FIXED
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
#endif // XGE_FIXED

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

FPhysicsReplication* FPhysScene_Chaos::GetPhysicsReplication()
{
	return PhysicsReplication;
}

void FPhysScene_Chaos::SetPhysicsReplication(FPhysicsReplication* InPhysicsReplication)
{
	PhysicsReplication = InPhysicsReplication;
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

const Chaos::ISpatialAcceleration<Chaos::TAccelerationStructureHandle<float, 3>, float, 3>* FPhysScene_Chaos::GetSpacialAcceleration() const
{
	if(GetDispatcher() && GetDispatcher()->GetMode() == Chaos::EThreadingMode::SingleThread)
	{
		return GetSolver()->GetEvolution()->GetSpatialAcceleration();
	}

	return SolverAccelerationStructure.Get();
}

Chaos::ISpatialAcceleration<Chaos::TAccelerationStructureHandle<float, 3>, float, 3>* FPhysScene_Chaos::GetSpacialAcceleration()
{
	if(GetDispatcher() && GetDispatcher()->GetMode() == Chaos::EThreadingMode::SingleThread)
	{
		return GetSolver()->GetEvolution()->GetSpatialAcceleration();
	}

	return SolverAccelerationStructure.Get();
}

void FPhysScene_Chaos::CopySolverAccelerationStructure()
{
	if (SceneSolver && GetDispatcher()->GetMode() != Chaos::EThreadingMode::SingleThread)
	{
		ExternalDataLock.WriteLock();
		SceneSolver->GetEvolution()->UpdateExternalAccelerationStructure(SolverAccelerationStructure);
		ExternalDataLock.WriteUnlock();
	}
}

static void SetCollisionInfoFromComp(FRigidBodyCollisionInfo& Info, UPrimitiveComponent* Comp)
{
	if (Comp)
	{
		Info.Component = Comp;
		Info.Actor = Comp->GetOwner();

		const FBodyInstance* const BodyInst = Comp->GetBodyInstance();
		Info.BodyIndex = BodyInst ? BodyInst->InstanceBodyIndex : INDEX_NONE;
		Info.BoneName = BodyInst && BodyInst->BodySetup.IsValid() ? BodyInst->BodySetup->BoneName : NAME_None;
	}
	else
	{
		Info.Component = nullptr;
		Info.Actor = nullptr;
		Info.BodyIndex = INDEX_NONE;
		Info.BoneName = NAME_None;
	}
}

FCollisionNotifyInfo& FPhysScene_Chaos::GetPendingCollisionForContactPair(const void* P0, const void* P1, bool& bNewEntry)
{
	const FUniqueContactPairKey Key = { P0, P1 };
	const int32* PendingNotifyIdx = ContactPairToPendingNotifyMap.Find(Key);
	if (PendingNotifyIdx)
	{
		// we already have one for this pair
		bNewEntry = false;
		return PendingCollisionNotifies[*PendingNotifyIdx];
	}

	// make a new entry
	bNewEntry = true;
	int32 NewIdx = PendingCollisionNotifies.AddZeroed();
	return PendingCollisionNotifies[NewIdx];
}

void FPhysScene_Chaos::HandleCollisionEvents(const Chaos::FCollisionEventData& Event)
{

	ContactPairToPendingNotifyMap.Reset();

	TMap<IPhysicsProxyBase*, TArray<int32>> const& PhysicsProxyToCollisionIndicesMap = Event.PhysicsProxyToCollisionIndices.PhysicsProxyToIndicesMap;
	Chaos::FCollisionDataArray const& CollisionData = Event.CollisionData.AllCollisionsArray;

	int32 NumCollisions = CollisionData.Num();
	if (NumCollisions > 0)
	{
		// look through all the components that someone is interested in, and see if they had a collision
		// note that we only need to care about the interaction from the POV of the registered component,
		// since if anyone wants notifications for the other component it hit, it's also registered and we'll get to that elsewhere in the list
		for (TArray<UPrimitiveComponent*>::TIterator It(CollisionEventRegistrations); It; ++It)
		{
			UPrimitiveComponent* const Comp0 = *It;
			IPhysicsProxyBase* const PhysicsProxy0 = GetOwnedPhysicsProxy(Comp0);
			TArray<int32> const* const CollisionIndices = PhysicsProxyToCollisionIndicesMap.Find(PhysicsProxy0);
			if (CollisionIndices)
			{
				for (int32 EncodedCollisionIdx : *CollisionIndices)
				{
					bool bSwapOrder;
					int32 CollisionIdx = Chaos::FEventManager::DecodeCollisionIndex(EncodedCollisionIdx, bSwapOrder);

					Chaos::TCollisionData<float, 3> const& CollisionDataItem = CollisionData[CollisionIdx];
					IPhysicsProxyBase* const PhysicsProxy1 = bSwapOrder ? CollisionDataItem.ParticleProxy : CollisionDataItem.LevelsetProxy;

					{
						bool bNewEntry = false;
						FCollisionNotifyInfo& NotifyInfo = GetPendingCollisionForContactPair(PhysicsProxy0, PhysicsProxy1, bNewEntry);

						// #note: we only notify on the first contact, though we will still accumulate the impulse data from subsequent contacts
						const FVector NormalImpulse = FVector::DotProduct(CollisionDataItem.AccumulatedImpulse, CollisionDataItem.Normal) * CollisionDataItem.Normal;	// project impulse along normal
						const FVector FrictionImpulse = FVector(CollisionDataItem.AccumulatedImpulse) - NormalImpulse; // friction is component not along contact normal
						NotifyInfo.RigidCollisionData.TotalNormalImpulse += NormalImpulse;
						NotifyInfo.RigidCollisionData.TotalFrictionImpulse += FrictionImpulse;

						if (bNewEntry)
						{
							UPrimitiveComponent* const Comp1 = GetOwningComponent<UPrimitiveComponent>(PhysicsProxy1);

							// fill in legacy contact data
							NotifyInfo.bCallEvent0 = true;
							// if Comp1 wants this event too, it will get its own pending collision entry, so we leave it false

							SetCollisionInfoFromComp(NotifyInfo.Info0, Comp0);
							SetCollisionInfoFromComp(NotifyInfo.Info1, Comp1);

							FRigidBodyContactInfo& NewContact = NotifyInfo.RigidCollisionData.ContactInfos.AddZeroed_GetRef();
							NewContact.ContactNormal = CollisionDataItem.Normal;
							NewContact.ContactPosition = CollisionDataItem.Location;
							NewContact.ContactPenetration = CollisionDataItem.PenetrationDepth;
							// NewContact.PhysMaterial[1] UPhysicalMaterial required here
						}
					}
				}
			}
		}
	}

	// Tell the world and actors about the collisions
	DispatchPendingCollisionNotifies();
}

void FPhysScene_Chaos::DispatchPendingCollisionNotifies()
{
	//UWorld const* const OwningWorld = GetWorld();

	//// Let the game-specific PhysicsCollisionHandler process any physics collisions that took place
	//if (OwningWorld != nullptr && OwningWorld->PhysicsCollisionHandler != nullptr)
	//{
	//	OwningWorld->PhysicsCollisionHandler->HandlePhysicsCollisions_AssumesLocked(PendingCollisionNotifies);
	//}

	// Fire any collision notifies in the queue.
	for (FCollisionNotifyInfo& NotifyInfo : PendingCollisionNotifies)
	{
		//		if (NotifyInfo.RigidCollisionData.ContactInfos.Num() > 0)
		{
			if (NotifyInfo.bCallEvent0 && /*NotifyInfo.IsValidForNotify() && */ NotifyInfo.Info0.Actor.IsValid())
			{
				NotifyInfo.Info0.Actor->DispatchPhysicsCollisionHit(NotifyInfo.Info0, NotifyInfo.Info1, NotifyInfo.RigidCollisionData);
			}

			// CHAOS: don't call event 1, because the code below will generate the reflexive hit data as separate entries
		}
	}

	PendingCollisionNotifies.Reset();
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

void FPhysScene_Chaos::OnWorldEndPlay()
{
#if WITH_EDITOR
	// Mark PIE modified objects dirty - couldn't do this during the run because
	// it's silently ignored
	for(UObject* Obj : PieModifiedObjects)
	{
		Obj->Modify();
	}

	PieModifiedObjects.Reset();
#endif

	PhysicsProxyToComponentMap.Reset();
	ComponentToPhysicsProxyMap.Reset();
}

#if WITH_EDITOR
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

FPhysScene_ChaosInterface::FPhysScene_ChaosInterface(const AWorldSettings* InSettings /*= nullptr*/
#if CHAOS_CHECKED
	, const FName& DebugName
#endif
)	: Scene(nullptr
#if CHAOS_CHECKED
	, DebugName
#endif
)
{
	//Initialize unique ptrs that are just here to allow forward declare. This should be reworked todo(ocohen)
#if TODO_FIX_REFERENCES_TO_ADDARRAY
	BodyInstances = MakeUnique<Chaos::TArrayCollectionArray<FBodyInstance*>>();
	Scene.GetSolver()->GetEvolution()->GetParticles().AddArray(BodyInstances.Get());
#endif

	// Create replication manager
	FPhysicsReplication* PhysicsReplication = PhysicsReplicationFactory.IsValid() ? PhysicsReplicationFactory->Create(this) : new FPhysicsReplication(this);
	Scene.SetPhysicsReplication(PhysicsReplication);

	Scene.GetSolver()->PhysSceneHack = this;

	Scene.GetSolver()->GetEvolution()->SetCollisionModifierCallback(CollisionModifierCallback);
}

void FPhysScene_ChaosInterface::OnWorldBeginPlay()
{
	Chaos::FPhysicsSolver* Solver = Scene.GetSolver();
	if (Solver)
	{
		Solver->SetEnabled(true);
	}

#if WITH_EDITOR
	const UWorld* WorldPtr = GetOwningWorld();
	const TIndirectArray<FWorldContext>& WorldContexts = GEngine->GetWorldContexts();
	for (const FWorldContext& Context : WorldContexts)
	{
		if (Context.WorldType == EWorldType::Editor)
		{
			UWorld* World = Context.World();
			if (World)
			{
				auto PhysScene = World->GetPhysicsScene();
				if (PhysScene)
				{
					auto InnerSolver = PhysScene->GetSolver();
					if (InnerSolver)
					{
						InnerSolver->SetEnabled(false);
					}
				}
			}
		}
	}
#endif

}

void FPhysScene_ChaosInterface::OnWorldEndPlay()
{
	Chaos::FPhysicsSolver* Solver = Scene.GetSolver();
	if (Solver)
	{
		Solver->SetEnabled(false);

	}

#if WITH_EDITOR
	const UWorld* WorldPtr = GetOwningWorld();
	const TIndirectArray<FWorldContext>& WorldContexts = GEngine->GetWorldContexts();
	for (const FWorldContext& Context : WorldContexts)
	{
		if (Context.WorldType == EWorldType::Editor)
		{
			UWorld* World = Context.World();
			if (World)
			{
				auto PhysScene = World->GetPhysicsScene();
				if (PhysScene)
				{
					auto InnerSolver = PhysScene->GetSolver();
					if (InnerSolver)
					{
						InnerSolver->SetEnabled(true);
					}
				}
			}
		}
	}
#endif

	Scene.OnWorldEndPlay();
}

void FPhysScene_ChaosInterface::AddActorsToScene_AssumesLocked(TArray<FPhysicsActorHandle>& InHandles, const bool bImmediate)
{
	Chaos::FPhysicsSolver* Solver = Scene.GetSolver();
	Chaos::IDispatcher* Dispatcher = Scene.GetDispatcher();
	Chaos::ISpatialAcceleration<Chaos::TAccelerationStructureHandle<float, 3>, float, 3>* SpatialAcceleration = Scene.GetSpacialAcceleration();
	for (FPhysicsActorHandle& Handle : InHandles)
	{
		FPhysicsInterface::AddActorToSolver(Handle, Solver, Dispatcher);

		// Optionally add this to the game-thread acceleration structure immediately
		if (bImmediate && SpatialAcceleration)
		{
			// Get the bounding box for the particle if it has one
			bool bHasBounds = Handle->Geometry()->HasBoundingBox();
			Chaos::TAABB<float, 3> WorldBounds;
			if (bHasBounds)
			{
				const Chaos::TAABB<float, 3> LocalBounds = Handle->Geometry()->BoundingBox();
				WorldBounds = LocalBounds.TransformedAABB(Chaos::TRigidTransform<float, 3>(Handle->X(), Handle->R()));
			}

			// Insert the particle
			Chaos::TAccelerationStructureHandle<float, 3> AccelerationHandle(Handle);
			SpatialAcceleration->UpdateElementIn(AccelerationHandle, WorldBounds, bHasBounds, Handle->SpatialIdx());
		}
	}
}

void FPhysScene_ChaosInterface::AddAggregateToScene(const FPhysicsAggregateHandle& InAggregate)
{

}

void FPhysScene_ChaosInterface::SetOwningWorld(UWorld* InOwningWorld)
{
	MOwningWorld = InOwningWorld;

#if WITH_EDITOR
	if (IsOwningWorldEditor())
	{
		GetScene().GetSolver()->SetEnabled(true);
	}
#endif

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

void FPhysScene_ChaosInterface::Flush_AssumesLocked()
{
	check(IsInGameThread());

	// Flush all of our pending commands
	Chaos::IDispatcher* Dispatcher = FChaosSolversModule::GetModule()->GetDispatcher();

	if(Dispatcher->GetMode() != Chaos::EThreadingMode::SingleThread)
	{
		Dispatcher->Execute();
	}

	Chaos::FPBDRigidsSolver* Solver = GetSolver();

	if(Solver)
	{
		TQueue<TFunction<void(Chaos::FPhysicsSolver*)>, EQueueMode::Mpsc>& Queue = Solver->GetCommandQueue();
		TFunction<void(Chaos::FPhysicsSolver*)> Command;
		while(Queue.Dequeue(Command))
		{
			Command(Solver);
		}

		// Populate the spacial acceleration
		Chaos::FPBDRigidsSolver::FPBDRigidsEvolution* Evolution = Solver->GetEvolution();

		if(Evolution)
		{
			Evolution->FlushSpatialAcceleration();
		}
	}

	Scene.CopySolverAccelerationStructure();
}

FPhysicsReplication* FPhysScene_ChaosInterface::GetPhysicsReplication()
{
	return Scene.GetPhysicsReplication();
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
	using namespace Chaos;

	FPhysicsActorHandle& Handle = BodyInstance->GetPhysicsActorHandle();
	if (FPhysicsInterface::IsValid(Handle))
	{
		Chaos::TPBDRigidParticle<float, 3>* Rigid = Handle->CastToRigidParticle();
		if(Rigid)
		{
			EObjectStateType ObjectState = Rigid->ObjectState();
			if (CHAOS_ENSURE(ObjectState == EObjectStateType::Dynamic || ObjectState == EObjectStateType::Sleeping))
			{
				Rigid->SetObjectState(EObjectStateType::Dynamic);

				const Chaos::TVector<float, 3> CurrentForce = Rigid->F();
				if (bAccelChange)
				{
					const float Mass = Rigid->M();
					const Chaos::TVector<float, 3> TotalAcceleration = CurrentForce + (Force * Mass);
					Rigid->SetF(TotalAcceleration);
				}
				else
				{
					Rigid->SetF(CurrentForce + Force);
				}

			}
		}
	}
}

void FPhysScene_ChaosInterface::AddForceAtPosition_AssumesLocked(FBodyInstance* BodyInstance, const FVector& Force, const FVector& Position, bool bAllowSubstepping, bool bIsLocalForce /*= false*/)
{
	using namespace Chaos;

	FPhysicsActorHandle& Handle = BodyInstance->GetPhysicsActorHandle();
	if (ensure(FPhysicsInterface::IsValid(Handle)))
	{
		Chaos::TPBDRigidParticle<float, 3>* Rigid = Handle->CastToRigidParticle();
		
		if (ensure(Rigid))
		{
			EObjectStateType ObjectState = Rigid->ObjectState();
			if (CHAOS_ENSURE(ObjectState == EObjectStateType::Dynamic || ObjectState == EObjectStateType::Sleeping))
			{
				const Chaos::FVec3& CurrentForce = Rigid->F();
				const Chaos::FVec3& CurrentTorque = Rigid->Torque();
				const Chaos::FVec3 WorldCOM = FParticleUtilitiesGT::GetCoMWorldPosition(Rigid);

				Rigid->SetObjectState(EObjectStateType::Dynamic);

				if (bIsLocalForce)
				{
					const Chaos::FRigidTransform3 CurrentTransform = FParticleUtilitiesGT::GetActorWorldTransform(Rigid);
					const Chaos::FVec3 WorldPosition = CurrentTransform.TransformPosition(Position);
					const Chaos::FVec3 WorldForce = CurrentTransform.TransformVector(Force);
					const Chaos::FVec3 WorldTorque = Chaos::FVec3::CrossProduct(WorldPosition - WorldCOM, WorldForce);
					Rigid->SetF(CurrentForce + WorldForce);
					Rigid->SetTorque(CurrentTorque + WorldTorque);
				}
				else
				{
					const Chaos::FVec3 WorldTorque = Chaos::FVec3::CrossProduct(Position - WorldCOM, Force);
					Rigid->SetF(CurrentForce + Force);
					Rigid->SetTorque(CurrentTorque + WorldTorque);
				}

			}
		}
	}
}

void FPhysScene_ChaosInterface::AddRadialForceToBody_AssumesLocked(FBodyInstance* BodyInstance, const FVector& Origin, const float Radius, const float Strength, const uint8 Falloff, bool bAccelChange, bool bAllowSubstepping)
{
	FPhysicsActorHandle& Handle = BodyInstance->GetPhysicsActorHandle();
	if (ensure(FPhysicsInterface::IsValid(Handle)))
	{
		Chaos::TPBDRigidParticle<float, 3>* Rigid = Handle->CastToRigidParticle();
		
		if (ensure(Rigid))
		{
			Chaos::EObjectStateType ObjectState = Rigid->ObjectState();
			if (CHAOS_ENSURE(ObjectState == Chaos::EObjectStateType::Dynamic || ObjectState == Chaos::EObjectStateType::Sleeping))
			{
				const Chaos::FVec3& CurrentForce = Rigid->F();
				const Chaos::FVec3& CurrentTorque = Rigid->Torque();
				const Chaos::FVec3 WorldCOM = Chaos::FParticleUtilitiesGT::GetCoMWorldPosition(Rigid);

				Chaos::FVec3 Direction = WorldCOM - Origin;
				const float Distance = Direction.Size();
				if (Distance > Radius)
				{
					return;
				}

				Rigid->SetObjectState(Chaos::EObjectStateType::Dynamic);

				if (Distance < 1e-4)
				{
					Direction = Chaos::FVec3(1, 0, 0);
				}
				else
				{
					Direction = Direction.GetUnsafeNormal();
				}
				Chaos::FVec3 Force(0, 0, 0);
				CHAOS_ENSURE(Falloff < RIF_MAX);
				if (Falloff == ERadialImpulseFalloff::RIF_Constant)
				{
					Force = Strength * Direction;
				}
				if (Falloff == ERadialImpulseFalloff::RIF_Linear)
				{
					Force = (Radius - Distance) / Radius * Strength * Direction;
				}
				if (bAccelChange)
				{
					const float Mass = Rigid->M();
					const Chaos::TVector<float, 3> TotalAcceleration = CurrentForce + (Force * Mass);
					Rigid->SetF(TotalAcceleration);
				}
				else
				{
					Rigid->SetF(CurrentForce + Force);
				}
			}
		}
	}
}

void FPhysScene_ChaosInterface::ClearForces_AssumesLocked(FBodyInstance* BodyInstance, bool bAllowSubstepping)
{
	FPhysicsActorHandle& Handle = BodyInstance->GetPhysicsActorHandle();
	if (ensure(FPhysicsInterface::IsValid(Handle)))
	{
		Chaos::TPBDRigidParticle<float, 3>* Rigid = Handle->CastToRigidParticle();
		if (ensure(Rigid))
		{
			Rigid->SetF(Chaos::TVector<float, 3>(0.f,0.f,0.f));
		}
	}
}

void FPhysScene_ChaosInterface::AddTorque_AssumesLocked(FBodyInstance* BodyInstance, const FVector& Torque, bool bAllowSubstepping, bool bAccelChange)
{
	using namespace Chaos;

	FPhysicsActorHandle& Handle = BodyInstance->GetPhysicsActorHandle();
	if (ensure(FPhysicsInterface::IsValid(Handle)))
	{
		Chaos::TPBDRigidParticle<float, 3>* Rigid = Handle->CastToRigidParticle();
		
		if (ensure(Rigid))
		{
			EObjectStateType ObjectState = Rigid->ObjectState();
			if (CHAOS_ENSURE(ObjectState == EObjectStateType::Dynamic || ObjectState == EObjectStateType::Sleeping))
			{
				const Chaos::TVector<float, 3> CurrentTorque = Rigid->Torque();
				if (bAccelChange)
				{
					Rigid->SetTorque(CurrentTorque + (FParticleUtilitiesXR::GetWorldInertia(Rigid) * Torque));
				}
				else
				{
					Rigid->SetTorque(CurrentTorque + Torque);
				}
			}
		}
	}
}

void FPhysScene_ChaosInterface::ClearTorques_AssumesLocked(FBodyInstance* BodyInstance, bool bAllowSubstepping)
{
	FPhysicsActorHandle& Handle = BodyInstance->GetPhysicsActorHandle();
	if (ensure(FPhysicsInterface::IsValid(Handle)))
	{
		Chaos::TPBDRigidParticle<float, 3>* Rigid = Handle->CastToRigidParticle();
		if (ensure(Rigid))
		{
			Rigid->SetTorque(Chaos::TVector<float, 3>(0.f, 0.f, 0.f));
		}
	}
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

bool FPhysScene_ChaosInterface::MarkForPreSimKinematicUpdate(USkeletalMeshComponent* InSkelComp, ETeleportType InTeleport, bool bNeedsSkinning)
{
#if !UE_BUILD_SHIPPING
	const bool bDeferredUpdate = CVar_ChaosUpdateKinematicsOnDeferredSkelMeshes.GetValueOnGameThread() != 0;
	if (!bDeferredUpdate)
	{
		return false;
	}
#endif

	// If null, or pending kill, do nothing
	if (InSkelComp != nullptr && !InSkelComp->IsPendingKill())
	{
		// If we are already flagged, just need to update info
		if (InSkelComp->DeferredKinematicUpdateIndex != INDEX_NONE)
		{
			FDeferredKinematicUpdateInfo& Info = DeferredKinematicUpdateSkelMeshes[InSkelComp->DeferredKinematicUpdateIndex].Value;

			// If we are currently not going to teleport physics, but this update wants to, we 'upgrade' it
			if (Info.TeleportType == ETeleportType::None && InTeleport == ETeleportType::TeleportPhysics)
			{
				Info.TeleportType = ETeleportType::TeleportPhysics;
			}

			// If we need skinning, remember that
			if (bNeedsSkinning)
			{
				Info.bNeedsSkinning = true;
			}
		}
		// We are not flagged yet..
		else
		{
			// Set info and add to map
			FDeferredKinematicUpdateInfo Info;
			Info.TeleportType = InTeleport;
			Info.bNeedsSkinning = bNeedsSkinning;
			InSkelComp->DeferredKinematicUpdateIndex = DeferredKinematicUpdateSkelMeshes.Num();
			DeferredKinematicUpdateSkelMeshes.Emplace(InSkelComp, Info);
		}
	}

	return true;
}

void FPhysScene_ChaosInterface::ClearPreSimKinematicUpdate(USkeletalMeshComponent* InSkelComp)
{
	if (InSkelComp != nullptr)
	{
		const int32 DeferredKinematicUpdateIndex = InSkelComp->DeferredKinematicUpdateIndex;
		if (DeferredKinematicUpdateIndex != INDEX_NONE)
		{
			DeferredKinematicUpdateSkelMeshes.Last().Key->DeferredKinematicUpdateIndex = DeferredKinematicUpdateIndex;
			DeferredKinematicUpdateSkelMeshes.RemoveAtSwap(InSkelComp->DeferredKinematicUpdateIndex);
			InSkelComp->DeferredKinematicUpdateIndex = INDEX_NONE;
		}
	}
}

// Collect all the actors that need moving, along with their transforms
// Extracted from USkeletalMeshComponent::UpdateKinematicBonesToAnim
// @todo(chaos): merge this functionality back into USkeletalMeshComponent
template<typename T_ACTORCONTAINER, typename T_TRANSFORMCONTAINER>
void GatherActorsAndTransforms(
	USkeletalMeshComponent* SkelComp, 
	const TArray<FTransform>& InComponentSpaceTransforms, 
	ETeleportType Teleport, 
	bool bNeedsSkinning, 
	T_ACTORCONTAINER& KinematicUpdateActors,
	T_TRANSFORMCONTAINER& KinematicUpdateTransforms,
	T_ACTORCONTAINER& TeleportActors,
	T_TRANSFORMCONTAINER& TeleportTransforms)
{
	bool bTeleport = Teleport == ETeleportType::TeleportPhysics;
	const UPhysicsAsset* PhysicsAsset = SkelComp->GetPhysicsAsset();
	const FTransform& CurrentLocalToWorld = SkelComp->GetComponentTransform();
	const int32 NumBodies = SkelComp->Bodies.Num();
	for (int32 i = 0; i < NumBodies; i++)
	{
		FBodyInstance* BodyInst = SkelComp->Bodies[i];
		FPhysicsActorHandle& ActorHandle = BodyInst->ActorHandle;
		if (bTeleport || !BodyInst->IsInstanceSimulatingPhysics())
		{
			const int32 BoneIndex = BodyInst->InstanceBoneIndex;
			if (BoneIndex != INDEX_NONE)
			{
				const FTransform BoneTransform = InComponentSpaceTransforms[BoneIndex] * CurrentLocalToWorld;
				if (!bTeleport)
				{
					KinematicUpdateActors.Add(ActorHandle);
					KinematicUpdateTransforms.Add(BoneTransform);
				}
				else
				{
					TeleportActors.Add(ActorHandle);
					TeleportTransforms.Add(BoneTransform);
				}
				if (!PhysicsAsset->SkeletalBodySetups[i]->bSkipScaleFromAnimation)
				{
					const FVector& MeshScale3D = CurrentLocalToWorld.GetScale3D();
					if (MeshScale3D.IsUniform())
					{
						BodyInst->UpdateBodyScale(BoneTransform.GetScale3D());
					}
					else
					{
						BodyInst->UpdateBodyScale(MeshScale3D);
					}
				}
			}
		}
	}
}

// Move all actors that need teleporting
void ProcessTeleportActors(FPhysScene_Chaos& Scene, const TArrayView<FPhysicsActorHandle>& ActorHandles, const TArrayView<FTransform>& Transforms)
{
	int32 NumActors = ActorHandles.Num();
	if (NumActors > 0)
	{
		for (int32 ActorIndex = 0; ActorIndex < NumActors; ++ActorIndex)
		{
			const FPhysicsActorHandle& ActorHandle = ActorHandles[ActorIndex];
			const FTransform& ActorTransform = Transforms[ActorIndex];
			ActorHandle->SetX(ActorTransform.GetLocation(), false);	// only set dirty once in SetR
			ActorHandle->SetR(ActorTransform.GetRotation());
			ActorHandle->UpdateShapeBounds();
		}

		Scene.UpdateActorsInAccelerationStructure(ActorHandles);
	}
}

// Set all actor kinematic targets
void ProcessKinematicTargetActors(FPhysScene_Chaos& Scene, const TArrayView<FPhysicsActorHandle>& ActorHandles, const TArrayView<FTransform>& Transforms)
{
	// TODO - kinematic targets
	ProcessTeleportActors(Scene, ActorHandles, Transforms);
}

// Collect the actors and transforms of all the bodies we have to move, and process them in bulk
// to avoid locks in the Spatial Acceleration and the Solver's Dirty Proxy systems.
void FPhysScene_ChaosInterface::UpdateKinematicsOnDeferredSkelMeshes()
{
	SCOPE_CYCLE_COUNTER(STAT_UpdateKinematicsOnDeferredSkelMeshesChaos);

	// Holds start index in actor pool for each skeletal mesh.
	TArray<int32, TInlineAllocator<64>> SkeletalMeshStartIndexArray;

	TArray<FPhysicsActorHandle, TInlineAllocator<64>>TeleportActorsPool;
	TArray<IPhysicsProxyBase*, TInlineAllocator<64>> ProxiesToDirty;
	
	// Count max number of bodies to determine actor pool size.
	{
		SkeletalMeshStartIndexArray.Reserve(DeferredKinematicUpdateSkelMeshes.Num());

		int32 TotalBodies = 0;
		for (const TPair<USkeletalMeshComponent*, FDeferredKinematicUpdateInfo>& DeferredKinematicUpdate : DeferredKinematicUpdateSkelMeshes)
		{
			SkeletalMeshStartIndexArray.Add(TotalBodies);

			USkeletalMeshComponent* SkelComp = DeferredKinematicUpdate.Key;
			if (!SkelComp->bEnablePerPolyCollision)
			{
				TotalBodies += SkelComp->Bodies.Num();
			}
		}

		// Actors pool is spare, initialize to nullptr.
		TeleportActorsPool.AddZeroed(TotalBodies);
		ProxiesToDirty.Reserve(TotalBodies);
	}

	// Gather proxies that need to be dirtied before paralell loop, and update any per poly collision skeletal meshes.
	{
		for (const TPair<USkeletalMeshComponent*, FDeferredKinematicUpdateInfo>& DeferredKinematicUpdate : DeferredKinematicUpdateSkelMeshes)
		{
			USkeletalMeshComponent* SkelComp = DeferredKinematicUpdate.Key;
			const FDeferredKinematicUpdateInfo& Info = DeferredKinematicUpdate.Value;

			if (!SkelComp->bEnablePerPolyCollision)
			{
				const int32 NumBodies = SkelComp->Bodies.Num();
				for (int32 i = 0; i < NumBodies; i++)
				{
					FBodyInstance* BodyInst = SkelComp->Bodies[i];
					FPhysicsActorHandle& ActorHandle = BodyInst->ActorHandle;
					if (!BodyInst->IsInstanceSimulatingPhysics())
					{
						const int32 BoneIndex = BodyInst->InstanceBoneIndex;
						if (BoneIndex != INDEX_NONE)
						{
							IPhysicsProxyBase* Proxy = ActorHandle->GetProxy();
							if (Proxy && Proxy->GetDirtyIdx() == INDEX_NONE)
							{
								ProxiesToDirty.Add(Proxy);
							}
						}
					}
				}
			}
			else
			{
				// TODO: acceleration for per-poly collision
				SkelComp->UpdateKinematicBonesToAnim(SkelComp->GetComponentSpaceTransforms(), Info.TeleportType, Info.bNeedsSkinning, EAllowKinematicDeferral::DisallowDeferral);
			}
		}
	}

	// Mark all body's proxies as dirty, as this is not threadsafe and cannot be done in parallel loop.
	if (ProxiesToDirty.Num() > 0)
	{
		// Assumes all particles have the same solver, safe for now, maybe not in the future.
		IPhysicsProxyBase* Proxy = ProxiesToDirty[0];
		Chaos::FPhysicsSolverBase* Solver = Proxy->GetSolver();
		Solver->AddDirtyProxiesUnsafe(ProxiesToDirty);
	}

	{
		Chaos::PhysicsParallelFor(DeferredKinematicUpdateSkelMeshes.Num(), [&](int32 Index)
		{
			const TPair<USkeletalMeshComponent*, FDeferredKinematicUpdateInfo>& DeferredKinematicUpdate = DeferredKinematicUpdateSkelMeshes[Index];
			USkeletalMeshComponent* SkelComp = DeferredKinematicUpdate.Key;
			const FDeferredKinematicUpdateInfo& Info = DeferredKinematicUpdate.Value;

			SkelComp->DeferredKinematicUpdateIndex = INDEX_NONE;

			if (!SkelComp->bEnablePerPolyCollision)
			{
				const UPhysicsAsset* PhysicsAsset = SkelComp->GetPhysicsAsset();
				const FTransform& CurrentLocalToWorld = SkelComp->GetComponentTransform();
				const int32 NumBodies = SkelComp->Bodies.Num();
				const TArray<FTransform>& ComponentSpaceTransforms = SkelComp->GetComponentSpaceTransforms();
				
				const int32 ActorPoolStartIndex = SkeletalMeshStartIndexArray[Index];
				for (int32 i = 0; i < NumBodies; i++)
				{
					FBodyInstance* BodyInst = SkelComp->Bodies[i];
					FPhysicsActorHandle& ActorHandle = BodyInst->ActorHandle;
					if (!BodyInst->IsInstanceSimulatingPhysics())
					{
						const int32 BoneIndex = BodyInst->InstanceBoneIndex;
						if (BoneIndex != INDEX_NONE)
						{
							const FTransform BoneTransform = ComponentSpaceTransforms[BoneIndex] * CurrentLocalToWorld;

							TeleportActorsPool[ActorPoolStartIndex + i] = ActorHandle;

							// TODO: Kinematic targets. Check Teleport type on FDeferredKinematicUpdateInfo and don't always teleport.
							ActorHandle->SetX(BoneTransform.GetLocation(), false);	// only set dirty once in SetR
							ActorHandle->SetR(BoneTransform.GetRotation());
							ActorHandle->UpdateShapeBounds(BoneTransform);

							if (!PhysicsAsset->SkeletalBodySetups[i]->bSkipScaleFromAnimation)
							{
								const FVector& MeshScale3D = CurrentLocalToWorld.GetScale3D();
								if (MeshScale3D.IsUniform())
								{
									BodyInst->UpdateBodyScale(BoneTransform.GetScale3D());
								}
								else
								{
									BodyInst->UpdateBodyScale(MeshScale3D);
								}
							}
						}
					}
				}
			}
		});
	}

	Scene.UpdateActorsInAccelerationStructure(TeleportActorsPool);

	DeferredKinematicUpdateSkelMeshes.Reset();
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

void FPhysScene_ChaosInterface::SetUpForFrame(const FVector* NewGrav, float InDeltaSeconds /*= 0.0f*/, float InMaxPhysicsDeltaTime /*= 0.0f*/, float InMaxSubstepDeltaTime /*= 0.0f*/, int32 InMaxSubsteps)
{
	SetGravity(*NewGrav);
	MDeltaTime = InMaxPhysicsDeltaTime > 0.f ? FMath::Min(InDeltaSeconds, InMaxPhysicsDeltaTime) : InDeltaSeconds;

	if (Chaos::FPhysicsSolver* Solver = GetSolver())
	{
		Solver->SetMaxDeltaTime(InMaxSubstepDeltaTime);
		Solver->SetMaxSubSteps(InMaxSubsteps);
	}
}

void FPhysScene_ChaosInterface::StartFrame()
{
	using namespace Chaos;

	SCOPE_CYCLE_COUNTER(STAT_Scene_StartFrame);

	if (CVar_ChaosSimulationEnable.GetValueOnGameThread() == 0)
	{
		return;
	}

	FChaosSolversModule* SolverModule = FChaosSolversModule::GetModule();
	checkSlow(SolverModule);

	float Dt = MDeltaTime;

#if WITH_EDITOR
	if (IsOwningWorldEditor())
	{
		// Ensure editor solver is enabled
		if (GetSolver()->Enabled() == false)
		{
			GetSolver()->SetEnabled(true);
		}

		Dt = 0.0f;
	}
#endif

	// Update any skeletal meshes that need their bone transforms sent to physics sim
	UpdateKinematicsOnDeferredSkelMeshes();

	if (FPhysicsReplication* PhysicsReplication = Scene.GetPhysicsReplication())
	{
		PhysicsReplication->Tick(Dt);
	}

	if (Chaos::IDispatcher* Dispatcher = SolverModule->GetDispatcher())
	{
		switch (Dispatcher->GetMode())
		{
		case EChaosThreadingMode::SingleThread:
		{
			OnPhysScenePreTick.Broadcast(this, Dt);
			OnPhysSceneStep.Broadcast(this, Dt);

			if (FPhysicsSolver* Solver = GetSolver())
			{
				Solver->PushPhysicsState(Dispatcher);
			}
			// Here we can directly tick the scene. Single threaded mode doesn't buffer any commands
			// that would require pumping here - everything is done on demand.
			Scene.Tick(Dt);

			// Copy out solver data
			if (Chaos::FPhysicsSolver* Solver = GetSolver())
			{
				Solver->GetActiveParticlesBuffer()->CaptureSolverData(Solver);
				Solver->BufferPhysicsResults();
				Solver->FlipBuffers();
			}
		}
		break;
		case EChaosThreadingMode::TaskGraph:
		{
			check(!CompletionEvent.GetReference())

			OnPhysScenePreTick.Broadcast(this, Dt);
			OnPhysSceneStep.Broadcast(this, Dt);

			TArray<Chaos::FPhysicsSolver*> SolverList;
			SolverModule->GetSolversMutable(GetOwningWorld(), SolverList);
			
			if (FPhysicsSolver* Solver = GetSolver())
			{
				Solver->PushPhysicsState(Dispatcher);

				// Make sure our solver is in the list
				SolverList.AddUnique(Solver);
			}

			FGraphEventRef SimulationCompleteEvent = FGraphEvent::CreateGraphEvent();

			// Need to fire off a parallel task to handle running physics commands and
			// ticking the scene while the engine continues on until TG_EndPhysics
			// (this should happen in TG_StartPhysics)
			PhysicsTickTask = TGraphTask<FPhysicsTickTask>::CreateTask(nullptr, ENamedThreads::GameThread).ConstructAndDispatchWhenReady(SimulationCompleteEvent, SolverList, Dt);

			// Setup post simulate tasks
			if (PhysicsTickTask.GetReference())
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
			if (FPhysicsSolver* Solver = GetSolver())
			{
				Solver->PushPhysicsState(Dispatcher);
			}
			break;
			break;
		}
	}
}

// Find the number of dirty elements in all substructures that has dirty elements that we know of
// This is non recursive for now
// Todo: consider making DirtyElementsCount a method on ISpatialAcceleration instead
int32 FPhysScene_ChaosInterface::DirtyElementCount(Chaos::ISpatialAccelerationCollection<Chaos::TAccelerationStructureHandle<Chaos::FReal, 3>, Chaos::FReal, 3>& Collection)
{
	using namespace Chaos;
	int32 DirtyElements = 0;
	TArray<FSpatialAccelerationIdx> SpatialIndices = Collection.GetAllSpatialIndices();
	for (const FSpatialAccelerationIdx SpatialIndex : SpatialIndices)
	{
		auto SubStructure = Collection.GetSubstructure(SpatialIndex);
		if (const auto AABBTree = SubStructure->template As<TAABBTree<TAccelerationStructureHandle<FReal, 3>, TAABBTreeLeafArray<TAccelerationStructureHandle<FReal, 3>, FReal>, FReal>>())
		{
			DirtyElements += AABBTree->NumDirtyElements();
		}
		else if (const auto AABBTreeBV = SubStructure->template As<TAABBTree<TAccelerationStructureHandle<FReal, 3>, TBoundingVolume<TAccelerationStructureHandle<FReal, 3>, FReal, 3>, FReal>>())
		{
			DirtyElements += AABBTreeBV->NumDirtyElements();
		}
	}
	return DirtyElements;
}


void FPhysScene_ChaosInterface::EndFrame(ULineBatchComponent* InLineBatcher)
{
	using namespace Chaos;
	using SpatialAccelerationCollection = Chaos::ISpatialAccelerationCollection<Chaos::TAccelerationStructureHandle<FReal, 3>, FReal, 3>;

	SCOPE_CYCLE_COUNTER(STAT_Scene_EndFrame);

	if (CVar_ChaosSimulationEnable.GetValueOnGameThread() == 0)
	{
		return;
	}

	FChaosSolversModule* SolverModule = FChaosSolversModule::GetModule();
	checkSlow(SolverModule);

	Chaos::IDispatcher* Dispatcher = SolverModule->GetDispatcher();

	int32 DirtyElements = DirtyElementCount(Scene.GetSpacialAcceleration()->AsChecked<SpatialAccelerationCollection>());
	CSV_CUSTOM_STAT(ChaosPhysics, AABBTreeDirtyElementCount, DirtyElements, ECsvCustomStatOp::Set);

	switch(Dispatcher->GetMode())
	{
	case EChaosThreadingMode::SingleThread:
	{
		SyncBodies(Scene.GetSolver());
		Scene.GetSolver()->SyncEvents_GameThread();

		OnPhysScenePostTick.Broadcast(this);
	}
	break;
	case EChaosThreadingMode::TaskGraph:
	{
		check(CompletionEvent->IsComplete());
		//check(PhysicsTickTask->IsComplete());
		CompletionEvent = nullptr;
		PhysicsTickTask = nullptr;

		//flush queue so we can merge the two threads
		Dispatcher->Execute();

		// Make a list of solvers to process. This is a list of all solvers registered to our world
		// And our internal base scene solver.
		TArray<Chaos::FPhysicsSolver*> SolverList;
		SolverModule->GetSolversMutable(GetOwningWorld(), SolverList);

		if(FPhysicsSolver* Solver = GetSolver())
		{
			// Make sure our solver is in the list
			SolverList.AddUnique(Solver);
		}

		// flush solver queues
		for (FPhysicsSolver* Solver : SolverList)
		{
			TQueue<TFunction<void(Chaos::FPhysicsSolver*)>, EQueueMode::Mpsc>& Queue = Solver->GetCommandQueue();
			TFunction<void(Chaos::FPhysicsSolver*)> Command;
			while (Queue.Dequeue(Command))
			{
				Command(Solver);
			}
		}

		// Flip the buffers over to the game thread and sync
		{
			SCOPE_CYCLE_COUNTER(STAT_FlipResults);

			//update external SQ structure
			//for now just copy the whole thing, stomping any changes that came from GT
			Scene.CopySolverAccelerationStructure();

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

			for (FPhysicsSolver* Solver : ActiveSolvers)
			{
				SyncBodies(Solver);
				Solver->SyncEvents_GameThread();

				{
					SCOPE_CYCLE_COUNTER(STAT_SqUpdateMaterials);
					Solver->SyncQueryMaterials();
				}
			}
		}

		OnPhysScenePostTick.Broadcast(this);
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
	if (CompletionEvent && !CompletionEvent->IsComplete())
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_FPhysScene_WaitPhysScenes);
		FTaskGraphInterface::Get().WaitUntilTaskCompletes(CompletionEvent, ENamedThreads::GameThread);
	}
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

void FPhysScene_ChaosInterface::SyncBodies(Chaos::FPhysicsSolver* Solver)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("SyncBodies"), STAT_SyncBodies, STATGROUP_Physics);
	TArray<FPhysScenePendingComponentTransform_Chaos> PendingTransforms;

	TSet<FGeometryCollectionPhysicsProxy*> GCProxies;

	{
		Chaos::FPBDRigidActiveParticlesBufferAccessor Accessor(Solver->GetActiveParticlesBuffer());


		const Chaos::FPBDRigidActiveParticlesBufferOut* ActiveParticleBuffer = Accessor.GetSolverOutData();
		for (Chaos::TGeometryParticle<float, 3>* ActiveParticle : ActiveParticleBuffer->ActiveGameThreadParticles)
		{
		if (IPhysicsProxyBase* ProxyBase = ActiveParticle->GetProxy())
			{
				if (ProxyBase->GetType() == EPhysicsProxyType::SingleRigidParticleType)
				{
					FSingleParticlePhysicsProxy< Chaos::TPBDRigidParticle<float, 3> > * Proxy = static_cast<FSingleParticlePhysicsProxy< Chaos::TPBDRigidParticle<float, 3> >*>(ProxyBase);
					Proxy->PullFromPhysicsState();

					if (FBodyInstance* BodyInstance = FPhysicsUserData::Get<FBodyInstance>(ActiveParticle->UserData()))
					{
						if (BodyInstance->OwnerComponent.IsValid())
						{
							UPrimitiveComponent* OwnerComponent = BodyInstance->OwnerComponent.Get();
							if (OwnerComponent != nullptr)
							{
								bool bPendingMove = false;
								if (BodyInstance->InstanceBodyIndex == INDEX_NONE)
								{
									Chaos::TRigidTransform<float, 3> NewTransform(ActiveParticle->X(), ActiveParticle->R());

									if (!NewTransform.EqualsNoScale(OwnerComponent->GetComponentTransform()))
									{
										bPendingMove = true;
										const FVector MoveBy = NewTransform.GetLocation() - OwnerComponent->GetComponentTransform().GetLocation();
										const FQuat NewRotation = NewTransform.GetRotation();
										PendingTransforms.Add(FPhysScenePendingComponentTransform_Chaos(OwnerComponent, MoveBy, NewRotation, Proxy->HasAwakeEvent()));
									}
								}

								if (Proxy->HasAwakeEvent() && !bPendingMove)
								{
									PendingTransforms.Add(FPhysScenePendingComponentTransform_Chaos(OwnerComponent));
								}
								Proxy->ClearEvents();
							}
						}
					}
				}
				else if(ProxyBase->GetType() == EPhysicsProxyType::GeometryCollectionType)
				{
					FGeometryCollectionPhysicsProxy* Proxy = static_cast<FGeometryCollectionPhysicsProxy*>(ProxyBase);
					GCProxies.Add(Proxy);
				}
			}
		}
		for (IPhysicsProxyBase* ProxyBase : ActiveParticleBuffer->PhysicsParticleProxies) 
		{
			if(ProxyBase->GetType() == EPhysicsProxyType::GeometryCollectionType)
			{
				FGeometryCollectionPhysicsProxy* Proxy = static_cast<FGeometryCollectionPhysicsProxy*>(ProxyBase);
				GCProxies.Add(Proxy);
			}
			else
			{
				ensure(false); // Unhandled physics only particle proxy!
			}
		}
	}
	
	for (auto* GCProxy : GCProxies)
	{
		GCProxy->PullFromPhysicsState();
	}
	for (const FPhysScenePendingComponentTransform_Chaos& ComponentTransform : PendingTransforms)
	{
		if (ComponentTransform.OwningComp != nullptr)
		{
			AActor* Owner = ComponentTransform.OwningComp->GetOwner();

			if (ComponentTransform.bHasValidTransform)
			{
				ComponentTransform.OwningComp->MoveComponent(ComponentTransform.NewTranslation, ComponentTransform.NewRotation, false, NULL, MOVECOMP_SkipPhysicsMove);
			}

			if (Owner != NULL && !Owner->IsPendingKill())
			{
				Owner->CheckStillInWorld();
			}
		}

		if (ComponentTransform.OwningComp != nullptr)
		{
			if (ComponentTransform.bHasWakeEvent)
			{
				ComponentTransform.OwningComp->DispatchWakeEvents(ESleepEvent::SET_Wakeup, NAME_None);
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
		LLM_SCOPE(ELLMTag::Chaos);
		SCOPE_CYCLE_COUNTER(STAT_BufferPhysicsResults);

		FChaosSolversModule* Module = FChaosSolversModule::GetModule();

		check(Module);

		TArray<FPhysicsSolver*> SolverList = Module->GetSolversMutable(GetOwningWorld());
		FPhysicsSolver* SceneSolver = GetSolver();

		TArray<FPhysicsSolver*> ActiveSolvers;

		if(SolverList.Num() > 0)
		{
			ActiveSolvers.Reserve(SolverList.Num());

			// #BG calculate active solver list once as we dispatch our first task
			for(FPhysicsSolver* Solver : SolverList)
			{
				if(Solver->HasActiveParticles())
				{
					ActiveSolvers.Add(Solver);
				}
			}
		}

		if(SceneSolver && SceneSolver->HasActiveParticles())
		{
			ActiveSolvers.AddUnique(SceneSolver);
		}

		const int32 NumActiveSolvers = ActiveSolvers.Num();

		PhysicsParallelFor(NumActiveSolvers, [&](int32 Index)
			{
				FPhysicsSolver* Solver = ActiveSolvers[Index];

				Solver->GetActiveParticlesBuffer()->CaptureSolverData(Solver);
				Solver->BufferPhysicsResults();
				Solver->FlipBuffers();
			});
	}
}

void FPhysScene_ChaosInterface::AddToComponentMaps(UPrimitiveComponent* Component, IPhysicsProxyBase* InObject)
{
	Scene.AddToComponentMaps(Component, InObject);
}

void FPhysScene_ChaosInterface::RemoveFromComponentMaps(IPhysicsProxyBase* InObject)
{
	Scene.RemoveFromComponentMaps(InObject);
}

TSharedPtr<IPhysicsReplicationFactory> FPhysScene_ChaosInterface::PhysicsReplicationFactory;

#endif // WITH_CHAOS

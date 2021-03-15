// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/ChaosScene.h"

#include "Async/AsyncWork.h"
#include "Async/ParallelFor.h"

#include "Misc/CoreDelegates.h"
#include "Misc/ScopeLock.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "ChaosSolversModule.h"
#include "ChaosLog.h"
#include "ChaosStats.h"

#include "Field/FieldSystem.h"

#include "PhysicsProxy/PerSolverFieldSystem.h"
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
#include "Chaos/Public/EventsData.h"
#include "Chaos/Public/EventManager.h"
#include "Chaos/Public/RewindData.h"
#include "PhysicsSettingsCore.h"
#include "Chaos/PhysicsSolverBaseImpl.h"

#include "ProfilingDebugging/CsvProfiler.h"

DECLARE_CYCLE_STAT(TEXT("Update Kinematics On Deferred SkelMeshes"),STAT_UpdateKinematicsOnDeferredSkelMeshesChaos,STATGROUP_Physics);
CSV_DEFINE_CATEGORY(ChaosPhysics,true);

// Stat Counters
DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("NumDirtyAABBTreeElements"), STAT_ChaosCounter_NumDirtyAABBTreeElements, STATGROUP_ChaosCounters);

TAutoConsoleVariable<int32> CVar_ChaosSimulationEnable(TEXT("P.Chaos.Simulation.Enable"),1,TEXT("Enable / disable chaos simulation. If disabled, physics will not tick."));
TAutoConsoleVariable<int32> CVar_ApplyProjectSettings(TEXT("p.Chaos.Simulation.ApplySolverProjectSettings"), 1, TEXT("Whether to apply the solver project settings on spawning a solver"));

FChaosScene::FChaosScene(
	UObject* OwnerPtr
#if CHAOS_CHECKED
	, const FName& DebugName
#endif
)
	: SolverAccelerationStructure(nullptr)
	, ChaosModule(nullptr)
	, SceneSolver(nullptr)
	, Owner(OwnerPtr)
{
	LLM_SCOPE(ELLMTag::Chaos);

	ChaosModule = FChaosSolversModule::GetModule();
	check(ChaosModule);

	const bool bForceSingleThread = !(FApp::ShouldUseThreadingForPerformance() || FForkProcessHelper::SupportsMultithreadingPostFork());

	Chaos::EThreadingMode ThreadingMode = bForceSingleThread ? Chaos::EThreadingMode::SingleThread : Chaos::EThreadingMode::TaskGraph;

	SceneSolver = ChaosModule->CreateSolver(OwnerPtr,ThreadingMode
#if CHAOS_CHECKED
		,DebugName
#endif
		);
	check(SceneSolver);

	SceneSolver->PhysSceneHack = this;
	SimCallback = SceneSolver->CreateAndRegisterSimCallbackObject_External<FChaosSceneSimCallback>();

	if(CVar_ApplyProjectSettings.GetValueOnAnyThread() != 0)
	{
		UPhysicsSettingsCore* Settings = UPhysicsSettingsCore::Get();
		SceneSolver->EnqueueCommandImmediate([InSolver = SceneSolver, SolverConfigCopy = Settings->SolverOptions]()
		{
			InSolver->ApplyConfig(SolverConfigCopy);
		});
	}

	Flush();	//make sure acceleration structure exists right away
}

FChaosScene::~FChaosScene()
{
	if(ensure(SceneSolver))
	{
		Chaos::FEventManager* EventManager = SceneSolver->GetEventManager();
		EventManager->UnregisterHandler(Chaos::EEventType::Collision,this);
		SceneSolver->UnregisterAndFreeSimCallbackObject_External(SimCallback);
	}

	if(ensure(ChaosModule))
	{
		// Destroy our solver
		ChaosModule->DestroySolver(GetSolver());
	}

	SimCallback = nullptr;
	ChaosModule = nullptr;
	SceneSolver = nullptr;
}

#if WITH_ENGINE
void FChaosScene::AddReferencedObjects(FReferenceCollector& Collector)
{
#if WITH_EDITOR
	for(UObject* Obj : PieModifiedObjects)
	{
		Collector.AddReferencedObject(Obj);
	}
#endif
}
#endif

#if WITH_EDITOR
void FChaosScene::AddPieModifiedObject(UObject* InObj)
{
	if(GIsPlayInEditorWorld)
	{
		PieModifiedObjects.AddUnique(InObj);
	}
}
#endif


const Chaos::ISpatialAcceleration<Chaos::FAccelerationStructureHandle, Chaos::FReal, 3>* FChaosScene::GetSpacialAcceleration() const
{
	return SolverAccelerationStructure;
}

Chaos::ISpatialAcceleration<Chaos::FAccelerationStructureHandle, Chaos::FReal, 3>* FChaosScene::GetSpacialAcceleration()
{
	return SolverAccelerationStructure;
}

void FChaosScene::CopySolverAccelerationStructure()
{
	using namespace Chaos;
	if(SceneSolver)
	{
		FPhysicsSceneGuardScopedWrite ScopedWrite(SceneSolver->GetExternalDataLock_External());
		SceneSolver->UpdateExternalAccelerationStructure_External(SolverAccelerationStructure);
	}
}

void FChaosScene::Flush()
{
	check(IsInGameThread());

	Chaos::FPBDRigidsSolver* Solver = GetSolver();

	if(Solver)
	{
		//Make sure any dirty proxy data is pushed
		Solver->AdvanceAndDispatch_External(0);	//force commands through
		Solver->WaitOnPendingTasks_External();

		// Populate the spacial acceleration
		Chaos::FPBDRigidsSolver::FPBDRigidsEvolution* Evolution = Solver->GetEvolution();

		if(Evolution)
		{
			Evolution->FlushSpatialAcceleration();
		}
	}

	CopySolverAccelerationStructure();
}

void FChaosScene::RemoveActorFromAccelerationStructure(Chaos::TGeometryParticle<Chaos::FReal, 3>* Particle)
{
#if WITH_CHAOS
	using namespace Chaos;
	if(GetSpacialAcceleration())
	{
		FPhysicsSceneGuardScopedWrite ScopedWrite(SceneSolver->GetExternalDataLock_External());
		Chaos::FAccelerationStructureHandle AccelerationHandle(Particle);
		GetSpacialAcceleration()->RemoveElementFrom(AccelerationHandle, Particle->SpatialIdx());
	}
#endif
}

void FChaosScene::RemoveActorFromAccelerationStructure(FPhysicsActorHandle& Actor)
{
#if WITH_CHAOS
	using namespace Chaos;
	Chaos::FRigidBodyHandle_External& Body_External = Actor->GetGameThreadAPI();
	if (GetSpacialAcceleration() && Body_External.UniqueIdx().IsValid())
	{
		FPhysicsSceneGuardScopedWrite ScopedWrite(SceneSolver->GetExternalDataLock_External());
		Chaos::FAccelerationStructureHandle AccelerationHandle(Actor->GetParticle_LowLevel());
		GetSpacialAcceleration()->RemoveElementFrom(AccelerationHandle, Body_External.SpatialIdx());
	}
#endif
}

void FChaosScene::UpdateActorInAccelerationStructure(const FPhysicsActorHandle& Actor)
{
#if WITH_CHAOS
	using namespace Chaos;

	if(GetSpacialAcceleration())
	{
		FPhysicsSceneGuardScopedWrite ScopedWrite(SceneSolver->GetExternalDataLock_External());

		auto SpatialAcceleration = GetSpacialAcceleration();
		const Chaos::FRigidBodyHandle_External& Body_External = Actor->GetGameThreadAPI();

		if(SpatialAcceleration)
		{

			FAABB3 WorldBounds;
			const bool bHasBounds = Body_External.Geometry()->HasBoundingBox();
			if(bHasBounds)
			{
				WorldBounds = Body_External.Geometry()->BoundingBox().TransformedAABB(FRigidTransform3(Body_External.X(), Body_External.R()));
			}


			Chaos::FAccelerationStructureHandle AccelerationHandle(Actor->GetParticle_LowLevel());
			SpatialAcceleration->UpdateElementIn(AccelerationHandle,WorldBounds,bHasBounds, Body_External.SpatialIdx());
		}

		GetSolver()->UpdateParticleInAccelerationStructure_External(Actor->GetParticle_LowLevel(),/*bDelete=*/false);
	}
#endif
}

void FChaosScene::UpdateActorsInAccelerationStructure(const TArrayView<FPhysicsActorHandle>& Actors)
{
#if WITH_CHAOS
	using namespace Chaos;

	if(GetSpacialAcceleration())
	{
		FPhysicsSceneGuardScopedWrite ScopedWrite(SceneSolver->GetExternalDataLock_External());

		auto SpatialAcceleration = GetSpacialAcceleration();

		if(SpatialAcceleration)
		{
			int32 NumActors = Actors.Num();
			for(int32 ActorIndex = 0; ActorIndex < NumActors; ++ActorIndex)
			{
				const FPhysicsActorHandle& Actor = Actors[ActorIndex];
				if(Actor)
				{
					const Chaos::FRigidBodyHandle_External& Body_External = Actor->GetGameThreadAPI();
					// @todo(chaos): dedupe code in UpdateActorInAccelerationStructure
					FAABB3 WorldBounds;
					const bool bHasBounds = Body_External.Geometry()->HasBoundingBox();
					if(bHasBounds)
					{
						WorldBounds = Body_External.Geometry()->BoundingBox().TransformedAABB(FRigidTransform3(Body_External.X(), Body_External.R()));
					}

					Chaos::FAccelerationStructureHandle AccelerationHandle(Actor->GetParticle_LowLevel());
					SpatialAcceleration->UpdateElementIn(AccelerationHandle,WorldBounds,bHasBounds, Body_External.SpatialIdx());
				}
			}
		}

		for(int32 ActorIndex = 0; ActorIndex < Actors.Num(); ++ActorIndex)
		{
			const FPhysicsActorHandle& Actor = Actors[ActorIndex];
			if(Actor)
			{
				GetSolver()->UpdateParticleInAccelerationStructure_External(Actor->GetParticle_LowLevel(),/*bDelete=*/false);
			}
		}
	}
#endif
}

void FChaosScene::AddActorsToScene_AssumesLocked(TArray<FPhysicsActorHandle>& InHandles,const bool bImmediate)
{
#if WITH_CHAOS
	Chaos::FPhysicsSolver* Solver = GetSolver();
	Chaos::ISpatialAcceleration<Chaos::FAccelerationStructureHandle,Chaos::FReal,3>* SpatialAcceleration = GetSpacialAcceleration();
	for(FPhysicsActorHandle& Handle : InHandles)
	{
		FChaosEngineInterface::AddActorToSolver(Handle,Solver);

		// Optionally add this to the game-thread acceleration structure immediately
		if(bImmediate && SpatialAcceleration)
		{
			const Chaos::FRigidBodyHandle_External& Body_External = Handle->GetGameThreadAPI();
			// Get the bounding box for the particle if it has one
			bool bHasBounds = Body_External.Geometry()->HasBoundingBox();
			Chaos::FAABB3 WorldBounds;
			if(bHasBounds)
			{
				const Chaos::FAABB3 LocalBounds = Body_External.Geometry()->BoundingBox();
				WorldBounds = LocalBounds.TransformedAABB(Chaos::FRigidTransform3(Body_External.X(), Body_External.R()));
			}

			// Insert the particle
			Chaos::FAccelerationStructureHandle AccelerationHandle(Handle->GetParticle_LowLevel());
			SpatialAcceleration->UpdateElementIn(AccelerationHandle,WorldBounds,bHasBounds, Body_External.SpatialIdx());
		}
	}
#endif
}

void FChaosSceneSimCallback::OnPreSimulate_Internal()
{
	if(const FChaosSceneCallbackInput* Input = GetConsumerInput_Internal())
	{
		static_cast<Chaos::FPBDRigidsSolver*>(GetSolver())->GetEvolution()->GetGravityForces().SetAcceleration(Input->Gravity);
	}
}

void FChaosScene::SetGravity(const Chaos::FVec3& Acceleration)
{
	SimCallback->GetProducerInputData_External()->Gravity = Acceleration;
}

void FChaosScene::SetUpForFrame(const FVector* NewGrav,float InDeltaSeconds /*= 0.0f*/,float InMaxPhysicsDeltaTime /*= 0.0f*/,float InMaxSubstepDeltaTime /*= 0.0f*/,int32 InMaxSubsteps,bool bSubstepping)
{
#if WITH_CHAOS
	using namespace Chaos;
	SetGravity(*NewGrav);
	MDeltaTime = InMaxPhysicsDeltaTime > 0.f ? FMath::Min(InDeltaSeconds,InMaxPhysicsDeltaTime) : InDeltaSeconds;

	if(FPhysicsSolver* Solver = GetSolver())
	{
		if(bSubstepping)
		{
			Solver->SetMaxDeltaTime(InMaxSubstepDeltaTime);
			Solver->SetMaxSubSteps(InMaxSubsteps);
		} else
		{
			Solver->SetMaxDeltaTime(InMaxPhysicsDeltaTime);
			Solver->SetMaxSubSteps(1);
		}
	}
#endif
}

void FChaosScene::StartFrame()
{
#if WITH_CHAOS
	using namespace Chaos;

	SCOPE_CYCLE_COUNTER(STAT_Scene_StartFrame);

	if(CVar_ChaosSimulationEnable.GetValueOnGameThread() == 0)
	{
		return;
	}

	const float UseDeltaTime = OnStartFrame(MDeltaTime);;

	TArray<FPhysicsSolverBase*> SolverList;
	ChaosModule->GetSolversMutable(Owner,SolverList);

	if(FPhysicsSolver* Solver = GetSolver())
	{
		// Make sure our solver is in the list
		SolverList.AddUnique(Solver);
	}


	for(FPhysicsSolverBase* Solver : SolverList)
	{
		CompletionEvents.Add(Solver->AdvanceAndDispatch_External(UseDeltaTime));
	}

#endif
}

void FChaosScene::OnSyncBodies(Chaos::FPhysicsSolverBase* Solver)
{
	Solver->PullPhysicsStateForEachDirtyProxy_External([](auto){});
}

bool FChaosScene::AreAnyTasksPending() const
{
	const Chaos::FPBDRigidsSolver* Solver = GetSolver();
	if (Solver && Solver->AreAnyTasksPending())
	{
		return true;
	}
	
	return false;
}

void FChaosScene::BeginDestroy()
{
	Chaos::FPBDRigidsSolver* Solver = GetSolver();
	if (Solver)
	{
		Solver->BeginDestroy();
	}
}

bool FChaosScene::IsCompletionEventComplete() const
{
	for (FGraphEventRef Event : CompletionEvents)
	{
		if (Event && !Event->IsComplete())
		{
			return false;
		}
	}

	return true;
}

template <typename TSolver>
void FChaosScene::SyncBodies(TSolver* Solver)
{
#if WITH_CHAOS
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("SyncBodies"),STAT_SyncBodies,STATGROUP_Physics);
	OnSyncBodies(Solver);
#endif
}


// Find the number of dirty elements in all substructures that has dirty elements that we know of
// This is non recursive for now
// Todo: consider making DirtyElementsCount a method on ISpatialAcceleration instead
int32 DirtyElementCount(Chaos::ISpatialAccelerationCollection<Chaos::FAccelerationStructureHandle,Chaos::FReal,3>& Collection)
{
	using namespace Chaos;
	int32 DirtyElements = 0;
	TArray<FSpatialAccelerationIdx> SpatialIndices = Collection.GetAllSpatialIndices();
	for(const FSpatialAccelerationIdx SpatialIndex : SpatialIndices)
	{
		auto SubStructure = Collection.GetSubstructure(SpatialIndex);
		if(const auto AABBTree = SubStructure->template As<TAABBTree<FAccelerationStructureHandle,TAABBTreeLeafArray<FAccelerationStructureHandle>>>())
		{
			DirtyElements += AABBTree->NumDirtyElements();
		} else if(const auto AABBTreeBV = SubStructure->template As<TAABBTree<FAccelerationStructureHandle,TBoundingVolume<FAccelerationStructureHandle>>>())
		{
			DirtyElements += AABBTreeBV->NumDirtyElements();
		}
	}
	return DirtyElements;
}

void FChaosScene::EndFrame()
{
#if WITH_CHAOS
	using namespace Chaos;
	using SpatialAccelerationCollection = ISpatialAccelerationCollection<FAccelerationStructureHandle,FReal,3>;

	SCOPE_CYCLE_COUNTER(STAT_Scene_EndFrame);

	if(CVar_ChaosSimulationEnable.GetValueOnGameThread() == 0 || GetSolver() == nullptr)
	{
		return;
	}

	int32 DirtyElements = DirtyElementCount(GetSpacialAcceleration()->AsChecked<SpatialAccelerationCollection>());
	CSV_CUSTOM_STAT(ChaosPhysics,AABBTreeDirtyElementCount,DirtyElements,ECsvCustomStatOp::Set);
	SET_DWORD_STAT(STAT_ChaosCounter_NumDirtyAABBTreeElements, DirtyElements);

	check(IsCompletionEventComplete())
	//check(PhysicsTickTask->IsComplete());
	CompletionEvents.Reset();

	// Make a list of solvers to process. This is a list of all solvers registered to our world
	// And our internal base scene solver.
	TArray<FPhysicsSolverBase*> SolverList;
	ChaosModule->GetSolversMutable(Owner,SolverList);

	{
		// Make sure our solver is in the list
		SolverList.AddUnique(GetSolver());
	}

	// Flip the buffers over to the game thread and sync
	{
		SCOPE_CYCLE_COUNTER(STAT_FlipResults);

		//update external SQ structure
		//for now just copy the whole thing, stomping any changes that came from GT
		CopySolverAccelerationStructure();

		for(FPhysicsSolverBase* Solver : SolverList)
		{
			Solver->CastHelper([&SolverList,this](auto& Concrete)
			{
				SyncBodies(&Concrete);
				Concrete.SyncEvents_GameThread();

				{
					SCOPE_CYCLE_COUNTER(STAT_SqUpdateMaterials);
					Concrete.SyncQueryMaterials_External();
				}
			});
		}
	}

	OnPhysScenePostTick.Broadcast(this);
#endif
}

void FChaosScene::WaitPhysScenes()
{
	if(!IsCompletionEventComplete())
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_FPhysScene_WaitPhysScenes);
		FTaskGraphInterface::Get().WaitUntilTasksComplete(CompletionEvents,ENamedThreads::GameThread);
	}
}

FGraphEventArray FChaosScene::GetCompletionEvents()
{
	return CompletionEvents;
}

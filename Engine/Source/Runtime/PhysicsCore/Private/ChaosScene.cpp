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
#include "ChaosSolvers/Public/EventsData.h"
#include "ChaosSolvers/Public/EventManager.h"
#include "ChaosSolvers/Public/RewindData.h"

#include "ProfilingDebugging/CsvProfiler.h"

DECLARE_CYCLE_STAT(TEXT("Update Kinematics On Deferred SkelMeshes"),STAT_UpdateKinematicsOnDeferredSkelMeshesChaos,STATGROUP_Physics);
CSV_DEFINE_CATEGORY(ChaosPhysics,true);

TAutoConsoleVariable<int32> CVar_ChaosSimulationEnable(TEXT("P.Chaos.Simulation.Enable"),1,TEXT("Enable / disable chaos simulation. If disabled, physics will not tick."));

FChaosScene::FChaosScene(
	UObject* OwnerPtr
#if CHAOS_CHECKED
	, const FName& DebugName
#endif
)
	: ChaosModule(nullptr)
	, SceneSolver(nullptr)
	, Owner(OwnerPtr)
{
	LLM_SCOPE(ELLMTag::Chaos);

	ChaosModule = FChaosSolversModule::GetModule();
	check(ChaosModule);

	const bool bForceSingleThread = !(FApp::ShouldUseThreadingForPerformance() || FForkProcessHelper::SupportsMultithreadingPostFork());

	Chaos::EThreadingMode ThreadingMode = bForceSingleThread ? Chaos::EThreadingMode::SingleThread : Chaos::EThreadingMode::TaskGraph;

	SceneSolver = ChaosModule->CreateSolver<Chaos::FDefaultTraits>(OwnerPtr,ThreadingMode
#if CHAOS_CHECKED
		,DebugName
#endif
		);
	check(SceneSolver);

	SceneSolver->PhysSceneHack = this;

	Flush_AssumesLocked();	//make sure acceleration structure exists right away
}

FChaosScene::~FChaosScene()
{
	if(ensure(SceneSolver))
	{
		Chaos::FEventManager* EventManager = SceneSolver->GetEventManager();
		EventManager->UnregisterHandler(Chaos::EEventType::Collision,this);
	}

	if(ensure(ChaosModule))
	{
		// Destroy our solver
		ChaosModule->DestroySolver(GetSolver());
	}

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


const Chaos::ISpatialAcceleration<Chaos::TAccelerationStructureHandle<float,3>,float,3>* FChaosScene::GetSpacialAcceleration() const
{
	return SolverAccelerationStructure.Get();
}

Chaos::ISpatialAcceleration<Chaos::TAccelerationStructureHandle<float,3>,float,3>* FChaosScene::GetSpacialAcceleration()
{
	return SolverAccelerationStructure.Get();
}

void FChaosScene::CopySolverAccelerationStructure()
{
	if(SceneSolver)
	{
		ExternalDataLock.WriteLock();
		SceneSolver->UpdateExternalAccelerationStructure_External(SolverAccelerationStructure);
		ExternalDataLock.WriteUnlock();
	}
}

void FChaosScene::Flush_AssumesLocked()
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

void FChaosScene::RemoveActorFromAccelerationStructure(FPhysicsActorHandle& Actor)
{
#if WITH_CHAOS
	if(GetSpacialAcceleration() && Actor->UniqueIdx().IsValid())
	{
		ExternalDataLock.WriteLock();
		Chaos::TAccelerationStructureHandle<float,3> AccelerationHandle(Actor);
		GetSpacialAcceleration()->RemoveElementFrom(AccelerationHandle,Actor->SpatialIdx());
		ExternalDataLock.WriteUnlock();
	}
#endif
}

void FChaosScene::UpdateActorInAccelerationStructure(const FPhysicsActorHandle& Actor)
{
#if WITH_CHAOS
	using namespace Chaos;

	if(GetSpacialAcceleration())
	{
		ExternalDataLock.WriteLock();

		auto SpatialAcceleration = GetSpacialAcceleration();

		if(SpatialAcceleration)
		{

			TAABB<FReal,3> WorldBounds;
			const bool bHasBounds = Actor->Geometry()->HasBoundingBox();
			if(bHasBounds)
			{
				WorldBounds = Actor->Geometry()->BoundingBox().TransformedAABB(TRigidTransform<FReal,3>(Actor->X(),Actor->R()));
			}


			Chaos::TAccelerationStructureHandle<float,3> AccelerationHandle(Actor);
			SpatialAcceleration->UpdateElementIn(AccelerationHandle,WorldBounds,bHasBounds,Actor->SpatialIdx());
		}

		GetSolver()->UpdateParticleInAccelerationStructure_External(Actor,/*bDelete=*/false);
		ExternalDataLock.WriteUnlock();
	}
#endif
}

void FChaosScene::UpdateActorsInAccelerationStructure(const TArrayView<FPhysicsActorHandle>& Actors)
{
#if WITH_CHAOS
	using namespace Chaos;

	if(GetSpacialAcceleration())
	{
		ExternalDataLock.WriteLock();

		auto SpatialAcceleration = GetSpacialAcceleration();

		if(SpatialAcceleration)
		{
			int32 NumActors = Actors.Num();
			for(int32 ActorIndex = 0; ActorIndex < NumActors; ++ActorIndex)
			{
				const FPhysicsActorHandle& Actor = Actors[ActorIndex];
				if(Actor != nullptr)
				{
					// @todo(chaos): dedupe code in UpdateActorInAccelerationStructure
					TAABB<FReal,3> WorldBounds;
					const bool bHasBounds = Actor->Geometry()->HasBoundingBox();
					if(bHasBounds)
					{
						WorldBounds = Actor->Geometry()->BoundingBox().TransformedAABB(TRigidTransform<FReal,3>(Actor->X(),Actor->R()));
					}

					Chaos::TAccelerationStructureHandle<float,3> AccelerationHandle(Actor);
					SpatialAcceleration->UpdateElementIn(AccelerationHandle,WorldBounds,bHasBounds,Actor->SpatialIdx());
				}
			}
		}

		for(int32 ActorIndex = 0; ActorIndex < Actors.Num(); ++ActorIndex)
		{
			const FPhysicsActorHandle& Actor = Actors[ActorIndex];
			if(Actor != nullptr)
			{
				GetSolver()->UpdateParticleInAccelerationStructure_External(Actor,/*bDelete=*/false);
			}
		}

		ExternalDataLock.WriteUnlock();
	}
#endif
}

void FChaosScene::AddActorsToScene_AssumesLocked(TArray<FPhysicsActorHandle>& InHandles,const bool bImmediate)
{
#if WITH_CHAOS
	Chaos::FPhysicsSolver* Solver = GetSolver();
	Chaos::ISpatialAcceleration<Chaos::TAccelerationStructureHandle<float,3>,float,3>* SpatialAcceleration = GetSpacialAcceleration();
	for(FPhysicsActorHandle& Handle : InHandles)
	{
		FChaosEngineInterface::AddActorToSolver(Handle,Solver);

		// Optionally add this to the game-thread acceleration structure immediately
		if(bImmediate && SpatialAcceleration)
		{
			// Get the bounding box for the particle if it has one
			bool bHasBounds = Handle->Geometry()->HasBoundingBox();
			Chaos::TAABB<float,3> WorldBounds;
			if(bHasBounds)
			{
				const Chaos::TAABB<float,3> LocalBounds = Handle->Geometry()->BoundingBox();
				WorldBounds = LocalBounds.TransformedAABB(Chaos::TRigidTransform<float,3>(Handle->X(),Handle->R()));
			}

			// Insert the particle
			Chaos::TAccelerationStructureHandle<float,3> AccelerationHandle(Handle);
			SpatialAcceleration->UpdateElementIn(AccelerationHandle,WorldBounds,bHasBounds,Handle->SpatialIdx());
		}
	}
#endif
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

	// Prereqs for the final completion task to run (collection of all the solver tasks)
	FGraphEventArray CompletionTaskPrerequisites;

	for(FPhysicsSolverBase* Solver : SolverList)
	{
		CompletionTaskPrerequisites.Add(Solver->AdvanceAndDispatch_External(UseDeltaTime));
	}

	// For unit testing in single threaded mode we have no task graph, so call complete directly if possible
	bool bCallDirectly = true;
	for(const auto& Prereq : CompletionTaskPrerequisites)
	{
		if(Prereq && !Prereq->IsComplete())
		{
			bCallDirectly = false;
			break;
		}
	}

	if(bCallDirectly)
	{
		CompleteSceneSimulationImp();
	}
	else
	{
		// Setup post simulate tasks

		DECLARE_CYCLE_STAT(TEXT("FDelegateGraphTask.CompletePhysicsSimulation"),STAT_FDelegateGraphTask_CompletePhysicsSimulation,STATGROUP_TaskGraphTasks);

		// Completion event runs in parallel and will flip out our buffers, gamethread work can be done in EndFrame (Called by world after this completion event finishes)
		CompletionEvent = FDelegateGraphTask::CreateAndDispatchWhenReady(FDelegateGraphTask::FDelegate::CreateRaw(this,&FChaosScene::CompleteSceneSimulation),GET_STATID(STAT_FDelegateGraphTask_CompletePhysicsSimulation),&CompletionTaskPrerequisites,ENamedThreads::GameThread,ENamedThreads::AnyHiPriThreadHiPriTask);
	}
	
#endif
}

void FChaosScene::OnSyncBodies(Chaos::FPBDRigidDirtyParticlesBufferAccessor& Accessor)
{
	//simple implementation that pulls data over. Used for unit testing, engine has its own version of this
	const Chaos::FPBDRigidDirtyParticlesBufferOut* DirtyParticleBuffer = Accessor.GetSolverOutData();
	TSet<FGeometryCollectionPhysicsProxy*> GCProxies;

	for(Chaos::TGeometryParticle<float,3>* DirtyParticle : DirtyParticleBuffer->DirtyGameThreadParticles)
	{
		if(IPhysicsProxyBase* ProxyBase = DirtyParticle->GetProxy())
		{
			if(ProxyBase->GetType() == EPhysicsProxyType::SingleRigidParticleType)
			{
				FSingleParticlePhysicsProxy< Chaos::TPBDRigidParticle<float,3> > * Proxy = static_cast<FSingleParticlePhysicsProxy< Chaos::TPBDRigidParticle<float,3> >*>(ProxyBase);
				Proxy->PullFromPhysicsState();
			}
			else if(ProxyBase->GetType() == EPhysicsProxyType::GeometryCollectionType)
			{
				FGeometryCollectionPhysicsProxy* Proxy = static_cast<FGeometryCollectionPhysicsProxy*>(ProxyBase);
				GCProxies.Add(Proxy);
			}
		}
	}

	for(IPhysicsProxyBase* ProxyBase : DirtyParticleBuffer->PhysicsParticleProxies)
	{
		if(ProxyBase->GetType() == EPhysicsProxyType::GeometryCollectionType)
		{
			FGeometryCollectionPhysicsProxy* Proxy = static_cast<FGeometryCollectionPhysicsProxy*>(ProxyBase);
			GCProxies.Add(Proxy);
		} else
		{
			ensure(false); // Unhandled physics only particle proxy!
		}
	}

	for(FGeometryCollectionPhysicsProxy* GCProxy : GCProxies)
	{
		GCProxy->PullFromPhysicsState();
	}

}

void FChaosScene::CompleteSceneSimulation(ENamedThreads::Type CurrentThread,const FGraphEventRef& MyCompletionGraphEvent)
{
	CompleteSceneSimulationImp();
}

void FChaosScene::CompleteSceneSimulationImp()
{
#if WITH_CHAOS
	using namespace Chaos;

	// Cache our results to the threaded buffer.
	{
		LLM_SCOPE(ELLMTag::Chaos);
		SCOPE_CYCLE_COUNTER(STAT_BufferPhysicsResults);

		FChaosSolversModule* Module = FChaosSolversModule::GetModule();

		check(Module);

		TArray<FPhysicsSolverBase*> SolverList = Module->GetSolversMutable(Owner);

		TArray<FPhysicsSolverBase*> ActiveSolvers;

		if(SolverList.Num() > 0)
		{
			ActiveSolvers.Reserve(SolverList.Num());

			// #BG calculate active solver list once as we dispatch our first task
			for(FPhysicsSolverBase* Solver : SolverList)
			{
				Solver->CastHelper([&ActiveSolvers](auto& Concrete)
				{
					if(Concrete.Enabled() && Concrete.HasActiveParticles())
					{
						ActiveSolvers.Add(&Concrete);
					}
				});
			}
		}

		if(SceneSolver && SceneSolver->Enabled() && SceneSolver->HasActiveParticles())
		{
			ActiveSolvers.AddUnique(SceneSolver);
		}

		const int32 NumActiveSolvers = ActiveSolvers.Num();

		PhysicsParallelFor(NumActiveSolvers,[&](int32 Index)
		{
			//TODO: support any type not just default traits
			FPhysicsSolverBase* Solver = ActiveSolvers[Index];
			auto& Concrete = Solver->CastChecked<Chaos::FDefaultTraits>();
			Concrete.GetDirtyParticlesBuffer()->CaptureSolverData(&Concrete);
			Concrete.BufferPhysicsResults();
			Concrete.FlipBuffers();
		});
	}
#endif
}

template <typename TSolver>
void FChaosScene::SyncBodies(TSolver* Solver)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("SyncBodies"),STAT_SyncBodies,STATGROUP_Physics);

	Chaos::FPBDRigidDirtyParticlesBufferAccessor Accessor(Solver->GetDirtyParticlesBuffer());
	OnSyncBodies(Accessor);


	//
	// @todo(chaos) : Add Dirty Constraints Support
	//
	// This is temporary constraint code until the DirtyParticleBuffer
	// can be updated to support constraints. In summary : The 
	// FDirtyPropertiesManager is going to be updated to support a 
	// FDirtySet that is specific to a TConstraintProperties class.
	//
	for (FJointConstraintPhysicsProxy* Proxy : Solver->GetJointConstraintPhysicsProxy())
	{
		Proxy->PullFromPhysicsState();
	}

}


// Find the number of dirty elements in all substructures that has dirty elements that we know of
// This is non recursive for now
// Todo: consider making DirtyElementsCount a method on ISpatialAcceleration instead
int32 DirtyElementCount(Chaos::ISpatialAccelerationCollection<Chaos::TAccelerationStructureHandle<Chaos::FReal,3>,Chaos::FReal,3>& Collection)
{
	using namespace Chaos;
	int32 DirtyElements = 0;
	TArray<FSpatialAccelerationIdx> SpatialIndices = Collection.GetAllSpatialIndices();
	for(const FSpatialAccelerationIdx SpatialIndex : SpatialIndices)
	{
		auto SubStructure = Collection.GetSubstructure(SpatialIndex);
		if(const auto AABBTree = SubStructure->template As<TAABBTree<TAccelerationStructureHandle<FReal,3>,TAABBTreeLeafArray<TAccelerationStructureHandle<FReal,3>,FReal>,FReal>>())
		{
			DirtyElements += AABBTree->NumDirtyElements();
		} else if(const auto AABBTreeBV = SubStructure->template As<TAABBTree<TAccelerationStructureHandle<FReal,3>,TBoundingVolume<TAccelerationStructureHandle<FReal,3>,FReal,3>,FReal>>())
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
	using SpatialAccelerationCollection = ISpatialAccelerationCollection<TAccelerationStructureHandle<FReal,3>,FReal,3>;

	SCOPE_CYCLE_COUNTER(STAT_Scene_EndFrame);

	if(CVar_ChaosSimulationEnable.GetValueOnGameThread() == 0 || GetSolver() == nullptr)
	{
		return;
	}

	int32 DirtyElements = DirtyElementCount(GetSpacialAcceleration()->AsChecked<SpatialAccelerationCollection>());
	CSV_CUSTOM_STAT(ChaosPhysics,AABBTreeDirtyElementCount,DirtyElements,ECsvCustomStatOp::Set);

	check(!CompletionEvent || CompletionEvent->IsComplete());	//CompletionEvent being null means we were able to schedule completion immediately
	//check(PhysicsTickTask->IsComplete());
	CompletionEvent = nullptr;

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

		TArray<FPhysicsSolverBase*> ActiveSolvers;
		ActiveSolvers.Reserve(SolverList.Num());

		// #BG calculate active solver list once as we dispatch our first task
		for(FPhysicsSolverBase* Solver : SolverList)
		{
			Solver->CastHelper([&ActiveSolvers](auto& Concrete)
			{
				if(Concrete.HasActiveParticles())
				{
					ActiveSolvers.Add(&Concrete);
				}
			});

		}

		const int32 NumActiveSolvers = ActiveSolvers.Num();

		for(FPhysicsSolverBase* Solver : ActiveSolvers)
		{
			Solver->CastHelper([&ActiveSolvers,this](auto& Concrete)
			{
				SyncBodies(&Concrete);
				Concrete.SyncEvents_GameThread();

				{
					SCOPE_CYCLE_COUNTER(STAT_SqUpdateMaterials);
					Concrete.SyncQueryMaterials();
				}
			});
		}
	}

	OnPhysScenePostTick.Broadcast(this);
#endif
}

void FChaosScene::WaitPhysScenes()
{
	if(CompletionEvent && !CompletionEvent->IsComplete())
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_FPhysScene_WaitPhysScenes);
		FTaskGraphInterface::Get().WaitUntilTaskCompletes(CompletionEvent,ENamedThreads::GameThread);
	}
}

FGraphEventRef FChaosScene::GetCompletionEvent()
{
	return CompletionEvent;
}

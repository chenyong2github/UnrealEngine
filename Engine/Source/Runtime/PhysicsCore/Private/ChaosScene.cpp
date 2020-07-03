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

DECLARE_CYCLE_STAT(TEXT("Update Kinematics On Deferred SkelMeshes"),STAT_UpdateKinematicsOnDeferredSkelMeshesChaos,STATGROUP_Physics);

FChaosScene::FChaosScene(
	UObject* OwnerPtr
#if CHAOS_CHECKED
	, const FName& DebugName
#endif
)
	: ChaosModule(nullptr)
	,SceneSolver(nullptr)
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
	if(SceneSolver && SceneSolver->GetThreadingMode() == Chaos::EThreadingModeTemp::SingleThread)
	{
		if(GetSolver() && GetSolver()->GetEvolution())
		{
			return GetSolver()->GetEvolution()->GetSpatialAcceleration();
		}

		return nullptr;
	}

	return SolverAccelerationStructure.Get();
}

Chaos::ISpatialAcceleration<Chaos::TAccelerationStructureHandle<float,3>,float,3>* FChaosScene::GetSpacialAcceleration()
{
	if(SceneSolver && SceneSolver->GetThreadingMode() == Chaos::EThreadingModeTemp::SingleThread)
	{
		if(SceneSolver->GetEvolution())
		{
			return SceneSolver->GetEvolution()->GetSpatialAcceleration();
		}

		return nullptr;
	}

	return SolverAccelerationStructure.Get();
}

void FChaosScene::CopySolverAccelerationStructure()
{
	if(SceneSolver && SceneSolver->GetThreadingMode() != Chaos::EThreadingModeTemp::SingleThread)
	{
		ExternalDataLock.WriteLock();
		SceneSolver->FlushCommands_External();	//make sure any pending commands are flushed so that scene query structure is up to date
		SceneSolver->GetEvolution()->UpdateExternalAccelerationStructure(SolverAccelerationStructure);
		ExternalDataLock.WriteUnlock();
	}
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
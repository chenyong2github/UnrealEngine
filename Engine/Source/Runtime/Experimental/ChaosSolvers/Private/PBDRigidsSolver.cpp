// Copyright Epic Games, Inc. All Rights Reserved.

#include "PBDRigidsSolver.h"

#include "Async/AsyncWork.h"
#include "Chaos/ChaosArchive.h"
#include "Chaos/PBDCollisionConstraintsUtil.h"
#include "Chaos/Utilities.h"
#include "Chaos/ChaosDebugDraw.h"
#include "ChaosStats.h"
#include "ChaosSolversModule.h"
#include "HAL/FileManager.h"
#include "Misc/ScopeLock.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "PhysicsProxy/SkeletalMeshPhysicsProxy.h"
#include "PhysicsProxy/StaticMeshPhysicsProxy.h"
#include "PhysicsProxy/GeometryCollectionPhysicsProxy.h"
#include "PhysicsProxy/FieldSystemPhysicsProxy.h"
#include "EventDefaults.h"
#include "EventsData.h"


DEFINE_LOG_CATEGORY_STATIC(LogPBDRigidsSolver, Log, All);

// DebugDraw CVars
#if CHAOS_DEBUG_DRAW
int32 ChaosSolverDrawCollisions = 0;
int32 ChaosSolverDrawBPBounds = 0;
FAutoConsoleVariableRef CVarChaosSolverDrawCollisions(TEXT("p.Chaos.Solver.DebugDrawCollisions"), ChaosSolverDrawCollisions, TEXT("Draw Collisions (0 = never; 1 = end of frame)."));
FAutoConsoleVariableRef CVarChaosSolverDrawBPBounds(TEXT("p.Chaos.Solver.DrawBPBounds"), ChaosSolverDrawBPBounds, TEXT("Draw bounding volumes inside the broadphase (0 = never; 1 = end of frame)."));
#endif

bool ChaosSolverUseParticlePool = true;
FAutoConsoleVariableRef CVarChaosSolverUseParticlePool(TEXT("p.Chaos.Solver.UseParticlePool"), ChaosSolverUseParticlePool, TEXT("Whether or not to use dirty particle pool (Optim)"));

int32 ChaosSolverParticlePoolNumFrameUntilShrink = 30;
FAutoConsoleVariableRef CVarChaosSolverParticlePoolNumFrameUntilShrink(TEXT("p.Chaos.Solver.ParticlePoolNumFrameUntilShrink"), ChaosSolverParticlePoolNumFrameUntilShrink, TEXT("Num Frame until we can potentially shrink the pool"));

int32 ChaosSolverCollisionDefaultIterationsCVar = 4;
FAutoConsoleVariableRef CVarChaosSolverCollisionDefaultIterations(TEXT("p.ChaosSolverCollisionDefaultIterations"), ChaosSolverCollisionDefaultIterationsCVar, TEXT("Default collision iterations for the solver.[def:1]"));

int32 ChaosSolverCollisionDefaultPushoutIterationsCVar = 4;
FAutoConsoleVariableRef CVarChaosSolverCollisionDefaultPushoutIterations(TEXT("p.ChaosSolverCollisionDefaultPushoutIterations"), ChaosSolverCollisionDefaultPushoutIterationsCVar, TEXT("Default collision pushout iterations for the solver.[def:1]"));

int32 ChaosSolverCleanupCommandsOnDestruction = 1;
FAutoConsoleVariableRef CVarChaosSolverCleanupCommandsOnDestruction(TEXT("p.Chaos.Solver.CleanupCommandsOnDestruction"), ChaosSolverCleanupCommandsOnDestruction, TEXT("Whether or not to run internal command queue cleanup on solver destruction (0 = no cleanup, >0 = cleanup all commands)"));



namespace Chaos
{

	class AdvanceOneTimeStepTask : public FNonAbandonableTask
	{
		friend class FAutoDeleteAsyncTask<AdvanceOneTimeStepTask>;
	public:
		AdvanceOneTimeStepTask(
			FPBDRigidsSolver* Scene,
			const float DeltaTime)
			: MSolver(Scene)
			, MDeltaTime(DeltaTime)
		{
			UE_LOG(LogPBDRigidsSolver, Verbose, TEXT("AdvanceOneTimeStepTask::AdvanceOneTimeStepTask()"));
		}

		void DoWork()
		{
			LLM_SCOPE(ELLMTag::Chaos);
			UE_LOG(LogPBDRigidsSolver, Verbose, TEXT("AdvanceOneTimeStepTask::DoWork()"));

			{
				SCOPE_CYCLE_COUNTER(STAT_UpdateParams);
				Chaos::TPBDPositionConstraints<float, 3> PositionTarget; // Dummy for now
				TMap<int32, int32> PositionTargetedParticles;
				//TArray<FKinematicProxy> AnimatedPositions;
				Chaos::TArrayCollectionArray<float> Strains;
				for (FFieldSystemPhysicsProxy* FieldObj : MSolver->GetFieldSystemPhysicsProxies())
				{
					auto& GeomCollectionParticles = MSolver->GetEvolution()->GetParticles().GetGeometryCollectionParticles();
					FieldObj->FieldParameterUpdateCallback(MSolver, GeomCollectionParticles, Strains,
						PositionTarget, PositionTargetedParticles, /*AnimatedPositions,*/ MSolver->GetSolverTime());
					auto& ClusteredParticles = MSolver->GetEvolution()->GetParticles().GetClusteredParticles();
					FieldObj->FieldParameterUpdateCallback(MSolver, ClusteredParticles, Strains,
						PositionTarget, PositionTargetedParticles, /*AnimatedPositions,*/ MSolver->GetSolverTime());
				}

				for (FGeometryCollectionPhysicsProxy* Obj : MSolver->GetGeometryCollectionPhysicsProxies())
				{
					Obj->ParameterUpdateCallback(MSolver->GetEvolution()->GetParticles().GetGeometryCollectionParticles(), MSolver->GetSolverTime());
				}

			}

			{
				//SCOPE_CYCLE_COUNTER(STAT_BeginFrame);
				//MSolver->StartFrameCallback(MDeltaTime, MSolver->GetSolverTime());
			}

			{
				SCOPE_CYCLE_COUNTER(STAT_EvolutionAndKinematicUpdate);

				// This outer loop can potentially cause the system to lose energy over integration
				// in a couple of different cases.
				//
				// * If we have a timestep that's smaller than MinDeltaTime, then we just won't step.
				//   Yes, we'll lose some teeny amount of energy, but we'll avoid 1/dt issues.
				//
				// * If we have used all of our substeps but still have time remaining, then some
				//   energy will be lost.
				const float MinDeltaTime = MSolver->GetMinDeltaTime();
				const float MaxDeltaTime = MSolver->GetMaxDeltaTime();
				int32 StepsRemaining = MSolver->GetMaxSubSteps();
				float TimeRemaining = MDeltaTime;
				while (StepsRemaining > 0 && TimeRemaining > MinDeltaTime)
				{
					--StepsRemaining;
					const float DeltaTime = MaxDeltaTime > 0.f ? FMath::Min(TimeRemaining, MaxDeltaTime) : TimeRemaining;
					TimeRemaining -= DeltaTime;

					Chaos::TArrayCollectionArray<FVector> Forces, Torques;
					for (FFieldSystemPhysicsProxy* Obj : MSolver->GetFieldSystemPhysicsProxies())
					{
						auto& GeomCollectionParticles = MSolver->GetEvolution()->GetParticles().GetGeometryCollectionParticles();
						Obj->FieldForcesUpdateCallback(MSolver, GeomCollectionParticles, Forces, Torques, MSolver->GetSolverTime());
						auto& ClusteredParticles = MSolver->GetEvolution()->GetParticles().GetClusteredParticles();
						Obj->FieldForcesUpdateCallback(MSolver, ClusteredParticles, Forces, Torques, MSolver->GetSolverTime());
					}

					for (FGeometryCollectionPhysicsProxy* Obj : MSolver->GetGeometryCollectionPhysicsProxies())
					{
						Obj->ParameterUpdateCallback(MSolver->GetEvolution()->GetParticles().GetGeometryCollectionParticles(), MSolver->GetSolverTime());
					}

					MSolver->GetEvolution()->AdvanceOneTimeStep(DeltaTime);
				}

#if CHAOS_CHECKED
				// If time remains, then log why we have lost energy over the timestep.
				if (TimeRemaining > 0.f)
				{
					auto& GeomCollectionParticles = MSolver->GetEvolution()->GetParticles().GetGeometryCollectionParticles();
					Obj->FieldForcesUpdateCallback(MSolver, GeomCollectionParticles, Forces, Torques, MSolver->GetSolverTime());
					auto& ClusteredParticles = MSolver->GetEvolution()->GetParticles().GetClusteredParticles();
					Obj->FieldForcesUpdateCallback(MSolver, ClusteredParticles, Forces, Torques, MSolver->GetSolverTime());
					if (StepsRemaining == 0)
					{
						UE_LOG(LogPBDRigidsSolver, Warning, TEXT("AdvanceOneTimeStepTask::DoWork() - Energy lost over %fs due to too many substeps over large timestep"), TimeRemaining);
					}
					else
					{
						UE_LOG(LogPBDRigidsSolver, Warning, TEXT("AdvanceOneTimeStepTask::DoWork() - Energy lost over %fs due to small timestep remainder"), TimeRemaining);
					}
				}
#endif
			}

			{
				SCOPE_CYCLE_COUNTER(STAT_EventDataGathering);
				{
					SCOPE_CYCLE_COUNTER(STAT_FillProducerData);
					MSolver->GetEventManager()->FillProducerData(MSolver);
				}
				{
					SCOPE_CYCLE_COUNTER(STAT_FlipBuffersIfRequired);
					MSolver->GetEventManager()->FlipBuffersIfRequired();
				}
			}

			{
				SCOPE_CYCLE_COUNTER(STAT_EndFrame);
				MSolver->GetEvolution()->EndFrame(MDeltaTime);
			}

			MSolver->GetSolverTime() += MDeltaTime;
			MSolver->GetCurrentFrame()++;
			MSolver->PostTickDebugDraw();
		}

	protected:

		TStatId GetStatId() const
		{
			RETURN_QUICK_DECLARE_CYCLE_STAT(AdvanceOneTimeStepTask, STATGROUP_ThreadPoolAsyncTasks);
		}

		FPBDRigidsSolver* MSolver;
		float MDeltaTime;
		TSharedPtr<FCriticalSection> PrevLock, CurrentLock;
		TSharedPtr<FEvent> PrevEvent, CurrentEvent;
	};

	FPBDRigidsSolver::FPBDRigidsSolver(const EMultiBufferMode BufferingModeIn, UObject* InOwner)
		: Super(BufferingModeIn, InOwner)
		, CurrentFrame(0)
		, MTime(0.0)
		, MLastDt(0.0)
		, MMaxDeltaTime(0.0)
		, MMinDeltaTime(1.e-10f)
		, MMaxSubSteps(1)
		, bEnabled(false)
		, bHasFloor(true)
		, bIsFloorAnalytic(false)
		, FloorHeight(0.f)
		, MEvolution(new FPBDRigidsEvolution(Particles, ChaosSolverCollisionDefaultIterationsCVar, ChaosSolverCollisionDefaultPushoutIterationsCVar, BufferingModeIn == Chaos::EMultiBufferMode::Single))
		, MEventManager(new FEventManager(BufferingModeIn))
		, MSolverEventFilters(new FSolverEventFilters())
		, MActiveParticlesBuffer(new FActiveParticlesBuffer(BufferingModeIn))
		, MCurrentLock(new FCriticalSection())
	{
		UE_LOG(LogPBDRigidsSolver, Verbose, TEXT("PBDRigidsSolver::PBDRigidsSolver()"));
		Reset();
	}

	FPBDRigidsSolver::~FPBDRigidsSolver()
	{
		if(ChaosSolverCleanupCommandsOnDestruction != 0)
		{
			TFunction<void(FPhysicsSolver*)> Command;
			while(CommandQueue.Dequeue(Command))
			{
				Command(this);
			}
		}
	}

	float MaxBoundsForTree = 10000;
	FAutoConsoleVariableRef CVarMaxBoundsForTree(
		TEXT("p.MaxBoundsForTree"),
		MaxBoundsForTree,
		TEXT("The max bounds before moving object into a large objects structure. Only applies on object registration")
		TEXT(""),
		ECVF_Default);

	void FPBDRigidsSolver::RegisterObject(TGeometryParticle<float, 3>* GTParticle)
	{
		LLM_SCOPE(ELLMTag::Chaos);

		UE_LOG(LogPBDRigidsSolver, Verbose, TEXT("FPBDRigidsSolver::RegisterObject()"));

		// Make sure this particle doesn't already have a proxy
		checkSlow(GTParticle->Proxy == nullptr);

		if (GTParticle->Geometry() && GTParticle->Geometry()->HasBoundingBox() && GTParticle->Geometry()->BoundingBox().Extents().Max() >= MaxBoundsForTree)
		{
			GTParticle->SetSpatialIdx(FSpatialAccelerationIdx{ 1,0 });
		}
		if (!ensure(GTParticle->IsParticleValid()))
		{
			return;
		}

		// NOTE: Do we really need these lists of proxies if we can just
		// access them through the GTParticles list?
		
		Chaos::FParticleData* ProxyData;
		IPhysicsProxyBase* ProxyBase;

		GTParticle->SetUniqueIdx(GetEvolution()->GenerateUniqueIdx());

		// Make a physics proxy, giving it our particle and particle handle
		const EParticleType InParticleType = GTParticle->ObjectType();
		if (InParticleType == EParticleType::Rigid)
		{
			auto Proxy = new FRigidParticlePhysicsProxy(GTParticle->CastToRigidParticle(), nullptr);
			RigidParticlePhysicsProxies.Add((FRigidParticlePhysicsProxy*)Proxy);
			ProxyData = Proxy->NewData();
			ProxyBase = Proxy;
		}
		else if (InParticleType == EParticleType::Kinematic)
		{
			auto Proxy = new FKinematicGeometryParticlePhysicsProxy(GTParticle->CastToKinematicParticle(), nullptr);
			KinematicGeometryParticlePhysicsProxies.Add((FKinematicGeometryParticlePhysicsProxy*)Proxy);
			ProxyData = Proxy->NewData();
			ProxyBase = Proxy;
		}
		else // Assume it's a static (geometry) if it's not dynamic or kinematic
		{
			auto Proxy = new FGeometryParticlePhysicsProxy(GTParticle, nullptr);
			GeometryParticlePhysicsProxies.Add((FGeometryParticlePhysicsProxy*)Proxy);
			ProxyData = Proxy->NewData();
			ProxyBase = Proxy;
		}

		ProxyBase->SetSolver(this);

		// Associate the proxy with the particle
		GTParticle->Proxy = ProxyBase;

		//enqueue onto physics thread for finalizing registration
		FChaosSolversModule::GetModule()->GetDispatcher()->EnqueueCommandImmediate(this, [GTParticle, InParticleType, ProxyBase, ProxyData](FPBDRigidsSolver* Solver)
		{
			UE_LOG(LogPBDRigidsSolver, Verbose, TEXT("FPBDRigidsSolver::RegisterObject() ~ Dequeue"));

			// Create a handle for the new particle
			TGeometryParticleHandle<float, 3>* Handle;

			// Insert the particle ptr into the auxiliary array
			//
			// TODO: Is this array even necessary?
			//       Proxy should be able to map back to Particle.
			//

			const auto& GeomData = static_cast<const Chaos::TGeometryParticleData<FReal,3>*>(ProxyData);

			if (InParticleType == EParticleType::Rigid)
			{
				Handle = Solver->Particles.CreateDynamicParticles(1, &GeomData->UniqueIdx)[0];
				auto Proxy = static_cast<FRigidParticlePhysicsProxy*>(ProxyBase);
				Proxy->SetHandle(Handle->CastToRigidParticle());
				Proxy->PushToPhysicsState(ProxyData);
			}
			else if (InParticleType == EParticleType::Kinematic)
			{
				Handle = Solver->Particles.CreateKinematicParticles(1, &GeomData->UniqueIdx)[0];
				auto Proxy = static_cast<FKinematicGeometryParticlePhysicsProxy*>(ProxyBase);
				Proxy->SetHandle(Handle->CastToKinematicParticle());
				Proxy->PushToPhysicsState(ProxyData);
			}
			else // Assume it's a static (geometry) if it's not dynamic or kinematic
			{
				Handle = Solver->Particles.CreateStaticParticles(1, &GeomData->UniqueIdx)[0];

				auto Proxy = static_cast<FGeometryParticlePhysicsProxy*>(ProxyBase);
				Proxy->SetHandle(Handle);
				Proxy->PushToPhysicsState(ProxyData);
			}

			Handle->GTGeometryParticle() = GTParticle;
			Solver->MParticleToProxy.Add(Handle, ProxyBase);
			Solver->GetEvolution()->CreateParticle(Handle);

			delete ProxyData;
		});
	}

	void FPBDRigidsSolver::UnregisterObject(TGeometryParticle<float, 3>* GTParticle)
	{
		UE_LOG(LogPBDRigidsSolver, Verbose, TEXT("FPBDRigidsSolver::UnregisterObject()"));

		// Get the proxy associated with this particle
		IPhysicsProxyBase* InProxy = GTParticle->Proxy;
		check(InProxy);

		// Grab the particle's type
		const EParticleType InParticleType = GTParticle->ObjectType();

		// remove the proxy from the invalidation list
		RemoveDirtyProxy(GTParticle->Proxy);

		// Null out the particle's proxy pointer
		GTParticle->Proxy = nullptr;

		// Remove the proxy from the GT proxy map
		if (InParticleType == EParticleType::Rigid)
		{
			RigidParticlePhysicsProxies.RemoveSingleSwap((FRigidParticlePhysicsProxy*)InProxy);
		}
		else if (InParticleType == EParticleType::Kinematic)
		{
			KinematicGeometryParticlePhysicsProxies.RemoveSingleSwap((FKinematicGeometryParticlePhysicsProxy*)InProxy);
		}
		else if (InParticleType == EParticleType::GeometryCollection)
		{
			check(false); // This shouldn't happen.
		}
		else
		{
			GeometryParticlePhysicsProxies.RemoveSingleSwap((FGeometryParticlePhysicsProxy*)InProxy);
		}

		// Enqueue a command to remove the particle and delete the proxy
		FChaosSolversModule::GetModule()->GetDispatcher()->EnqueueCommandImmediate(this, [InProxy, InParticleType](FPBDRigidsSolver* Solver)
		{
			UE_LOG(LogPBDRigidsSolver, Verbose, TEXT("FPBDRigidsSolver::UnregisterObject() ~ Dequeue"));

				// Generally need to remove stale events for particles that no longer exist
				Solver->GetEventManager()->ClearEvents<FCollisionEventData>([InProxy]
				(FCollisionEventData& EventDataInOut)
				{
					Chaos::FCollisionDataArray const& CollisionData = EventDataInOut.CollisionData.AllCollisionsArray;
					if (CollisionData.Num() > 0)
					{
						check(InProxy);
						TArray<int32> const* const CollisionIndices = EventDataInOut.PhysicsProxyToCollisionIndices.PhysicsProxyToIndicesMap.Find(InProxy);
						if (CollisionIndices)
						{
							for (int32 EncodedCollisionIdx : *CollisionIndices)
							{
								bool bSwapOrder;
								int32 CollisionIdx = Chaos::FEventManager::DecodeCollisionIndex(EncodedCollisionIdx, bSwapOrder);

								// invalidate but don't delete from array, as this would mean we'd need to reindex PhysicsProxyToIndicesMap to maintain the other collisions lookup
								Chaos::TCollisionData<float, 3>& CollisionDataItem = EventDataInOut.CollisionData.AllCollisionsArray[CollisionIdx];
								CollisionDataItem.ParticleProxy = nullptr;
								CollisionDataItem.LevelsetProxy = nullptr;
							}

							EventDataInOut.PhysicsProxyToCollisionIndices.PhysicsProxyToIndicesMap.Remove(InProxy);
						}
					}

				});

			// Get the physics thread-handle from the proxy, and then delete the proxy.
			//
			// NOTE: We have to delete the proxy from its derived version, because the
			// base destructor is protected. This makes everything just a bit uglier,
			// maybe that extra safety is not needed if we continue to contain all
			// references to proxy instances entirely in Chaos?
			TGeometryParticleHandle<float, 3>* Handle;
			if (InParticleType == Chaos::EParticleType::Rigid)
			{
				auto Proxy = (FRigidParticlePhysicsProxy*)InProxy;
				Handle = Proxy->GetHandle();
				delete Proxy;
			}
			else if (InParticleType == Chaos::EParticleType::Kinematic)
			{
				auto Proxy = (FKinematicGeometryParticlePhysicsProxy*)InProxy;
				Handle = Proxy->GetHandle();
				delete Proxy;
			}
			else
			{
				auto Proxy = (FGeometryParticlePhysicsProxy*)InProxy;
				Handle = Proxy->GetHandle();
				delete Proxy;
			}

			Solver->MParticleToProxy.Remove(Handle);

			// Use the handle to destroy the particle data
			Solver->GetEvolution()->DestroyParticle(Handle);

		});

	}

	void FPBDRigidsSolver::RegisterObject(FGeometryCollectionPhysicsProxy* InProxy)
	{
		UE_LOG(LogPBDRigidsSolver, Verbose, TEXT("FPBDRigidsSolver::RegisterObject(FGeometryCollectionPhysicsProxy*)"));
		GeometryCollectionPhysicsProxies.AddUnique(InProxy);
		InProxy->SetSolver(this);
		InProxy->Initialize();
		InProxy->NewData(); // Buffers data on the proxy.
		FParticlesType* InParticles = &GetParticles();

		// Finish registration on the physics thread...
		FChaosSolversModule::GetModule()->GetDispatcher()->EnqueueCommandImmediate(this,
			[InParticles, InProxy](FPBDRigidsSolver* Solver)
		{
			UE_LOG(LogPBDRigidsSolver, Verbose, 
				TEXT("FPBDRigidsSolver::RegisterObject(FGeometryCollectionPhysicsProxy*)"));
			check(InParticles);
			InProxy->InitializeBodiesPT(Solver, *InParticles);
		});
	}

	bool FPBDRigidsSolver::UnregisterObject(FGeometryCollectionPhysicsProxy* InProxy)
	{
		InProxy->OnRemoveFromSolver(this);
		InProxy->SetSolver(static_cast<FPBDRigidsSolver*>(nullptr));
		return GeometryCollectionPhysicsProxies.Remove(InProxy) != 0;
	}

	void FPBDRigidsSolver::RegisterObject(FFieldSystemPhysicsProxy* InProxy)
	{
		UE_LOG(LogPBDRigidsSolver, Verbose, TEXT("FPBDRigidsSolver::RegisterObject(FFieldSystemPhysicsProxy*)"));
		FieldSystemPhysicsProxies.AddUnique(InProxy);
		InProxy->SetSolver(this);
		InProxy->Initialize();
		Chaos::FParticleData* ProxyData = InProxy->NewData();

		FChaosSolversModule::GetModule()->GetDispatcher()->EnqueueCommandImmediate(
			this,
			[InProxy, ProxyData](FPBDRigidsSolver* Solver)
			{
				UE_LOG(LogPBDRigidsSolver, Verbose,
					TEXT("FPBDRigidsSolver::RegisterObject(FFieldSystemPhysicsProxy*)"));
				//InProxy->ActivateBodies();
				InProxy->PushToPhysicsState(ProxyData);
			});
	}

	bool FPBDRigidsSolver::UnregisterObject(FFieldSystemPhysicsProxy* InProxy)
	{
		InProxy->SetSolver(static_cast<FPBDRigidsSolver*>(nullptr));
		return FieldSystemPhysicsProxies.Remove(InProxy) != 0;
	}

	bool FPBDRigidsSolver::IsSimulating() const
	{
		for (FGeometryParticlePhysicsProxy* Obj : GeometryParticlePhysicsProxies)
			if (Obj->IsSimulating())
				return true;
		for (FKinematicGeometryParticlePhysicsProxy* Obj : KinematicGeometryParticlePhysicsProxies)
			if (Obj->IsSimulating())
				return true;
		for (FRigidParticlePhysicsProxy* Obj : RigidParticlePhysicsProxies)
			if (Obj->IsSimulating())
				return true;
		for (FSkeletalMeshPhysicsProxy* Obj : SkeletalMeshPhysicsProxies)
			if (Obj->IsSimulating())
				return true;
		for (FStaticMeshPhysicsProxy* Obj : StaticMeshPhysicsProxies)
			if (Obj->IsSimulating())
				return true;
		for (FGeometryCollectionPhysicsProxy* Obj : GeometryCollectionPhysicsProxies)
			if (Obj->IsSimulating())
				return true;
		for (FFieldSystemPhysicsProxy* Obj : FieldSystemPhysicsProxies)
			if (Obj->IsSimulating())
				return true;
		return false;
	}

	void FPBDRigidsSolver::Reset()
	{
		UE_LOG(LogPBDRigidsSolver, Verbose, TEXT("PBDRigidsSolver::Reset()"));

		MTime = 0;
		MLastDt = 0.0f;
		bEnabled = false;
		CurrentFrame = 0;
		MMaxDeltaTime = 1.f;
		MMinDeltaTime = 1.e-10f;
		MMaxSubSteps = 1;
		MEvolution = TUniquePtr<FPBDRigidsEvolution>(new FPBDRigidsEvolution(Particles, ChaosSolverCollisionDefaultIterationsCVar, ChaosSolverCollisionDefaultPushoutIterationsCVar, BufferMode == EMultiBufferMode::Single)); 

		FEventDefaults::RegisterSystemEvents(*GetEventManager());
	}

	void FPBDRigidsSolver::ChangeBufferMode(Chaos::EMultiBufferMode InBufferMode)
	{
		// This seems unused inside the solver? #BH
		BufferMode = InBufferMode;
	}

	void FPBDRigidsSolver::AdvanceSolverBy(float DeltaTime)
	{
		UE_LOG(LogPBDRigidsSolver, Verbose, TEXT("PBDRigidsSolver::Tick(%3.5f)"), DeltaTime);
		if (bEnabled)
		{
			MLastDt = DeltaTime;
			AdvanceOneTimeStepTask(this, DeltaTime).DoWork();
		}

	}

	void FPBDRigidsSolver::SyncEvents_GameThread()
	{
		GetEventManager()->DispatchEvents();
	}

	/**
	 * ProxyType is FSingleParticlePhysicsProxy<T>, where T is:
	 *    Chaos::TPBDRigidParticle<float,3>
	 *    Chaos::TKinematicGeometryParticle<float,3>
	 *    Chaos::TGeometryParticle<float,3>
	 */
	template<typename ProxyType>
	void PushPhysicsStateExec(FPBDRigidsSolver * Solver, ProxyType* Proxy, Chaos::IDispatcher* Dispatcher)
	{
		LLM_SCOPE(ELLMTag::Chaos);

		if (Chaos::FParticleData* ProxyData = Proxy->NewData())
		{
			if (auto* RigidHandle = static_cast<Chaos::TGeometryParticleHandle<float, 3>*>(Proxy->GetHandle()))
			{
				if (Dispatcher)
				{
					Dispatcher->EnqueueCommandImmediate([Proxy, ProxyData, Solver](Chaos::FPersistentPhysicsTask* PhysThread)
					{
						// make sure the handle is still valid
						if (auto* Handle = static_cast<Chaos::TGeometryParticleHandle<float, 3>*>(Proxy->GetHandle()))
						{
							Solver->GetEvolution()->DirtyParticle(*Handle);
							Proxy->PushToPhysicsState(ProxyData);
						}
						delete ProxyData;
					});
				}
				else
				{
					Solver->GetEvolution()->DirtyParticle(*RigidHandle);
					Proxy->PushToPhysicsState(ProxyData);
					delete ProxyData;
				}

				Proxy->ClearAccumulatedData();
				Solver->RemoveDirtyProxy(Proxy);

			}
			else
			{
				delete ProxyData;
			}
		}
	}

	template<typename ParticleEntry, typename ProxyEntry, SIZE_T PreAllocCount>
	void FlushExec(FPBDRigidsSolver::TFramePool<ParticleEntry, ProxyEntry, PreAllocCount>& PoolParticles, Chaos::IDispatcher* Dispatcher, FPBDRigidsSolver * Solver)
	{
		Dispatcher->EnqueueCommandImmediate([Solver, &PoolParticles](Chaos::FPersistentPhysicsTask* PhysThread)
		{
			for (int32 i = 0; i < PoolParticles.GetEntryCount(); i++)
			{
				auto& Entry = PoolParticles.GetEntry(i);
				// make sure the handle is still valid
				if (auto* Handle = static_cast<Chaos::TGeometryParticleHandle<float, 3>*>(Entry.Proxy->GetHandle()))
				{
					Solver->GetEvolution()->DirtyParticle(*Handle);
					Entry.Proxy->PushToPhysicsState(&Entry.Particle);
				}
				Entry.Particle.Reset();
			}
		});
	}

	template<typename ProxyType, typename ParticleEntry, typename ProxyEntry, SIZE_T PreAllocCount>
	void PushPhysicsStateExec(FPBDRigidsSolver * Solver, ProxyType* Proxy, FPBDRigidsSolver::TFramePool<ParticleEntry, ProxyEntry, PreAllocCount>& PoolParticles, Chaos::IDispatcher* Dispatcher)
	{
		auto* RigidHandle = static_cast<Chaos::TGeometryParticleHandle<float, 3>*>(Proxy->GetHandle());
		if (RigidHandle == nullptr)
		{
			return;
		}

		// get a new entry in the pool
		auto& Entry = PoolParticles.GetNewEntry();
		Entry.Particle.Init(*Proxy->GetParticle());
		Entry.Proxy = Proxy;

		Proxy->ClearAccumulatedData();
		Solver->RemoveDirtyProxy(Proxy);
	}

	void PushPhysicsStateExec(FPBDRigidsSolver* Solver, FGeometryCollectionPhysicsProxy* Proxy, Chaos::IDispatcher* Dispatcher)
	{
		Proxy->NewData();
		auto Cmd = [Proxy, Solver](Chaos::FPersistentPhysicsTask* PhysThread)
		{
			auto* Evolution = Solver->GetEvolution();
			TArray<FGeometryCollectionPhysicsProxy::FClusterHandle*>& Handles = Proxy->GetSolverParticleHandles();
			for (auto* Handle : Handles) if (Handle) Evolution->DirtyParticle(*Handle);
			Proxy->PushToPhysicsState(nullptr);
		};
		if (Dispatcher) Dispatcher->EnqueueCommandImmediate(Cmd);  else Cmd(nullptr);
		Proxy->ClearAccumulatedData();
		Solver->RemoveDirtyProxy(Proxy);
	}

	void PushPhysicsStateExec(FPBDRigidsSolver* Solver, FFieldSystemPhysicsProxy* Proxy, Chaos::IDispatcher* Dispatcher)
	{
		Chaos::FParticleData* ProxyData = Proxy->NewData();
		auto Cmd = [Proxy, Solver, ProxyData](Chaos::FPersistentPhysicsTask* PhysThread)
		{
			Proxy->PushToPhysicsState(nullptr);
		};

		if (Dispatcher)
			Dispatcher->EnqueueCommandImmediate(Cmd);
		else
			Cmd(nullptr);
		Proxy->ClearAccumulatedData();
		Solver->RemoveDirtyProxy(Proxy);
	}

	void FPBDRigidsSolver::PushPhysicsStatePooled(IDispatcher* Dispatcher)
	{

		ensure(Dispatcher != nullptr);

		// reset the per frame pool
		RigidParticlePool.ResetPool();
		KinematicGeometryParticlePool.ResetPool();
		GeometryParticlePool.ResetPool();

		TArray< IPhysicsProxyBase*> DirtyProxiesArray = DirtyProxiesSet.Array();
		for (auto& Proxy : DirtyProxiesArray)
		{
			switch (Proxy->GetType())
			{
				//case EPhysicsProxyType::NoneType: // 0
				//case EPhysicsProxyType::StaticMeshType: // 1
			case EPhysicsProxyType::GeometryCollectionType: // 2
				PushPhysicsStateExec(this, static_cast<FGeometryCollectionPhysicsProxy*>(Proxy), Dispatcher); // non pool api
				break;
			case EPhysicsProxyType::SingleRigidParticleType: // 7
				PushPhysicsStateExec(this, static_cast<FSingleParticlePhysicsProxy< Chaos::TPBDRigidParticle<float, 3> >*>(Proxy), RigidParticlePool, Dispatcher);
				break;
				// case EPhysicsProxyType::FieldType: // 3
				// case EPhysicsProxyType::SkeletalMeshType: // 4
			case EPhysicsProxyType::SingleKinematicParticleType: // 6
				PushPhysicsStateExec(this, static_cast<FSingleParticlePhysicsProxy< Chaos::TKinematicGeometryParticle<float, 3> >*>(Proxy), KinematicGeometryParticlePool, Dispatcher);
				break;
			case EPhysicsProxyType::SingleGeometryParticleType: // 5
				PushPhysicsStateExec(this, static_cast<FSingleParticlePhysicsProxy< Chaos::TGeometryParticle<float, 3> >*>(Proxy), GeometryParticlePool, Dispatcher);
				break;
			default:
				ensure("Unknown proxy type in physics solver.");
			}
		}

		FlushExec(RigidParticlePool, Dispatcher, this);
		FlushExec(KinematicGeometryParticlePool, Dispatcher, this);
		FlushExec(GeometryParticlePool, Dispatcher, this);
	}

	void FPBDRigidsSolver::PushPhysicsState(IDispatcher* Dispatcher)
	{
		if (ChaosSolverUseParticlePool)
		{
			PushPhysicsStatePooled(Dispatcher);
		}
		else
		{
			if (DirtyProxiesSet.Num() == 0)
				return;
			TArray< IPhysicsProxyBase *> DirtyProxiesArray = DirtyProxiesSet.Array();
			for (auto & Proxy : DirtyProxiesArray)
			{
				switch (Proxy->GetType())
				{
					//case EPhysicsProxyType::NoneType: // 0
					//case EPhysicsProxyType::StaticMeshType: // 1
				case EPhysicsProxyType::GeometryCollectionType: // 2
					PushPhysicsStateExec(this, static_cast<FGeometryCollectionPhysicsProxy*>(Proxy), Dispatcher);
					break;
				case EPhysicsProxyType::FieldType: // 3
					PushPhysicsStateExec(this, static_cast<FFieldSystemPhysicsProxy*>(Proxy), Dispatcher);
					break;
					//case EPhysicsProxyType::SkeletalMeshType: // 4
				case EPhysicsProxyType::SingleRigidParticleType: // 7
					PushPhysicsStateExec(this, static_cast<FSingleParticlePhysicsProxy< Chaos::TPBDRigidParticle<float, 3> >*>(Proxy), Dispatcher);
					break;
				case EPhysicsProxyType::SingleKinematicParticleType: // 6
					PushPhysicsStateExec(this, static_cast<FSingleParticlePhysicsProxy< Chaos::TKinematicGeometryParticle<float, 3> >*>(Proxy), Dispatcher);
					break;
				case EPhysicsProxyType::SingleGeometryParticleType: // 5
					PushPhysicsStateExec(this, static_cast<FSingleParticlePhysicsProxy< Chaos::TGeometryParticle<float, 3> >*>(Proxy), Dispatcher);
					break;
				default:
					ensure("Unknown proxy type in physics solver.");
				}
			}
		}
	}

	void FPBDRigidsSolver::BufferPhysicsResults()
	{
		//ensure(IsInPhysicsThread());
		TArray<FGeometryCollectionPhysicsProxy*> ActiveGC;
		ActiveGC.Reserve(GeometryCollectionPhysicsProxies.Num());

		TParticleView<TPBDRigidParticles<float, 3>>& ActiveParticles = GetParticles().GetActiveParticlesView();
		for (Chaos::TPBDRigidParticleHandleImp<float, 3, false>& ActiveObject : ActiveParticles)
		{
			if (IPhysicsProxyBase* Proxy = GetProxy(ActiveObject.Handle()))
			{
				switch (ActiveObject.GetParticleType())
				{
				case Chaos::EParticleType::Rigid:
					((FRigidParticlePhysicsProxy*)(Proxy))->BufferPhysicsResults();
					break;
				case Chaos::EParticleType::Kinematic:
					((FKinematicGeometryParticlePhysicsProxy*)(Proxy))->BufferPhysicsResults();
					break;
				case Chaos::EParticleType::Static:
					((FGeometryParticlePhysicsProxy*)(Proxy))->BufferPhysicsResults();
					break;
				case Chaos::EParticleType::GeometryCollection:
					ActiveGC.AddUnique((FGeometryCollectionPhysicsProxy*)(Proxy));
					break;
				case Chaos::EParticleType::Clustered:
					ActiveGC.AddUnique((FGeometryCollectionPhysicsProxy*)(Proxy));
					break;
				default:
					check(false);
				}
			}
		}

		for (auto* GCProxy : ActiveGC)
		{
			GCProxy->BufferPhysicsResults();
		}
	}

	void FPBDRigidsSolver::FlipBuffers()
	{
		//ensure(IsInPhysicsThread());
		TArray<FGeometryCollectionPhysicsProxy*> ActiveGC;
		ActiveGC.Reserve(GeometryCollectionPhysicsProxies.Num());

		TParticleView<TPBDRigidParticles<float, 3>>& ActiveParticles = GetParticles().GetActiveParticlesView();
		for (Chaos::TPBDRigidParticleHandleImp<float, 3, false>& ActiveObject : ActiveParticles)
		{
			if (IPhysicsProxyBase* Proxy = GetProxy(ActiveObject.Handle()))
			{
				switch (ActiveObject.GetParticleType())
				{
				case Chaos::EParticleType::Rigid:
					((FRigidParticlePhysicsProxy*)(Proxy))->FlipBuffer();
					break;
				case Chaos::EParticleType::Kinematic:
					((FKinematicGeometryParticlePhysicsProxy*)(Proxy))->FlipBuffer();
					break;
				case Chaos::EParticleType::Static:
					((FGeometryParticlePhysicsProxy*)(Proxy))->FlipBuffer();
					break;
				case Chaos::EParticleType::GeometryCollection:
					ActiveGC.AddUnique((FGeometryCollectionPhysicsProxy*)(Proxy));
					break;
				case Chaos::EParticleType::Clustered:
					ActiveGC.AddUnique((FGeometryCollectionPhysicsProxy*)(Proxy));
					break;
				default:
					check(false);
				}
			}
		}

		for (auto* GCProxy : ActiveGC)
		{
			GCProxy->FlipBuffer();
		}
	}

	// This function is not called during normal Engine execution.  
	// FPhysScene_ChaosInterface::EndFrame() calls 
	// FPhysScene_ChaosInterface::SyncBodies() instead, and then immediately afterwards 
	// calls FPBDRigidsSovler::SyncEvents_GameThread().  This function is used by tests,
	// however.
	void FPBDRigidsSolver::UpdateGameThreadStructures()
	{
		//ensure(IsInGameThread());
		TArray<FGeometryCollectionPhysicsProxy*> ActiveGC;
		ActiveGC.Reserve(GeometryCollectionPhysicsProxies.Num());

		TParticleView<TPBDRigidParticles<float, 3>>& ActiveParticles = GetParticles().GetActiveParticlesView();
		for (Chaos::TPBDRigidParticleHandleImp<float, 3, false>& ActiveObject : ActiveParticles)
		{
			if (IPhysicsProxyBase* Proxy = GetProxy(ActiveObject.Handle()))
			{
				switch (ActiveObject.GetParticleType())
				{
				case Chaos::EParticleType::Rigid:
					((FRigidParticlePhysicsProxy*)(Proxy))->PullFromPhysicsState();
					break;
				case Chaos::EParticleType::Kinematic:
					((FKinematicGeometryParticlePhysicsProxy*)(Proxy))->PullFromPhysicsState();
					break;
				case Chaos::EParticleType::Static:
					((FGeometryParticlePhysicsProxy*)(Proxy))->PullFromPhysicsState();
					break;
				case Chaos::EParticleType::GeometryCollection:
					ActiveGC.AddUnique((FGeometryCollectionPhysicsProxy*)(Proxy));
					break;
				case Chaos::EParticleType::Clustered:
					ActiveGC.AddUnique((FGeometryCollectionPhysicsProxy*)(Proxy));
					break;
				default:
					check(false);
				}
			}
		}

		for (auto* GCProxy : ActiveGC)
		{
			GCProxy->PullFromPhysicsState();
		}
	}


	void FPBDRigidsSolver::PostTickDebugDraw() const
	{
#if CHAOS_DEBUG_DRAW
		if (ChaosSolverDrawCollisions == 1) {
			DebugDraw::DrawCollisions(TRigidTransform<float, 3>(), GetEvolution()->GetCollisionConstraints(), 1.f);
		}
#endif
	}


	void FPBDRigidsSolver::UpdateMaterial(Chaos::FMaterialHandle InHandle, const Chaos::FChaosPhysicsMaterial& InNewData)
	{
		*SimMaterials.Get(InHandle.InnerHandle) = InNewData;
	}

	void FPBDRigidsSolver::CreateMaterial(Chaos::FMaterialHandle InHandle, const Chaos::FChaosPhysicsMaterial& InNewData)
	{
		ensure(SimMaterials.Create(InNewData) == InHandle.InnerHandle);
	}

	void FPBDRigidsSolver::DestroyMaterial(Chaos::FMaterialHandle InHandle)
	{
		SimMaterials.Destroy(InHandle.InnerHandle);
	}

	void FPBDRigidsSolver::UpdateMaterialMask(Chaos::FMaterialMaskHandle InHandle, const Chaos::FChaosPhysicsMaterialMask& InNewData)
	{
		*SimMaterialMasks.Get(InHandle.InnerHandle) = InNewData;
	}

	void FPBDRigidsSolver::CreateMaterialMask(Chaos::FMaterialMaskHandle InHandle, const Chaos::FChaosPhysicsMaterialMask& InNewData)
	{
		ensure(SimMaterialMasks.Create(InNewData) == InHandle.InnerHandle);
	}

	void FPBDRigidsSolver::DestroyMaterialMask(Chaos::FMaterialMaskHandle InHandle)
	{
		SimMaterialMasks.Destroy(InHandle.InnerHandle);
	}

	void FPBDRigidsSolver::SyncQueryMaterials()
	{
		TSolverQueryMaterialScope<ELockType::Write> Scope(this);
		QueryMaterials = SimMaterials;
		QueryMaterialMasks = SimMaterialMasks;
	}

}; // namespace Chaos

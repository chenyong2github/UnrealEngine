// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#if INCLUDE_CHAOS

#include "PBDRigidsSolver.h"

#include "Async/AsyncWork.h"
#include "Chaos/ChaosArchive.h"
#include "Chaos/Framework/SingleParticlePhysicsProxy.h"
#include "Chaos/PBDCollisionConstraintUtil.h"
#include "Chaos/Utilities.h"
#include "ChaosStats.h"
#include "ChaosSolversModule.h"
#include "HAL/FileManager.h"
#include "Misc/ScopeLock.h"
#include "PhysicsProxy/SkeletalMeshPhysicsProxy.h"
#include "PhysicsProxy/StaticMeshPhysicsProxy.h"
#include "PhysicsProxy/GeometryCollectionPhysicsProxy.h"
#include "PhysicsProxy/FieldSystemPhysicsProxy.h"
#include "EventDefaults.h"

DEFINE_LOG_CATEGORY_STATIC(LogPBDRigidsSolverSolver, Log, All);

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
			UE_LOG(LogPBDRigidsSolverSolver, Verbose, TEXT("AdvanceOneTimeStepTask::AdvanceOneTimeStepTask()"));
		}

		void DoWork()
		{
			UE_LOG(LogPBDRigidsSolverSolver, Verbose, TEXT("AdvanceOneTimeStepTask::DoWork()"));

			{
				//SCOPE_CYCLE_COUNTER(STAT_UpdateParams);
				//MSolver->ParameterUpdateCallback(MSolver->GetSolverTime());
			}

			{
				//SCOPE_CYCLE_COUNTER(STAT_BeginFrame);
				//MSolver->StartFrameCallback(MDeltaTime, MSolver->GetSolverTime());
			}

			{
				SCOPE_CYCLE_COUNTER(STAT_EvolutionAndKinematicUpdate);
				while (MDeltaTime > MSolver->GetMaxDeltaTime())
				{
					//MSolver->ForceUpdateCallback(MSolver->GetSolverTime());
					//MSolver->GetEvolution()->ReconcileIslands();
					//MSolver->KinematicUpdateCallback(MSolver->GetMaxDeltaTime(), MSolver->GetSolverTime());
					MSolver->GetEvolution()->AdvanceOneTimeStep(MSolver->GetMaxDeltaTime());
					MDeltaTime -= MSolver->GetMaxDeltaTime();
				}
				//MSolver->ForceUpdateCallback(MSolver->GetSolverTime());
				//MSolver->GetEvolution()->ReconcileIslands();
				//MSolver->KinematicUpdateCallback(MDeltaTime, MSolver->GetSolverTime());
				MSolver->GetEvolution()->AdvanceOneTimeStep(MDeltaTime);
			}

			{
				SCOPE_CYCLE_COUNTER(STAT_EventDataGathering);
				MSolver->GetEventManager()->FillProducerData(MSolver);
				MSolver->GetEventManager()->FlipBuffersIfRequired();
			}

			{
				//SCOPE_CYCLE_COUNTER(STAT_EndFrame);
				//MSolver->EndFrameCallback(MDeltaTime);
			}

			MSolver->GetSolverTime() += MDeltaTime;
			MSolver->GetCurrentFrame()++;
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




	FPBDRigidsSolver::FPBDRigidsSolver(const EMultiBufferMode BufferingModeIn)
		: CurrentFrame(0)
		, MTime(0.0)
		, MLastDt(0.0)
		, MMaxDeltaTime(0.0)
		, TimeStepMultiplier(1.0)
		, bEnabled(false)
		, bHasFloor(true)
		, bIsFloorAnalytic(false)
		, FloorHeight(0.f)
		, MEvolution(new FPBDRigidsEvolution(Particles))
		, MEventManager(new FEventManager(BufferingModeIn))
		, MSolverEventFilters(new FSolverEventFilters())
		, BufferMode(BufferingModeIn)
		, MCurrentLock(new FCriticalSection())
	{
		UE_LOG(LogPBDRigidsSolverSolver, Verbose, TEXT("PBDRigidsSolver::PBDRigidsSolver()"));
		Reset();
	}

	void FPBDRigidsSolver::RegisterObject(TGeometryParticle<float, 3>* GTParticle)
	{
		UE_LOG(LogPBDRigidsSolverSolver, Verbose, TEXT("FPBDRigidsSolver::RegisterObject()"));

		// Make sure this particle doesn't already have a proxy
		checkSlow(GTParticle->Proxy == nullptr);

		// Grab the particle's type
		const EParticleType InParticleType = GTParticle->ObjectType();

		IPhysicsProxyBase* ProxyBase = nullptr;
		// NOTE: Do we really need this list of proxies if we can just
		// access them through the GTParticles list?
		
		Chaos::FParticleData* ProxyData;

		// Make a physics proxy, giving it our particle and particle handle
		if (InParticleType == EParticleType::Dynamic)
		{
			auto Proxy = new FRigidParticlePhysicsProxy(GTParticle->AsDynamic(), nullptr);
			RigidParticlePhysicsProxies.Add((FRigidParticlePhysicsProxy*)Proxy);
			ProxyData = Proxy->NewData();
			ProxyBase = Proxy;
		}
		else if (InParticleType == EParticleType::Kinematic)
		{
			auto Proxy = new FKinematicGeometryParticlePhysicsProxy(GTParticle->AsKinematic(), nullptr);
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
			UE_LOG(LogPBDRigidsSolverSolver, Verbose, TEXT("FPBDRigidsSolver::RegisterObject() ~ Dequeue"));

			// Create a handle for the new particle
			TGeometryParticleHandle<float, 3>* Handle;

			// Insert the particle ptr into the auxiliary array
			//
			// TODO: Is this array even necessary?
			//       Proxy should be able to map back to Particle.
			//

			if (InParticleType == EParticleType::Dynamic)
			{
				Handle = Solver->Particles.CreateDynamicParticles(1)[0];
				auto Proxy = static_cast<FRigidParticlePhysicsProxy*>(ProxyBase);
				Proxy->SetHandle(Handle->AsDynamic());
				Proxy->PushToPhysicsState(ProxyData);
			}
			else if (InParticleType == EParticleType::Kinematic)
			{
				Handle = Solver->Particles.CreateKinematicParticles(1)[0];
				auto Proxy = static_cast<FKinematicGeometryParticlePhysicsProxy*>(ProxyBase);
				Proxy->SetHandle(Handle->AsKinematic());
				Proxy->PushToPhysicsState(ProxyData);
			}
			else // Assume it's a static (geometry) if it's not dynamic or kinematic
			{
				Handle = Solver->Particles.CreateStaticParticles(1)[0];

				auto Proxy = static_cast<FGeometryParticlePhysicsProxy*>(ProxyBase);
				Proxy->SetHandle(Handle);
				Proxy->PushToPhysicsState(ProxyData);
			}

			Handle->GTGeometryParticle() = GTParticle;

			delete ProxyData;
		});
	}

	void FPBDRigidsSolver::UnregisterObject(TGeometryParticle<float, 3>* GTParticle)
	{
		UE_LOG(LogPBDRigidsSolverSolver, Verbose, TEXT("FPBDRigidsSolver::UnregisterObject()"));

		// Get the proxy associated with this particle
		IPhysicsProxyBase* InProxy = GTParticle->Proxy;
		check(InProxy);

		// Grab the particle's type
		const EParticleType InParticleType = GTParticle->ObjectType();

		// Null out the particle's proxy pointer
		GTParticle->Proxy = nullptr;

		// Remove the proxy from the GT proxy map
		if (InParticleType == EParticleType::Dynamic)
		{
			RigidParticlePhysicsProxies.Remove((FRigidParticlePhysicsProxy*)InProxy);
		}
		else if (InParticleType == EParticleType::Kinematic)
		{
			KinematicGeometryParticlePhysicsProxies.Remove((FKinematicGeometryParticlePhysicsProxy*)InProxy);
		}
		else
		{
			GeometryParticlePhysicsProxies.Remove((FGeometryParticlePhysicsProxy*)InProxy);
		}

		// Enqueue a command to remove the particle and delete the proxy
		FChaosSolversModule::GetModule()->GetDispatcher()->EnqueueCommandImmediate(this, [InProxy, InParticleType](FPBDRigidsSolver* Solver)
		{
			UE_LOG(LogPBDRigidsSolverSolver, Verbose, TEXT("FPBDRigidsSolver::UnregisterObject() ~ Dequeue"));

			// Get the physics thread-handle from the proxy, and then delete the proxy.
			//
			// NOTE: We have to delete the proxy from its derived version, because the
			// base destructor is protected. This makes everything just a bit uglier,
			// maybe that extra safety is not needed if we continue to contain all
			// references to proxy instances entirely in Chaos?
			TGeometryParticleHandle<float, 3>* Handle;
			if (InParticleType == Chaos::EParticleType::Dynamic)
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

			// Use the handle to destroy the particle data
			Solver->Particles.DestroyParticle(Handle);
		});
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

		UE_LOG(LogPBDRigidsSolverSolver, Verbose, TEXT("PBDRigidsSolver::Reset()"));

		MTime = 0;
		MLastDt = 0.0f;
		bEnabled = false;
		CurrentFrame = 0;
		MMaxDeltaTime = 1;
		TimeStepMultiplier = 1;
		MEvolution = TUniquePtr<FPBDRigidsEvolution>(new FPBDRigidsEvolution(Particles));

		FEventDefaults::RegisterSystemEvents(*GetEventManager());
	}

	void FPBDRigidsSolver::ChangeBufferMode(Chaos::EMultiBufferMode InBufferMode)
	{
		// This seems unused inside the solver? #BH
		BufferMode = InBufferMode;
	}

	void FPBDRigidsSolver::AdvanceSolverBy(float DeltaTime)
	{
		UE_LOG(LogPBDRigidsSolverSolver, Verbose, TEXT("PBDRigidsSolver::Tick(%3.5f)"), DeltaTime);
		if (bEnabled)
		{
			MLastDt = DeltaTime;

			int32 NumTimeSteps = (int32)(1.f*TimeStepMultiplier);
			float dt = FMath::Min(DeltaTime, float(5.f / 30.f)) / (float)NumTimeSteps;
			for (int i = 0; i < NumTimeSteps; i++)
			{
				AdvanceOneTimeStepTask(this, DeltaTime).DoWork();
			}
		}

	}

	void FPBDRigidsSolver::UpdatePhysicsThreadStructures()
	{
		//ensure(IsInPhyscisThread<BufferMode>());

		if (FChaosSolversModule* ChaosModule = FModuleManager::Get().GetModulePtr<FChaosSolversModule>("ChaosSolvers"))
		{
			// #todo(benn.gallagher) : why is the dispatcher failing here...
			if (Chaos::IDispatcher* Dispatcher = ChaosModule->GetDispatcher())
			{
				this->ForEachPhysicsProxyParallel([Dispatcher](auto* Proxy)
				{
					if (Chaos::FParticleData* ProxyData = Proxy->NewData())
					{
						Dispatcher->EnqueueCommandImmediate([Proxy , ProxyData](Chaos::FPersistentPhysicsTask* PhysThread)
						{
							Proxy->PushToPhysicsState(ProxyData);
							delete ProxyData;
						});
						//Proxy->Particle()->ClearDirtyFlags()
					}
				});
			}
			else // no threading
			{
				this->ForEachPhysicsProxy([](auto* Proxy)
				{
					if (Chaos::FParticleData* ProxyData = Proxy->NewData())
					{
						Proxy->PushToPhysicsState(ProxyData);
						delete ProxyData;
						//Proxy->Particle()->ClearDirtyFlags()
					}
				});
			}
		}
	}

	void FPBDRigidsSolver::UpdateGameThreadStructures()
	{
		//ensure(IsInGameThread());

		// on game thread
		this->ForEachPhysicsProxy([](auto* Object)
		{
			Object->PullFromPhysicsState();
		});

	}
	
	void FPBDRigidsSolver::BufferPhysicsResults()
	{
		//ensure(IsInGameThread());

		// on game thread
		this->ForEachPhysicsProxy([](auto* Object)
		{
			Object->BufferPhysicsResults();
		});

	}


		
	void FPBDRigidsSolver::FlipBuffers()
	{
		//ensure(IsInGameThread());

		// on game thread
		this->ForEachPhysicsProxy([](auto* Object)
		{
			Object->FlipBuffer();
		});

	}

	void FPBDRigidsSolver::SyncEvents_GameThread()
	{
		GetEventManager()->DispatchEvents();
	}

}; // namespace Chaos


#endif

// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Defines.h"
#include "Chaos/Framework/MultiBufferResource.h"
#include "Chaos/Framework/PhysicsProxy.h"
#include "Chaos/Framework/PhysicsSolverBase.h"
#include "Chaos/PBDRigidParticles.h"
#include "Chaos/PBDRigidsEvolutionGBF.h"
#include "Chaos/PBDCollisionConstraints.h"
#include "Chaos/PBDRigidDynamicSpringConstraints.h"
#include "Chaos/PBDPositionConstraints.h"
#include "Chaos/PBDJointConstraints.h"
#include "Chaos/PBDConstraintRule.h"
#include "Chaos/PerParticleGravity.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/Transform.h"
#include "CoreMinimal.h"
#include "Containers/Queue.h"
#include "EventManager.h"
#include "Framework/Dispatcher.h"
#include "Field/FieldSystem.h"
#include "PBDRigidActiveParticlesBuffer.h"
#include "PhysicsProxy/SingleParticlePhysicsProxyFwd.h"
#include "SolverEventFilters.h"

class FPhysicsSolverAdvanceTask;
class FPhysInterface_Chaos;

class FSkeletalMeshPhysicsProxy;
class FStaticMeshPhysicsProxy;
class FGeometryCollectionPhysicsProxy;
class FFieldSystemPhysicsProxy;

/**
*
*/
namespace Chaos
{
	class AdvanceOneTimeStepTask;
	class FPersistentPhysicsTask;
	class FChaosArchive;
	class FPBDRigidsSolver;

	enum class ELockType : uint8
	{
		Read,
		Write
	};

	template<ELockType LockType>
	struct TSolverQueryMaterialScope
	{
		TSolverQueryMaterialScope() = delete;
	};

	/**
	*
	*/
	class CHAOSSOLVERS_API FPBDRigidsSolver : public FPhysicsSolverBase
	{

		FPBDRigidsSolver(const EMultiBufferMode BufferingModeIn);

	public:
		typedef FPhysicsSolverBase Super;

		friend class FPersistentPhysicsTask;
		friend class ::FPhysicsSolverAdvanceTask;
		friend class ::FChaosSolversModule;

		template<EThreadingMode Mode>
		friend class FDispatcher;
		friend class FEventDefaults;

		friend class FPhysInterface_Chaos;
		friend class FPhysScene_ChaosInterface;
		friend class FPBDRigidActiveParticlesBuffer;

		void* PhysSceneHack;	//This is a total hack for now to get at the owning scene

		typedef TPBDRigidsSOAs<float, 3> FParticlesType;
		typedef FPBDRigidActiveParticlesBuffer FActiveParticlesBuffer;

		typedef Chaos::TGeometryParticle<float, 3> FParticle;
		typedef Chaos::TGeometryParticleHandle<float, 3> FHandle;
		typedef Chaos::TPBDRigidsEvolutionGBF<float, 3> FPBDRigidsEvolution;
		typedef Chaos::TPBDCollisionConstraints<float, 3> FPBDCollisionConstraints;

		typedef FPBDCollisionConstraints FCollisionConstraints;
		typedef TPBDRigidDynamicSpringConstraints<float, 3> FRigidDynamicSpringConstraints;
		typedef TPBDPositionConstraints<float, 3> FPositionConstraints;

		typedef TPBDConstraintIslandRule<FPBDJointConstraints> FJointConstraintsRule;
		typedef TPBDConstraintIslandRule<FRigidDynamicSpringConstraints> FRigidDynamicSpringConstraintsRule;
		typedef TPBDConstraintIslandRule<FPositionConstraints> FPositionConstraintsRule;

		//
		// Execution API
		//

		void ChangeBufferMode(Chaos::EMultiBufferMode InBufferMode);
		TQueue<TFunction<void(FPBDRigidsSolver*)>, EQueueMode::Mpsc>& GetCommandQueue() { return CommandQueue; }


		//
		//  Object API
		//

		void RegisterObject(Chaos::TGeometryParticle<float, 3>* GTParticle);
		void UnregisterObject(Chaos::TGeometryParticle<float, 3>* GTParticle);

		// TODO: Set up an interface for registering fields and geometry collections
		void RegisterObject(FGeometryCollectionPhysicsProxy* InProxy);
		bool UnregisterObject(FGeometryCollectionPhysicsProxy* InProxy);
		void RegisterObject(FFieldSystemPhysicsProxy* InProxy);
		bool UnregisterObject(FFieldSystemPhysicsProxy* InProxy);

		bool IsSimulating() const;

		template<typename Lambda>
		void ForEachPhysicsProxy(Lambda InCallable)
		{
			for (FGeometryParticlePhysicsProxy* Obj : GeometryParticlePhysicsProxies)
			{
				InCallable(Obj);
			}
			for (FKinematicGeometryParticlePhysicsProxy* Obj : KinematicGeometryParticlePhysicsProxies)
			{
				InCallable(Obj);
			}
			for (FRigidParticlePhysicsProxy* Obj : RigidParticlePhysicsProxies)
			{
				InCallable(Obj);
			}
			for (FSkeletalMeshPhysicsProxy* Obj : SkeletalMeshPhysicsProxies)
			{
				InCallable(Obj);
			}
			for (FStaticMeshPhysicsProxy* Obj : StaticMeshPhysicsProxies)
			{
				InCallable(Obj);
			}
			for (FGeometryCollectionPhysicsProxy* Obj : GeometryCollectionPhysicsProxies)
			{
				InCallable(Obj);
			}
			for (FFieldSystemPhysicsProxy* Obj : FieldSystemPhysicsProxies)
			{
				InCallable(Obj);
			}
		}

		template<typename Lambda>
		void ForEachPhysicsProxyParallel(Lambda InCallable)
		{
			Chaos::PhysicsParallelFor(GeometryParticlePhysicsProxies.Num(), [this, &InCallable](const int32 Index)
			{
				FGeometryParticlePhysicsProxy* Obj = GeometryParticlePhysicsProxies[Index];
				InCallable(Obj);
			});
			Chaos::PhysicsParallelFor(KinematicGeometryParticlePhysicsProxies.Num(), [this, &InCallable](const int32 Index)
			{
				FKinematicGeometryParticlePhysicsProxy* Obj = KinematicGeometryParticlePhysicsProxies[Index];
				InCallable(Obj);
			});
			Chaos::PhysicsParallelFor(RigidParticlePhysicsProxies.Num(), [this, &InCallable](const int32 Index)
			{
				FRigidParticlePhysicsProxy* Obj = RigidParticlePhysicsProxies[Index];
				InCallable(Obj);
			});
			Chaos::PhysicsParallelFor(SkeletalMeshPhysicsProxies.Num(), [this, &InCallable](const int32 Index)
			{
				FSkeletalMeshPhysicsProxy* Obj = SkeletalMeshPhysicsProxies[Index];
				InCallable(Obj);
			});
			Chaos::PhysicsParallelFor(StaticMeshPhysicsProxies.Num(), [this, &InCallable](const int32 Index)
			{
				FStaticMeshPhysicsProxy* Obj = StaticMeshPhysicsProxies[Index];
				InCallable(Obj);
			});
			Chaos::PhysicsParallelFor(GeometryCollectionPhysicsProxies.Num(), [this, &InCallable](const int32 Index)
			{
				FGeometryCollectionPhysicsProxy* Obj = GeometryCollectionPhysicsProxies[Index];
				InCallable(Obj);
			});
			Chaos::PhysicsParallelFor(FieldSystemPhysicsProxies.Num(), [this, &InCallable](const int32 Index)
			{
				FFieldSystemPhysicsProxy* Obj = FieldSystemPhysicsProxies[Index];
				InCallable(Obj);
			});
		}

		int32 GetNumPhysicsProxies() const {
			return GeometryParticlePhysicsProxies.Num() + KinematicGeometryParticlePhysicsProxies.Num() + RigidParticlePhysicsProxies.Num()
				+ SkeletalMeshPhysicsProxies.Num() + StaticMeshPhysicsProxies.Num()
				+ GeometryCollectionPhysicsProxies.Num() + FieldSystemPhysicsProxies.Num() ;
		}

		//
		//  Simulation API
		//

		/**/
		bool Enabled() const { if (bEnabled) return this->IsSimulating(); return false; }
		void SetEnabled(bool bEnabledIn) { bEnabled = bEnabledIn; }
		bool HasActiveParticles() const { return !!GetNumPhysicsProxies(); }
		bool HasPendingCommands() const { return !CommandQueue.IsEmpty(); }
		FActiveParticlesBuffer* GetActiveParticlesBuffer() const { return MActiveParticlesBuffer.Get(); }

		/**/
		void Reset();

		/**/
		void AdvanceSolverBy(float DeltaTime);

		/**/
		void PushPhysicsState(IDispatcher* Dispatcher = nullptr);

		/**/
		void BufferPhysicsResults();

		/**/
		void FlipBuffers();

		/**/
		void UpdateGameThreadStructures();


		/**/
		void SetCurrentFrame(const int32 CurrentFrameIn) { CurrentFrame = CurrentFrameIn; }
		int32& GetCurrentFrame() { return CurrentFrame; }

		/**/
		float& GetSolverTime() { return MTime; }
		const float GetSolverTime() const { return MTime; }

		/**/
		const float GetLastDt() const { return MLastDt; }
		const float GetMaxDeltaTime() const { return MMaxDeltaTime; }

		/**/
		void SetGenerateCollisionData(bool bDoGenerate) { GetEventFilters()->SetGenerateCollisionEvents(bDoGenerate); }
		void SetGenerateBreakingData(bool bDoGenerate)
		{
			GetEventFilters()->SetGenerateBreakingEvents(bDoGenerate);
#if TODO_REIMPLEMENT_RIGID_CLUSTERING
			GetRigidClustering().SetGenerateClusterBreaking(GenerateBreakingEventsEnabled);
#endif
		}
		void SetGenerateTrailingData(bool bDoGenerate) { GetEventFilters()->SetGenerateTrailingEvents(bDoGenerate); }
		void SetCollisionFilterSettings(const FSolverCollisionFilterSettings& InCollisionFilterSettings) { GetEventFilters()->GetCollisionFilter()->UpdateFilterSettings(InCollisionFilterSettings); }
		void SetBreakingFilterSettings(const FSolverBreakingFilterSettings& InBreakingFilterSettings) { GetEventFilters()->GetBreakingFilter()->UpdateFilterSettings(InBreakingFilterSettings); }
		void SetTrailingFilterSettings(const FSolverTrailingFilterSettings& InTrailingFilterSettings) { GetEventFilters()->GetTrailingFilter()->UpdateFilterSettings(InTrailingFilterSettings); }
		void SetHasFloor(bool bHasFloorIn) { bHasFloor = bHasFloorIn; }
		void SetIsFloorAnalytic(bool bIsAnalytic) { bIsFloorAnalytic = bIsAnalytic; }
		void SetFloorHeight(float Height) { FloorHeight = Height; }

		/**/
		FPBDRigidsEvolution* GetEvolution() { return MEvolution.Get(); }
		FPBDRigidsEvolution* GetEvolution() const { return MEvolution.Get(); }

		FParticlesType& GetParticles() { return Particles; }
		const FParticlesType& GetParticles() const { return Particles; }

		/**/
		FEventManager* GetEventManager() { return MEventManager.Get(); }

		/**/
		FSolverEventFilters* GetEventFilters() { return MSolverEventFilters.Get(); }
		FSolverEventFilters* GetEventFilters() const { return MSolverEventFilters.Get(); }

		/**/
		void SyncEvents_GameThread();

		/**/
		void PostTickDebugDraw() const;

		TArray<FFieldSystemPhysicsProxy*>& GetFieldSystemPhysicsProxies()
		{
			return FieldSystemPhysicsProxies;
		}

		/** Events hooked up to the Chaos material manager */
		void UpdateMaterial(Chaos::FMaterialHandle InHandle, const Chaos::FChaosPhysicsMaterial& InNewData);
		void CreateMaterial(Chaos::FMaterialHandle InHandle, const Chaos::FChaosPhysicsMaterial& InNewData);
		void DestroyMaterial(Chaos::FMaterialHandle InHandle);

		/** Access to the intenal material mirrors */
		const THandleArray<FChaosPhysicsMaterial>& GetQueryMaterials() const { return QueryMaterials; }
		const THandleArray<FChaosPhysicsMaterial>& GetSimMaterials() const { return SimMaterials; }

		/** Copy the simulation material list to the query material list, to be done when the SQ commits an update */
		void SyncQueryMaterials();

	private:

		template<typename ParticleType>
		void FlipBuffer(Chaos::TGeometryParticleHandle<float, 3>* Handle)
		{
			((ParticleType*)(Handle->GTGeometryParticle()->Proxy))->FlipBuffer();
		}

		template<typename ParticleType>
		void PullFromPhysicsState(Chaos::TGeometryParticleHandle<float, 3>* Handle)
		{
			((ParticleType*)(Handle->GTGeometryParticle()->Proxy))->PullFromPhysicsState();
		}

		template<typename ParticleType>
		void BufferPhysicsResults(Chaos::TGeometryParticleHandle<float, 3>* Handle)
		{
			((ParticleType*)(Handle->GTGeometryParticle()->Proxy))->BufferPhysicsResults();
		}

		//
		// Solver Data
		//
		int32 CurrentFrame;
		float MTime;
		float MLastDt;
		float MMaxDeltaTime;
		float TimeStepMultiplier;

		bool bEnabled;
		bool bHasFloor;
		bool bIsFloorAnalytic;
		float FloorHeight;

		FParticlesType Particles;
		TUniquePtr<FPBDRigidsEvolution> MEvolution;
		TUniquePtr<FEventManager> MEventManager;
		TUniquePtr<FSolverEventFilters> MSolverEventFilters;
		TUniquePtr<FActiveParticlesBuffer> MActiveParticlesBuffer;

		//
		// Commands
		//
		TQueue<TFunction<void(FPBDRigidsSolver*)>, EQueueMode::Mpsc> CommandQueue;

		//
		// Proxies
		//
		TSharedPtr<FCriticalSection> MCurrentLock;
		TArray< FGeometryParticlePhysicsProxy* > GeometryParticlePhysicsProxies;
		TArray< FKinematicGeometryParticlePhysicsProxy* > KinematicGeometryParticlePhysicsProxies;
		TArray< FRigidParticlePhysicsProxy* > RigidParticlePhysicsProxies;
		TArray< FSkeletalMeshPhysicsProxy* > SkeletalMeshPhysicsProxies; // dep
		TArray< FStaticMeshPhysicsProxy* > StaticMeshPhysicsProxies; // dep
		TArray< FGeometryCollectionPhysicsProxy* > GeometryCollectionPhysicsProxies;
		TArray< FFieldSystemPhysicsProxy* > FieldSystemPhysicsProxies;

		// Physics material mirrors for the solver. These should generally stay in sync with the global material list from
		// the game thread. This data is read only in the solver as we should never need to update it here. External threads can
		// Enqueue commands to change parameters.
		//
		// There are two copies here to enable SQ to lock only the solvers that it needs to handle the material access during a query
		// instead of having to lock the entire physics state of the runtime.
		FRWLock QueryMaterialLock;
		THandleArray<FChaosPhysicsMaterial> QueryMaterials;
		THandleArray<FChaosPhysicsMaterial> SimMaterials;
				
		template<ELockType>
		friend struct TSolverQueryMaterialScope;
	};

	template<>
	struct TSolverQueryMaterialScope<ELockType::Read>
	{
		TSolverQueryMaterialScope() = delete;


		explicit TSolverQueryMaterialScope(FPBDRigidsSolver* InSolver)
			: Solver(InSolver)
		{
			check(Solver);
			Solver->QueryMaterialLock.ReadLock();
		}

		~TSolverQueryMaterialScope()
		{
			Solver->QueryMaterialLock.ReadUnlock();
		}

	private:
		FPBDRigidsSolver* Solver;
	};

	template<>
	struct TSolverQueryMaterialScope<ELockType::Write>
	{
		TSolverQueryMaterialScope() = delete;

		explicit TSolverQueryMaterialScope(FPBDRigidsSolver* InSolver)
			: Solver(InSolver)
		{
			check(Solver);
			Solver->QueryMaterialLock.WriteLock();
		}

		~TSolverQueryMaterialScope()
		{
			Solver->QueryMaterialLock.WriteUnlock();
		}

	private:
		FPBDRigidsSolver* Solver;
	};

}; // namespace Chaos

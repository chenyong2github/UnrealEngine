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
#include "Chaos/PBDSuspensionConstraints.h"
#include "Chaos/PBDJointConstraints.h"
#include "Chaos/PBDConstraintRule.h"
#include "Chaos/PerParticleGravity.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/Transform.h"
#include "Chaos/Framework/PhysicsProxy.h"
#include "CoreMinimal.h"
#include "Containers/Queue.h"
#include "EventManager.h"
#include "Field/FieldSystem.h"
#include "PBDRigidActiveParticlesBuffer.h"
#include "PhysicsProxy/SingleParticlePhysicsProxyFwd.h"
#include "PhysicsProxy/JointConstraintProxy.h"
#include "PhysicsProxy/SuspensionConstraintProxy.h"
#include "SolverEventFilters.h"
#include "Chaos/EvolutionTraits.h"
#include "Chaos/PBDRigidsEvolutionFwd.h"
#include "ChaosSolversModule.h"

class FPhysInterface_Chaos;
struct FChaosSolverConfiguration;
class FSkeletalMeshPhysicsProxy;
class FStaticMeshPhysicsProxy;
class FPerSolverFieldSystem;

#define PBDRIGID_PREALLOC_COUNT 1024
#define KINEMATIC_GEOM_PREALLOC_COUNT 100
#define GEOMETRY_PREALLOC_COUNT 100

extern int32 ChaosSolverParticlePoolNumFrameUntilShrink;

namespace ChaosTest
{
	template <typename TSolver>
	void AdvanceSolverNoPushHelper(TSolver* Solver,float Dt);
}

/**
*
*/
namespace Chaos
{
	class FPersistentPhysicsTask;
	class FChaosArchive;
	class FRewindData;

	template <typename T,typename R,int d>
	class ISpatialAccelerationCollection;

	template <typename T,int d>
	class TAccelerationStructureHandle;

	enum class ELockType : uint8
	{
		Read,
		Write
	};

	template<ELockType LockType>
	struct TSolverSimMaterialScope
	{
		TSolverSimMaterialScope() = delete;
	};

	/**
	*
	*/
	template <typename Traits>
	class TPBDRigidsSolver : public FPhysicsSolverBase
	{

		TPBDRigidsSolver(const EMultiBufferMode BufferingModeIn, UObject* InOwner);

	public:

		typedef FPhysicsSolverBase Super;

		friend class FPersistentPhysicsTask;
		friend class ::FChaosSolversModule;

		template<EThreadingMode Mode>
		friend class FDispatcher;
		
		template <typename Traits2>
		friend class TEventDefaults;

		friend class FPhysInterface_Chaos;
		friend class FPhysScene_ChaosInterface;
		friend class FPBDRigidDirtyParticlesBuffer;

		void* PhysSceneHack;	//This is a total hack for now to get at the owning scene

		typedef TPBDRigidsSOAs<float, 3> FParticlesType;
		typedef FPBDRigidDirtyParticlesBuffer FDirtyParticlesBuffer;

		typedef Chaos::TGeometryParticle<float, 3> FParticle;
		typedef Chaos::TGeometryParticleHandle<float, 3> FHandle;
		typedef Chaos::TPBDRigidsEvolutionGBF<Traits> FPBDRigidsEvolution;

		typedef TPBDRigidDynamicSpringConstraints<float, 3> FRigidDynamicSpringConstraints;
		typedef TPBDPositionConstraints<float, 3> FPositionConstraints;

		typedef TPBDConstraintIslandRule<FPBDJointConstraints> FJointConstraintsRule;
		typedef TPBDConstraintIslandRule<FRigidDynamicSpringConstraints> FRigidDynamicSpringConstraintsRule;
		typedef TPBDConstraintIslandRule<FPositionConstraints> FPositionConstraintsRule;
		typedef TPBDConstraintIslandRule<FPBDSuspensionConstraints> FSuspensionConstraintsRule;

		using FJointConstraints = FPBDJointConstraints;
		using FJointConstraintRule = TPBDConstraintIslandRule<FJointConstraints>;
		//
		// Execution API
		//

		void ChangeBufferMode(Chaos::EMultiBufferMode InBufferMode);

		//
		//  Object API
		//

		void RegisterObject(Chaos::TGeometryParticle<float, 3>* GTParticle);
		void UnregisterObject(Chaos::TGeometryParticle<float, 3>* GTParticle);

		void RegisterObject(FGeometryCollectionPhysicsProxy* InProxy);
		bool UnregisterObject(FGeometryCollectionPhysicsProxy* InProxy);

		void RegisterObject(Chaos::FJointConstraint* GTConstraint);
		bool UnregisterObject(Chaos::FJointConstraint* GTConstraint);

		void RegisterObject(Chaos::FSuspensionConstraint* GTConstraint);
		bool UnregisterObject(Chaos::FSuspensionConstraint* GTConstraint);

		bool IsSimulating() const;

		void EnableRewindCapture(int32 NumFrames, bool InUseCollisionResimCache);
		FRewindData* GetRewindData()
		{
			if(Traits::IsRewindable())
			{
				return MRewindData.Get();
			}
			else
			{
				return nullptr;
			}
		}

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
			for (FJointConstraintPhysicsProxy* Obj : JointConstraintPhysicsProxies)
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
			Chaos::PhysicsParallelFor(JointConstraintPhysicsProxies.Num(), [this, &InCallable](const int32 Index)
			{
				FJointConstraintPhysicsProxy* Obj = JointConstraintPhysicsProxies[Index];
				InCallable(Obj);
			});
		}

		int32 GetNumPhysicsProxies() const {
			return GeometryParticlePhysicsProxies.Num() + KinematicGeometryParticlePhysicsProxies.Num() + RigidParticlePhysicsProxies.Num()
				+ SkeletalMeshPhysicsProxies.Num() + StaticMeshPhysicsProxies.Num()
				+ GeometryCollectionPhysicsProxies.Num()
				+ JointConstraintPhysicsProxies.Num();
		}

		//
		//  Simulation API
		//

		/**/
		bool HasActiveParticles() const { return !!GetNumPhysicsProxies(); }
		FDirtyParticlesBuffer* GetDirtyParticlesBuffer() const { return MDirtyParticlesBuffer.Get(); }


		//Make friend with unit test code so we can verify some behavior
		template <typename TSolver>
		friend void ChaosTest::AdvanceSolverNoPushHelper(TSolver* Solver,float Dt);

		/**/
		void Reset();

		/**/
		void StartingSceneSimulation();

		/**/
		void CompleteSceneSimulation();

		/**/
		void UpdateGameThreadStructures();



		/**/
		void SetCurrentFrame(const int32 CurrentFrameIn) { CurrentFrame = CurrentFrameIn; }
		int32& GetCurrentFrame() { return CurrentFrame; }

		/**/
		float& GetSolverTime() { return MTime; }
		const float GetSolverTime() const { return MTime; }

		/**/
		void SetMaxDeltaTime(const float InMaxDeltaTime) { MMaxDeltaTime = InMaxDeltaTime; }
		float GetLastDt() const { return MLastDt; }
		float GetMaxDeltaTime() const { return MMaxDeltaTime; }
		float GetMinDeltaTime() const { return MMinDeltaTime; }
		void SetMaxSubSteps(const int32 InMaxSubSteps) { MMaxSubSteps = InMaxSubSteps; }
		int32 GetMaxSubSteps() const { return MMaxSubSteps; }

		/**/
		void SetIterations(const int32 InNumIterations) { GetEvolution()->SetNumIterations(InNumIterations); }
		void SetPushOutIterations(const int32 InNumIterations) {  GetEvolution()->SetNumPushOutIterations(InNumIterations); }
		void SetCollisionPairIterations(const int32 InNumIterations) { GetEvolution()->GetCollisionConstraints().SetPairIterations(InNumIterations); }
		void SetCollisionPushOutPairIterations(const int32 InNumIterations) { GetEvolution()->GetCollisionConstraints().SetPushOutPairIterations(InNumIterations); }
		void SetJointPairIterations(const int32 InNumIterations) { GetJointConstraints().SetNumPairIterations(InNumIterations); }
		void SetJointPushOutPairIterations(const int32 InNumIterations) {GetJointConstraints().SetNumPushOutPairIterations(InNumIterations); }
		void SetCollisionCullDistance(const FReal InCullDistance) { GetEvolution()->GetCollisionConstraints().SetCullDistance(InCullDistance); GetEvolution()->GetBroadPhase().SetCullDistance(InCullDistance); }
		void SetUseContactGraph(const bool bInUseContactGraph) { GetEvolution()->GetCollisionConstraintsRule().SetUseContactGraph(bInUseContactGraph); }

		/**/
		void SetGenerateCollisionData(bool bDoGenerate) { GetEventFilters()->SetGenerateCollisionEvents(bDoGenerate); }
		void SetGenerateBreakingData(bool bDoGenerate)
		{
			GetEventFilters()->SetGenerateBreakingEvents(bDoGenerate);
			GetEvolution()->GetRigidClustering().SetGenerateClusterBreaking(bDoGenerate);
		}
		void SetGenerateTrailingData(bool bDoGenerate) { GetEventFilters()->SetGenerateTrailingEvents(bDoGenerate); }
		void SetCollisionFilterSettings(const FSolverCollisionFilterSettings& InCollisionFilterSettings) { GetEventFilters()->GetCollisionFilter()->UpdateFilterSettings(InCollisionFilterSettings); }
		void SetBreakingFilterSettings(const FSolverBreakingFilterSettings& InBreakingFilterSettings) { GetEventFilters()->GetBreakingFilter()->UpdateFilterSettings(InBreakingFilterSettings); }
		void SetTrailingFilterSettings(const FSolverTrailingFilterSettings& InTrailingFilterSettings) { GetEventFilters()->GetTrailingFilter()->UpdateFilterSettings(InTrailingFilterSettings); }

		/**/
		FJointConstraints& GetJointConstraints() { return JointConstraints; }
		const FJointConstraints& GetJointConstraints() const { return JointConstraints; }

		FPBDSuspensionConstraints& GetSuspensionConstraints() { return SuspensionConstraints; }
		const FPBDSuspensionConstraints& GetSuspensionConstraints() const { return SuspensionConstraints; }

		/**/
		FPBDRigidsEvolution* GetEvolution() { return MEvolution.Get(); }
		FPBDRigidsEvolution* GetEvolution() const { return MEvolution.Get(); }

		FParticlesType& GetParticles() { return Particles; }
		const FParticlesType& GetParticles() const { return Particles; }

		void AddParticleToProxy(const Chaos::TGeometryParticleHandle<float, 3>* Particle, IPhysicsProxyBase* Proxy)
		{
			if (!MParticleToProxy.Find(Particle))
			{
				MParticleToProxy.Add(Particle, TSet<IPhysicsProxyBase*>());
			}
			MParticleToProxy[Particle].Add(Proxy); 
		}
		
		void RemoveParticleToProxy(const Chaos::TGeometryParticleHandle<float, 3>* Particle)
		{
			MParticleToProxy.Remove(Particle);
		}
		
		const TSet<IPhysicsProxyBase*> * GetProxies(const Chaos::TGeometryParticleHandle<float, 3>* Handle) const
		{
			const TSet<IPhysicsProxyBase*>* PhysicsProxyPtr = MParticleToProxy.Find(Handle);
			return PhysicsProxyPtr ? PhysicsProxyPtr : nullptr;
		}

		/**/
		TEventManager<Traits>* GetEventManager() { return MEventManager.Get(); }

		/**/
		FSolverEventFilters* GetEventFilters() { return MSolverEventFilters.Get(); }
		FSolverEventFilters* GetEventFilters() const { return MSolverEventFilters.Get(); }

		/**/
		void SyncEvents_GameThread();

		/**/
		void PostTickDebugDraw(FReal Dt) const;

		// Visual debugger (VDB) push methods
		void PostEvolutionVDBPush() const;

		TArray<FGeometryCollectionPhysicsProxy*>& GetGeometryCollectionPhysicsProxies()
		{
			return GeometryCollectionPhysicsProxies;
		}

		TArray<FJointConstraintPhysicsProxy*>& GetJointConstraintPhysicsProxy()
		{
			return JointConstraintPhysicsProxies;
		}

		/** Events hooked up to the Chaos material manager */
		void UpdateMaterial(Chaos::FMaterialHandle InHandle, const Chaos::FChaosPhysicsMaterial& InNewData);
		void CreateMaterial(Chaos::FMaterialHandle InHandle, const Chaos::FChaosPhysicsMaterial& InNewData);
		void DestroyMaterial(Chaos::FMaterialHandle InHandle);
		void UpdateMaterialMask(Chaos::FMaterialMaskHandle InHandle, const Chaos::FChaosPhysicsMaterialMask& InNewData);
		void CreateMaterialMask(Chaos::FMaterialMaskHandle InHandle, const Chaos::FChaosPhysicsMaterialMask& InNewData);
		void DestroyMaterialMask(Chaos::FMaterialMaskHandle InHandle);

		/** Access to the internal material mirrors */
		const THandleArray<FChaosPhysicsMaterial>& GetQueryMaterials_External() const { return QueryMaterials_External; }
		const THandleArray<FChaosPhysicsMaterialMask>& GetQueryMaterialMasks_External() const { return QueryMaterialMasks_External; }
		const THandleArray<FChaosPhysicsMaterial>& GetSimMaterials() const { return SimMaterials; }
		const THandleArray<FChaosPhysicsMaterialMask>& GetSimMaterialMasks() const { return SimMaterialMasks; }

		/** Copy the simulation material list to the query material list, to be done when the SQ commits an update */
		void SyncQueryMaterials_External();

		void FinalizeRewindData(const TParticleView<TPBDRigidParticles<FReal,3>>& DirtyParticles);
		bool RewindUsesCollisionResimCache() const { return bUseCollisionResimCache; }

		FPerSolverFieldSystem& GetPerSolverField() { return *PerSolverField; }
		const FPerSolverFieldSystem& GetPerSolverField() const { return *PerSolverField; }

		void UpdateExternalAccelerationStructure_External(ISpatialAccelerationCollection<TAccelerationStructureHandle<FReal,3>,FReal,3>*& ExternalStructure);

		/** Apply a solver configuration to this solver, set externally by the owner of a solver (see UPhysicsSettings for world solver settings) */
		void ApplyConfig(const FChaosSolverConfiguration& InConfig);

		virtual bool AreAnyTasksPending() const override
		{
			return GetEvolution()->AreAnyTasksPending();
		}

		void BeginDestroy();

	private:

		/**/
		void BufferPhysicsResults();
	
		/**/
		virtual void AdvanceSolverBy(const FReal DeltaTime) override;
		virtual void PushPhysicsState(const FReal ExternalDt, const int32 NumSteps, const int32 NumExternalSteps) override;
		virtual void SetExternalTimestampConsumed_Internal(const int32 Timestamp) override;

		//
		// Solver Data
		//
		int32 CurrentFrame;
		float MTime;
		float MLastDt;
		float MMaxDeltaTime;
		float MMinDeltaTime;
		int32 MMaxSubSteps;
		bool bHasFloor;
		bool bIsFloorAnalytic;
		float FloorHeight;

		FParticlesType Particles;
		TUniquePtr<FPBDRigidsEvolution> MEvolution;
		TUniquePtr<TEventManager<Traits>> MEventManager;
		TUniquePtr<FSolverEventFilters> MSolverEventFilters;
		TUniquePtr<FDirtyParticlesBuffer> MDirtyParticlesBuffer;
		TMap<const Chaos::TGeometryParticleHandle<float, 3>*, TSet<IPhysicsProxyBase*> > MParticleToProxy;
		TUniquePtr<FRewindData> MRewindData;

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
		TArray< FJointConstraintPhysicsProxy* > JointConstraintPhysicsProxies;
		TArray< FSuspensionConstraintPhysicsProxy* > SuspensionConstraintPhysicsProxies;
		bool bUseCollisionResimCache;

		//
		//  Constraints
		//
		FPBDJointConstraints JointConstraints;
		TPBDConstraintIslandRule<FPBDJointConstraints> JointConstraintRule;

		FPBDSuspensionConstraints SuspensionConstraints;
		TPBDConstraintIslandRule<FPBDSuspensionConstraints> SuspensionConstraintRule;

		TUniquePtr<FPerSolverFieldSystem> PerSolverField;


		// Physics material mirrors for the solver. These should generally stay in sync with the global material list from
		// the game thread. This data is read only in the solver as we should never need to update it here. External threads can
		// Enqueue commands to change parameters.
		//
		// There are two copies here to enable SQ to lock only the solvers that it needs to handle the material access during a query
		// instead of having to lock the entire physics state of the runtime.
		
		THandleArray<FChaosPhysicsMaterial> QueryMaterials_External;
		THandleArray<FChaosPhysicsMaterialMask> QueryMaterialMasks_External;
		THandleArray<FChaosPhysicsMaterial> SimMaterials;
		THandleArray<FChaosPhysicsMaterialMask> SimMaterialMasks;

		void ProcessSinglePushedData_Internal(FPushPhysicsData& PushData);
		virtual void ProcessPushedData_Internal(FPushPhysicsData& PushData) override;
	};

	template<>
	struct TSolverSimMaterialScope<ELockType::Read>
	{
		TSolverSimMaterialScope() = delete;


		explicit TSolverSimMaterialScope(FPhysicsSolverBase* InSolver)
			: Solver(InSolver)
		{
			check(Solver);
			Solver->SimMaterialLock.ReadLock();
		}

		~TSolverSimMaterialScope()
		{
			Solver->SimMaterialLock.ReadUnlock();
		}

	private:
		FPhysicsSolverBase* Solver;
	};

	template<>
	struct TSolverSimMaterialScope<ELockType::Write>
	{
		TSolverSimMaterialScope() = delete;

		explicit TSolverSimMaterialScope(FPhysicsSolverBase* InSolver)
			: Solver(InSolver)
		{
			check(Solver);
			Solver->SimMaterialLock.WriteLock();
		}

		~TSolverSimMaterialScope()
		{
			Solver->SimMaterialLock.WriteUnlock();
		}

	private:
		FPhysicsSolverBase* Solver;
	};

#define EVOLUTION_TRAIT(Trait) extern template class CHAOS_TEMPLATE_API TPBDRigidsSolver<Trait>;
#include "Chaos/EvolutionTraits.inl"
#undef EVOLUTION_TRAIT

}; // namespace Chaos

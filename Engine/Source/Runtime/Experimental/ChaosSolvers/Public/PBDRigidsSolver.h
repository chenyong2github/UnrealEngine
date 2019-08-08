// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#if INCLUDE_CHAOS
#pragma once

#include "Chaos/ArrayCollectionArray.h"
#include "Chaos/Defines.h"
#include "Chaos/Framework/PhysicsProxy.h"
#include "Chaos/Framework/MultiBufferResource.h"
#include "Chaos/Framework/SingleParticlePhysicsProxy.h"
#include "Chaos/PBDRigidParticles.h"
#include "Chaos/PBDRigidsEvolutionGBF.h"
#include "Chaos/PBDCollisionConstraint.h"
#include "Chaos/PBDRigidDynamicSpringConstraints.h"
#include "Chaos/PBDPositionConstraints.h"
#include "Chaos/PBDJointConstraints.h"
#include "Chaos/PBDConstraintRule.h"
#include "Chaos/PerParticleGravity.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/Transform.h"
#include "CoreMinimal.h"
#include "Containers/Queue.h"
#include "Framework/Dispatcher.h"
#include "Field/FieldSystem.h"
#include "EventManager.h"
#include "SolverEventFilters.h"

#ifndef CHAOS_WITH_PAUSABLE_SOLVER
#define CHAOS_WITH_PAUSABLE_SOLVER 1
#endif

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
	class FPhysicsCommand;
	class FChaosArchive;
	/**
	*
	*/
	class CHAOSSOLVERS_API FPBDRigidsSolver
	{

		FPBDRigidsSolver(const EMultiBufferMode BufferingModeIn);

	public:
		friend class FPersistentPhysicsTask;
		friend class ::FPhysicsSolverAdvanceTask;
		friend class ::FChaosSolversModule;

		template<EThreadingMode Mode>
		friend class FDispatcher;
		friend class FEventDefaults;

		friend class FPhysInterface_Chaos;
		friend class FPhysScene_ChaosInterface;

		void* PhysSceneHack;	//This is a total hack for now to get at the owning scene

		typedef TPBDRigidsSOAs<float, 3> FParticlesType;

		typedef Chaos::TGeometryParticle<float, 3> FParticle;
		typedef Chaos::TGeometryParticleHandle<float, 3> FHandle;
		typedef Chaos::TPBDRigidsEvolutionGBF<float, 3> FPBDRigidsEvolution;
		typedef Chaos::TPBDCollisionConstraint<float, 3> FPBDCollisionConstraints;

		typedef FPBDCollisionConstraints FCollisionConstraints;
		typedef TPBDJointConstraints<float, 3> FJointConstraints;
		typedef TPBDRigidDynamicSpringConstraints<float, 3> FRigidDynamicSpringConstraints;
		typedef TPBDPositionConstraints<float, 3> FPositionConstraints;

		typedef TPBDConstraintIslandRule<FJointConstraints, float, 3> FJointConstraintsRule;
		typedef TPBDConstraintIslandRule<FRigidDynamicSpringConstraints, float, 3> FRigidDynamicSpringConstraintsRule;
		typedef TPBDConstraintIslandRule<FPositionConstraints, float, 3> FPositionConstraintsRule;

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
		//void RegisterObject(FGeometryCollectionPhysicsProxy* InProxy);
		//int UnregisterObject(FGeometryCollectionPhysicsProxy* InProxy);
		//void RegisterObject(FFieldSystemPhysicsProxy* InProxy);
		//int UnregisterObject(FFieldSystemPhysicsProxy* InProxy);

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

		bool GetNumPhysicsProxies() const {
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

		/**/
		void Reset();

		/**/
		void AdvanceSolverBy(float DeltaTime);
		void UpdatePhysicsThreadStructures();
		void UpdateGameThreadStructures();
		void BufferPhysicsResults();
		void FlipBuffers();

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

		const FParticlesType& GetParticles() const { return Particles; }

		/**/
		FEventManager* GetEventManager() { return MEventManager.Get(); }

		/**/
		FSolverEventFilters* GetEventFilters() { return MSolverEventFilters.Get(); }
		FSolverEventFilters* GetEventFilters() const { return MSolverEventFilters.Get(); }

		/**/
		void SyncEvents_GameThread();

	private:

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

		//
		// Commands
		//
		TQueue<TFunction<void(FPBDRigidsSolver*)>, EQueueMode::Mpsc> CommandQueue;

		//
		// Proxies
		//
		EMultiBufferMode BufferMode;
		TSharedPtr<FCriticalSection> MCurrentLock;
		TArray< FGeometryParticlePhysicsProxy* > GeometryParticlePhysicsProxies;
		TArray< FKinematicGeometryParticlePhysicsProxy* > KinematicGeometryParticlePhysicsProxies;
		TArray< FRigidParticlePhysicsProxy* > RigidParticlePhysicsProxies;
		TArray< FSkeletalMeshPhysicsProxy* > SkeletalMeshPhysicsProxies; // dep
		TArray< FStaticMeshPhysicsProxy* > StaticMeshPhysicsProxies; // dep
		TArray< FGeometryCollectionPhysicsProxy* > GeometryCollectionPhysicsProxies;
		TArray< FFieldSystemPhysicsProxy* > FieldSystemPhysicsProxies;
				
	};

}; // namespace Chaos
#endif

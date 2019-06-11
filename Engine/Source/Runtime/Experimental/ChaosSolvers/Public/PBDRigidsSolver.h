// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#if INCLUDE_CHAOS
#pragma once

#include "Chaos/ArrayCollectionArray.h"
#include "Chaos/Defines.h"
#include "Chaos/PBDRigidParticles.h"
#include "Chaos/PBDRigidsEvolution.h"
#include "Chaos/PBDCollisionConstraint.h"
#include "Chaos/PBDRigidDynamicSpringConstraints.h"
#include "Chaos/PBDPositionConstraints.h"
#include "Chaos/PBDJointConstraints.h"
#include "Chaos/PBDSpringConstraints.h"
#include "Chaos/PBDConstraintRule.h"
#include "Chaos/PerParticleGravity.h"
#include "Chaos/Transform.h"
#include "CoreMinimal.h"
#include "Containers/Queue.h"
#include "Framework/Dispatcher.h"
#include "Field/FieldSystem.h"
#include "SolverObjects/SolverObject.h"
#include "MultiBufferResource.h"
#include "SolverEventFilters.h"

#ifndef CHAOS_WITH_PAUSABLE_SOLVER
#define CHAOS_WITH_PAUSABLE_SOLVER 1
#endif

class FSkeletalMeshPhysicsObject;
class FStaticMeshPhysicsObject;
class FBodyInstancePhysicsObject;
class FGeometryCollectionPhysicsObject;
class FFieldSystemPhysicsObject;
class FPhysicsSolverAdvanceTask;

#define USE_PGS 0

#if USE_PGS
typedef Chaos::TPBDRigidsEvolutionPGS<float, 3> FPBDRigidsEvolution;
typedef Chaos::TPBDCollisionConstraintPGS<float, 3> FPBDCollisionConstraints;
#else
typedef Chaos::TPBDRigidsEvolutionGBF<float, 3> FPBDRigidsEvolution;
typedef Chaos::TPBDCollisionConstraint<float, 3> FPBDCollisionConstraints;
#endif

class FPhysInterface_Chaos;
namespace Chaos
{
	class FPBDRigidsSolver;
	class AdvanceOneTimeStepTask;
	class FPersistentPhysicsTask;
	class FPhysicsCommand;
};

struct FKinematicProxy
{
	TArray<int32> Ids;
	TArray<FVector> Position;
	TArray<FQuat> Rotation;
	TArray<FVector> NextPosition;
	TArray<FQuat> NextRotation;
};

struct CHAOSSOLVERS_API FSolverObjectStorage
{
	void Reset();

	// Call helper for TSolverObject items, use with a generic lambda of form [](auto* Object){}
	template<typename Lambda>
	void ForEachSolverObject(Lambda InCallable)
	{
		for(TSolverObject<FGeometryCollectionPhysicsObject>* Obj : GeometryCollectionObjects)
		{
			InCallable(Obj);
		}
		
		for(TSolverObject<FSkeletalMeshPhysicsObject>* Obj : SkeletalMeshObjects)
		{
			InCallable(Obj);
		}
		
		for(TSolverObject<FStaticMeshPhysicsObject>* Obj : StaticMeshObjects)
		{
			InCallable(Obj);
		}

		for (TSolverObject<FBodyInstancePhysicsObject>* Obj : BodyInstanceObjects)
		{
			InCallable(Obj);
		}

	}

	template<typename Lambda>
	void ForEachSolverObjectParallel(Lambda InCallable)
	{
		Chaos::PhysicsParallelFor(GeometryCollectionObjects.Num(), [this, &InCallable](const int32 Index)
		{
			TSolverObject<FGeometryCollectionPhysicsObject>* Obj = GeometryCollectionObjects[Index];
			InCallable(Obj);
		});

		Chaos::PhysicsParallelFor(SkeletalMeshObjects.Num(), [this, &InCallable](const int32 Index)
		{
			TSolverObject<FSkeletalMeshPhysicsObject>* Obj = SkeletalMeshObjects[Index];
			InCallable(Obj);
		});

		Chaos::PhysicsParallelFor(StaticMeshObjects.Num(), [this, &InCallable](const int32 Index)
		{
			TSolverObject<FStaticMeshPhysicsObject>* Obj = StaticMeshObjects[Index];
			InCallable(Obj);
		});

		Chaos::PhysicsParallelFor(BodyInstanceObjects.Num(), [this, &InCallable](const int32 Index)
		{
			TSolverObject<FBodyInstancePhysicsObject>* Obj = BodyInstanceObjects[Index];
			InCallable(Obj);
		});
	}

	template<typename Lambda>
	void ForEachFieldSolverObject(Lambda InCallable)
	{
		for(FFieldSystemPhysicsObject* Obj : FieldSystemObjects)
		{
			InCallable(Obj);
		}
	}

	template<typename ObjectType>
	TArray<TSolverObject<ObjectType>*>& GetObjectStorage();

	template<typename ObjectType>
	TArray<ObjectType*>& GetFieldObjectStorage();

	int32 GetNumObjects() const;

	int32 GetNumFieldObjects() const;

	template<typename ObjectType>
	int32 GetNumObjectsOfType() const;

	TArray<TSolverObject<FGeometryCollectionPhysicsObject>*> GeometryCollectionObjects;
	TArray<TSolverObject<FSkeletalMeshPhysicsObject>*> SkeletalMeshObjects;
	TArray<TSolverObject<FStaticMeshPhysicsObject>*> StaticMeshObjects;
	TArray<TSolverObject<FBodyInstancePhysicsObject>*> BodyInstanceObjects;
	TArray<FFieldSystemPhysicsObject*> FieldSystemObjects;
};

template<> TArray<TSolverObject<FGeometryCollectionPhysicsObject>*>& FSolverObjectStorage::GetObjectStorage();
template<> TArray<TSolverObject<FSkeletalMeshPhysicsObject>*>& FSolverObjectStorage::GetObjectStorage();
template<> TArray<TSolverObject<FStaticMeshPhysicsObject>*>& FSolverObjectStorage::GetObjectStorage();
template<> TArray<TSolverObject<FBodyInstancePhysicsObject>*>& FSolverObjectStorage::GetObjectStorage();
template<> TArray<FFieldSystemPhysicsObject*>& FSolverObjectStorage::GetFieldObjectStorage();
template<> int32 FSolverObjectStorage::GetNumObjectsOfType<FGeometryCollectionPhysicsObject>() const;
template<> int32 FSolverObjectStorage::GetNumObjectsOfType<FSkeletalMeshPhysicsObject>() const;
template<> int32 FSolverObjectStorage::GetNumObjectsOfType<FStaticMeshPhysicsObject>() const;
template<> int32 FSolverObjectStorage::GetNumObjectsOfType<FBodyInstancePhysicsObject>() const;
template<> int32 FSolverObjectStorage::GetNumObjectsOfType<FFieldSystemPhysicsObject>() const;

/**
*
*/
namespace Chaos
{
	class FChaosArchive;
	/**
	*
	*/
	class CHAOSSOLVERS_API FPBDRigidsSolver
	{
	public:
		typedef TPBDRigidParticles<float, 3> FParticlesType;

		typedef FPBDCollisionConstraints FCollisionConstraints;
		typedef TPBDJointConstraints<float, 3> FJointConstraints;
		typedef TPBDRigidDynamicSpringConstraints<float, 3> FRigidDynamicSpringConstraints;
		typedef TPBDPositionConstraints<float, 3> FPositionConstraints;

		typedef TPBDConstraintIslandRule<FJointConstraints, float, 3> FJointConstraintsRule;
		typedef TPBDConstraintIslandRule<FRigidDynamicSpringConstraints, float, 3> FRigidDynamicSpringConstraintsRule;
		typedef TPBDConstraintIslandRule<FPositionConstraints, float, 3> FPositionConstraintsRule;

		friend class AdvanceOneTimeStepTask;
		friend class FPersistentPhysicsTask;
		friend class ::FPhysicsSolverAdvanceTask;

		template<EThreadingMode Mode>
		friend class FDispatcher;

		friend class FPhysInterface_Chaos;
		friend class FPhysScene_ChaosInterface;

		static int8 Invalid;
	
		friend struct FAccessor;
		friend class FScopedGetEventsData;

		/* Access to details */
		// This is required for GCC which doesn't allow friends across dlls
		struct CHAOSSOLVERS_API FAccessor
		{
			FAccessor(FPBDRigidsSolver* InSolver) { Solver = InSolver; }

			static FPBDRigidsSolver* CreateSolver(const EMultiBufferMode BufferingModeIn = EMultiBufferMode::Double)
			{
				return new FPBDRigidsSolver(BufferingModeIn);
			}

			TPBDRigidDynamicSpringConstraints<float, 3>& DynamicConstraints() { return Solver->DynamicConstraints; }
			TSet<int32>& DynamicConstraintParticles() { return Solver->DynamicConstraintParticles; }

		private:
			FPBDRigidsSolver* Solver;
		};

		/* General maps reverse lookups */
		struct FTimeResource
		{
			FTimeResource() : TimeCreated(-FLT_MAX) {}
			float TimeCreated;
		};

		// SolverObjectReverseMapping for one frame time stamped with the time for that frame  
		struct FSolverObjectReverseMapping : FTimeResource
		{
			FSolverObjectReverseMapping()
				: SolverObjectReverseMappingArray(TArray<SolverObjectWrapper>())
			{}

			void Reset()
			{
				SolverObjectReverseMappingArray.Reset();
			}

			/** Maps ParticleIndex to SolverObject.  SolverObjectReverseMappingArray[ParticleIndex] = SolverObject */
			TArray<SolverObjectWrapper> SolverObjectReverseMappingArray; 
		};

		// ParticleIndexReverseMapping for one frame time stamped with the time for that frame
		struct FParticleIndexReverseMapping : FTimeResource
		{
			FParticleIndexReverseMapping()
				: ParticleIndexReverseMappingArray(TArray<int32>())
			{}

			void Reset()
			{
				ParticleIndexReverseMappingArray.Reset();
			}

			TArray<int32> ParticleIndexReverseMappingArray; // ParticleIndex -> TransformIndex
		};

		/* Collision */

		typedef TArray<TCollisionData<float, 3>> FCollisionDataArray;

		struct FCollisionData
		{
			float TimeCreated;
			int32 NumCollisions;
			FCollisionDataArray CollisionDataArray;
		};

		// All the collisions for one frame time stamped with the time for that frame  
		struct FAllCollisionData : public FTimeResource
		{
			FAllCollisionData()
				: AllCollisionsArray(FCollisionDataArray())
			{}

			void Reset()
			{
				AllCollisionsArray.Reset();
			}

			FCollisionDataArray AllCollisionsArray;
		};

		// AllCollisionsIndicesBySolverObject for one frame time stamped with the time for that frame
		struct FAllCollisionsIndicesBySolverObject : public FTimeResource
		{
			FAllCollisionsIndicesBySolverObject()
				: AllCollisionsIndicesBySolverObjectMap(TMap<ISolverObjectBase*, TArray<int32>>())
			{}

			void Reset()
			{
				AllCollisionsIndicesBySolverObjectMap.Reset();
			}

			TMap<ISolverObjectBase*, TArray<int32>> AllCollisionsIndicesBySolverObjectMap; // SolverObject -> Indices in AllCollisions array
		};

		// All the collisions with all the maps
		struct FAllCollisionDataMaps
		{
			FAllCollisionDataMaps()
				: bIsDataAndMapsInSync(false)
				, AllCollisionData(nullptr)
				, SolverObjectReverseMapping(nullptr)
				, ParticleIndexReverseMapping(nullptr)
				, AllCollisionsIndicesBySolverObject(nullptr)
			{}

			bool IsValid() const 
			{
				return bIsDataAndMapsInSync && AllCollisionData && SolverObjectReverseMapping && ParticleIndexReverseMapping && AllCollisionsIndicesBySolverObject; 
			}

			bool bIsDataAndMapsInSync;
			const FAllCollisionData* AllCollisionData;
			const FSolverObjectReverseMapping* SolverObjectReverseMapping;
			const FParticleIndexReverseMapping* ParticleIndexReverseMapping;
			const FAllCollisionsIndicesBySolverObject* AllCollisionsIndicesBySolverObject;
		};

		/* Breaking */

		typedef TArray<TBreakingData<float, 3>> FBreakingDataArray;
		struct FBreakingData
		{
			float TimeCreated;
			int32 NumBreakings;
			FBreakingDataArray BreakingDataArray;
		};

		// All the breakings for one frame time stamped with the time for that frame  
		struct FAllBreakingData : public FTimeResource
		{
			FAllBreakingData()
				: AllBreakingsArray(FBreakingDataArray())
			{}

			void Reset()
			{
				AllBreakingsArray.Reset();
			}

			FBreakingDataArray AllBreakingsArray;
		};

		// AllBreakingsIndicesBySolverObject for one frame time stamped with the time for that frame
		struct FAllBreakingsIndicesBySolverObject : public FTimeResource
		{
			FAllBreakingsIndicesBySolverObject()
				: AllBreakingsIndicesBySolverObjectMap(TMap<ISolverObjectBase*, TArray<int32>>())
			{}

			void Reset()
			{
				AllBreakingsIndicesBySolverObjectMap.Reset();
			}

			TMap<ISolverObjectBase*, TArray<int32>> AllBreakingsIndicesBySolverObjectMap; // SolverObject -> Indices in AllBreakings array
		};

		// All the Breakings with all the maps
		struct FAllBreakingDataMaps
		{
			FAllBreakingDataMaps()
				: bIsDataAndMapsInSync(false)
				, AllBreakingData(nullptr)
				, SolverObjectReverseMapping(nullptr)
				, ParticleIndexReverseMapping(nullptr)
				, AllBreakingsIndicesBySolverObject(nullptr)
			{}

			bool IsValid() const { return bIsDataAndMapsInSync; }

			bool bIsDataAndMapsInSync;
			const FAllBreakingData* AllBreakingData;
			const FSolverObjectReverseMapping* SolverObjectReverseMapping;
			const FParticleIndexReverseMapping* ParticleIndexReverseMapping;
			const FAllBreakingsIndicesBySolverObject* AllBreakingsIndicesBySolverObject;
		};

		/* Trailing */

		typedef TSet<TTrailingData<float, 3>> FTrailingDataSet;
		typedef TArray<TTrailingData<float, 3>> FTrailingDataArray;
		struct FTrailingData
		{
			float TimeLastUpdated;
			FTrailingDataSet TrailingDataSet;
		};
		typedef TPBDRigidClustering<FPBDRigidsEvolution, FCollisionConstraints, float, 3> FClusteringType;

		// All the trailings for one frame time stamped with the time for that frame  
		struct FAllTrailingData : FTimeResource
		{
			FAllTrailingData()
				: AllTrailingsArray(FTrailingDataArray())
			{}

			void Reset()
			{
				AllTrailingsArray.Reset();
			}

			FTrailingDataArray AllTrailingsArray;
		};

		// AllTrailingsIndicesBySolverObject for one frame time stamped with the time for that frame
		struct FAllTrailingsIndicesBySolverObject : public FTimeResource
		{
			FAllTrailingsIndicesBySolverObject()
				: AllTrailingsIndicesBySolverObjectMap(TMap<ISolverObjectBase*, TArray<int32>>())
			{}

			void Reset()
			{
				AllTrailingsIndicesBySolverObjectMap.Reset();
			}

			TMap<ISolverObjectBase*, TArray<int32>> AllTrailingsIndicesBySolverObjectMap; // SolverObject -> Indices in AllTrailings array
		};

		// All the Trailings with all the maps
		struct FAllTrailingDataMaps
		{
			FAllTrailingDataMaps()
				: bIsDataAndMapsInSync(false)
				, AllTrailingData(nullptr)
				, SolverObjectReverseMapping(nullptr)
				, ParticleIndexReverseMapping(nullptr)
				, AllTrailingsIndicesBySolverObject(nullptr)
			{}

			bool IsValid() const { return bIsDataAndMapsInSync; }

			bool bIsDataAndMapsInSync;
			const FAllTrailingData* AllTrailingData;
			const FSolverObjectReverseMapping* SolverObjectReverseMapping;
			const FParticleIndexReverseMapping* ParticleIndexReverseMapping;
			const FAllTrailingsIndicesBySolverObject* AllTrailingsIndicesBySolverObject;
		};


		class CHAOSSOLVERS_API FScopedGetEventsData
		{
		public:

			FScopedGetEventsData(const FPBDRigidsSolver* SolverIn) : Solver(SolverIn)
			{
				check(Solver);
				if (Solver->BufferMode == EMultiBufferMode::Double)
				{
					Solver->SolverResourceLock.ReadLock();
				}
			}

			~FScopedGetEventsData()
			{
				check(Solver);

				if (Solver->BufferMode == EMultiBufferMode::Double)
				{
					Solver->SolverResourceLock.ReadUnlock();
				}
			}

			const Chaos::FPBDRigidsSolver::FAllCollisionDataMaps& GetAllCollisions_Maps() const
			{
				return Solver->GetAllCollisions_Maps_GameThread_NEEDSLOCK();
			}

			const Chaos::FPBDRigidsSolver::FAllBreakingDataMaps& GetAllBreakings_Maps() const
			{
				return Solver->GetAllBreakings_Maps_GameThread_NEEDSLOCK();
			}

			const Chaos::FPBDRigidsSolver::FAllTrailingDataMaps& GetAllTrailings_Maps() const
			{
				return Solver->GetAllTrailings_Maps_GameThread_NEEDSLOCK();
			}


		private:

			const FPBDRigidsSolver* Solver;
		};


		/* ---------------- */

		private:
		FPBDRigidsSolver(const EMultiBufferMode BufferingModeIn);
		public:


		/* Object registration */
		void RegisterObject(TSolverObject<FGeometryCollectionPhysicsObject>* InObject);
		void RegisterObject(TSolverObject<FSkeletalMeshPhysicsObject>* InObject);
		void RegisterObject(TSolverObject<FStaticMeshPhysicsObject>* InObject);
		void RegisterObject(TSolverObject<FBodyInstancePhysicsObject>* InObject);
		void RegisterObject(FFieldSystemPhysicsObject* InObject);

		void UnregisterObject(TSolverObject<FGeometryCollectionPhysicsObject>* InObject);
		void UnregisterObject(TSolverObject<FSkeletalMeshPhysicsObject>* InObject);
		void UnregisterObject(TSolverObject<FStaticMeshPhysicsObject>* InObject);
		void UnregisterObject(TSolverObject<FBodyInstancePhysicsObject>* InObject);
		void UnregisterObject(FFieldSystemPhysicsObject* InObject);
		/*---------------------*/

		const FSolverObjectStorage& GetObjectStorage() const { return Objects; }
		FSolverObjectStorage& GetObjectStorage() { return Objects; }
		
		const FSolverObjectStorage& GetObjectStorage_GameThread() const { return Objects_GameThread; }
		FSolverObjectStorage& GetObjectStorage_GameThread() { return Objects_GameThread; }

		const FSolverObjectStorage& GetRemovedObjectStorage() const { return RemovedObjects; }
		FSolverObjectStorage& GetRemovedObjectStorage() { return RemovedObjects; }

		bool HasActiveObjects() const { return !!Objects.GetNumObjects(); }

		/**/
		void Reset();
		void ChangeBufferMode(Chaos::EMultiBufferMode InBufferMode);

		/**/
		void AdvanceSolverBy(float DeltaTime);

		/* Particle Update and access*/
		void InitializeFromParticleData(const int32 StartIndex) { MEvolution->InitializeFromParticleData(StartIndex); }
		const FParticlesType& GetRigidParticles() const { return MEvolution->GetParticles(); }
		FParticlesType& GetRigidParticles() { return MEvolution->GetParticles(); }

		/**/
		const TSet<int32>& ActiveIndices() const { return MEvolution->GetActiveIndices(); }
		TSet<int32>& ActiveIndices() { return MEvolution->GetActiveIndices(); }
		const TArray<int32>& NonDisabledIndices() const { return MEvolution->GetNonDisabledIndices(); }
		TArray<int32>& NonDisabledIndices() { return MEvolution->GetNonDisabledIndices(); }

		void WakeIslands(const TSet<int32>& InIslandIndices) { MEvolution->WakeIslands(InIslandIndices); }


		TSerializablePtr<TChaosPhysicsMaterial<float>> GetPhysicsMaterial(const int32 Index) const { return MEvolution->GetPhysicsMaterial(Index);  }
		void SetPhysicsMaterial(const int32 Index, TSerializablePtr<TChaosPhysicsMaterial<float>> InMaterial) const { return MEvolution->SetPhysicsMaterial(Index, InMaterial); }

		const TUniquePtr<TChaosPhysicsMaterial<float>>& GetPerParticlePhysicsMaterial(const int32 Index) const { return MEvolution->GetPerParticlePhysicsMaterial(Index); }
		void SetPerParticlePhysicsMaterial(const int32 Index, TUniquePtr<TChaosPhysicsMaterial<float>>&& InMaterial) const { return MEvolution->SetPerParticlePhysicsMaterial(Index, MoveTemp(InMaterial)); }

		FCollisionConstraints& GetCollisionConstraints() const { return MEvolution->GetCollisionConstraints(); }

		const TSet<TTuple<int32, int32>>& GetDisabledCollisionPairs() const { return MEvolution->GetDisabledCollisions(); }
		void RemoveConstraints(const TSet<uint32>& RemovedParticles) { MEvolution->RemoveConstraints(RemovedParticles); }

		/**/
		void SetCurrentFrame(const int32 CurrentFrameIn) { CurrentFrame = CurrentFrameIn; }
		int32 GetCurrentFrame() { return CurrentFrame; }

		/* Pause */
#if CHAOS_WITH_PAUSABLE_SOLVER
		bool Paused() const { return bPaused; }
		void SetPaused(bool bPausedIn) { bPaused = bPausedIn; }
#endif

		bool Enabled() const;
		void SetEnabled(bool bEnabledIn) { bEnabled = bEnabledIn; }

		/* Clustering Access */
		FClusteringType& GetRigidClustering() { return MEvolution->GetRigidClustering(); }
		const FClusteringType& GetRigidClustering() const { return MEvolution->GetRigidClustering(); }

		/**/
		const float GetSolverTime() { return MTime; }
		const float GetLastDt() { return MLastDt; }
		const bool GetGenerateCollisionData() { return bDoGenerateCollisionData; }
		const bool GetGenerateBreakingData() { return bDoGenerateBreakingData; }
		const bool GetGenerateTrailingData() { return bDoGenerateTrailingData; }

		const TArray<TBreakingData<float, 3>>& GetAllClusterBreakings() const { return MEvolution->GetRigidClustering().GetAllClusterBreakings(); }

		/**/
		const ISpatialAcceleration<float,3>* GetSpatialAcceleration() const { return &GetCollisionConstraints().GetSpatialAcceleration(); }
		void ReleaseSpatialAcceleration() const { GetCollisionConstraints().ReleaseSpatialAcceleration(); }

		/* General maps for reverse lookups */
		const FSolverObjectReverseMapping& GetSolverObjectReverseMapping_GameThread() const
		{ 
			SolverObjectReverseMappingLock.ReadLock();
			return *SolverObjectReverseMappingBuffer->GetConsumerBuffer();
		}
		void ReleaseSolverObjectReverseMapping() const { SolverObjectReverseMappingLock.ReadUnlock(); }

		void SyncEvents_GameThread();

private:
		/* Collision */

		/** 
		 * Returns the most recent gamethread-accessible collision data. 
		 */
		const FAllCollisionDataMaps& GetAllCollisions_Maps_GameThread_NEEDSLOCK() const;

		/* Breaking */

		/**
		 * Returns the most recent gamethread-accessible breaking data, incl various data maps
		 */
		const FAllBreakingDataMaps& GetAllBreakings_Maps_GameThread_NEEDSLOCK() const;

		/* Trailing */

		/**
		 * Returns the most recent gamethread-accessible trailing data, incl various data maps
		 */
		const FAllTrailingDataMaps& GetAllTrailings_Maps_GameThread_NEEDSLOCK() const;
public:

		/**/
		void SetTimeStepMultiplier(float TimeStepMultiplierIn) { ensure(TimeStepMultiplierIn > 0); TimeStepMultiplier = TimeStepMultiplierIn; }
		void SetIterations(int32 Iterations) { MEvolution->SetIterations(Iterations); }
		void SetPushOutIterations(int32 PushOutIterations) { MEvolution->SetPushOutIterations(PushOutIterations); }
		void SetPushOutPairIterations(int32 PushOutPairIterations) { MEvolution->SetPushOutPairIterations(PushOutPairIterations); }
		void SetClusterConnectionFactor(float ClusterConnectionFactor) { GetRigidClustering().SetClusterConnectionFactor(ClusterConnectionFactor); }
		void SetClusterUnionConnectionType(FClusterCreationParameters<float>::EConnectionMethod ClusterConnectionType) {GetRigidClustering().SetClusterUnionConnectionType(ClusterConnectionType);}
		void SetGenerateCollisionData(bool bDoGenerate) { bDoGenerateCollisionData = bDoGenerate; }
		void SetGenerateBreakingData(bool bDoGenerate) 
		{
			bDoGenerateBreakingData = bDoGenerate; 
			GetRigidClustering().SetGenerateClusterBreaking(bDoGenerateBreakingData);
		}
		void SetGenerateTrailingData(bool bDoGenerate) { bDoGenerateTrailingData = bDoGenerate; }
		void SetCollisionFilterSettings(const FSolverCollisionFilterSettings InCollisionFilterSettings)
		{
			CollisionFilterSettings = InCollisionFilterSettings;
		}
		const FSolverCollisionFilterSettings& GetCollisionFilterSettings() const { return CollisionFilterSettings; }

		void SetBreakingFilterSettings(const FSolverBreakingFilterSettings InBreakingFilterSettings)
		{
			BreakingFilterSettings = InBreakingFilterSettings;
		}
		const FSolverBreakingFilterSettings& GetBreakingFilterSettings() const { return BreakingFilterSettings; }

		void SetTrailingFilterSettings(const FSolverTrailingFilterSettings InTrailingFilterSettings)
		{
			TrailingFilterSettings = InTrailingFilterSettings;
		}
		const FSolverTrailingFilterSettings& GetTrailingFilterSettings() const { return TrailingFilterSettings; }

		/**/
		void SetHasFloor(bool bHasFloorIn) { bHasFloor = bHasFloorIn; }
		void SetIsFloorAnalytic(bool bIsAnalytic) { bIsFloorAnalytic = bIsAnalytic; }
		void SetFloorHeight(float Height) { FloorHeight = Height; }
		void SetMassScale(float MassScaleIn) { MassScale = MassScaleIn; }
		int32 GetFloorIndex() const { return FloorIndex; }

		const Chaos::TArrayCollectionArray<SolverObjectWrapper>& GetSolverObjectReverseMapping() const { return SolverObjectReverseMapping; }
		const Chaos::TArrayCollectionArray<int32>& GetParticleIndexReverseMapping() const { return ParticleIndexReverseMapping; }

		FCollisionDataArray& GetAllCollisionsDataArray() { return AllCollisionsBuffer->AccessProducerBuffer()->AllCollisionsArray; }
		TMap<ISolverObjectBase*, TArray<int32>>& GetAllCollisionsIndicesBySolverObject() { return AllCollisionsIndicesBySolverObjectBuffer->AccessProducerBuffer()->AllCollisionsIndicesBySolverObjectMap; }
		FBreakingDataArray& GetAllBreakingsDataArray() { return AllBreakingsBuffer->AccessProducerBuffer()->AllBreakingsArray; }
		TMap<ISolverObjectBase*, TArray<int32>>& GetAllBreakingsIndicesBySolverObject() { return AllBreakingsIndicesBySolverObjectBuffer->AccessProducerBuffer()->AllBreakingsIndicesBySolverObjectMap; }
		FTrailingDataArray& GetAllTrailingsDataArray() { return AllTrailingsBuffer->AccessProducerBuffer()->AllTrailingsArray; }
		TMap<ISolverObjectBase*, TArray<int32>>& GetAllTrailingsIndicesBySolverObject() { return AllTrailingsIndicesBySolverObjectBuffer->AccessProducerBuffer()->AllTrailingsIndicesBySolverObjectMap; }

		int32 GetParticleIndexMesh(const TMap<uint32, TUniquePtr<TArray<uint32>>>& ParentToChildrenMap, int32 ParticleIndex);

		/** Returns encoded collision index. */
		static int32 EncodeCollisionIndex(int32 ActualCollisionIndex, bool bSwapOrder);
		/** Returns decoded collision index. */
		static int32 DecodeCollisionIndex(int32 EncodedCollisionIdx, bool& bSwapOrder);


		TQueue<TFunction<void(FPBDRigidsSolver*)>, EQueueMode::Mpsc>& GetCommandQueue() { return CommandQueue; }

#if !UE_BUILD_SHIPPING
		void SerializeForPerfTest(FChaosArchive& Ar) { MEvolution->SerializeForPerfTest(Ar); }
		void SerializeForPerfTest(const FString& FileName);
#endif

		/* Return the debug substep object to allow pausing/stepping/substepping operations on this solver. */
		FDebugSubstep& GetDebugSubstep() const { return MEvolution->GetDebugSubstep(); }

		const float GetMassScale() const { return MassScale; }

		FPBDRigidsEvolution* GetEvolution() { return MEvolution.Get(); }

	protected:
		/**/
		void CreateRigidBodyCallback(FParticlesType& Particles);

		/**/
		void ParameterUpdateCallback(FParticlesType& Particles, const float Time);

		/**/
		void ForceUpdateCallback(FParticlesType& Particles, const float Time);

		/**/
		void DisableCollisionsCallback(TSet<TTuple<int32, int32>>& CollisionPairs);

		/**/
		void StartFrameCallback(const float Dt, const float Time);

		/**/
		void EndFrameCallback(const float EndFrame);

		/**/
		void KinematicUpdateCallback(FParticlesType& Particles, const float Dt, const float Time);

		/**/
		void AddForceCallback(FParticlesType& Particles, const float Time, const int32 Index);

		/**/
		void CollisionContactsCallback(FParticlesType& Particles, FCollisionConstraints& CollisionConstraints);

		/**/
		void BreakingCallback(FParticlesType& Particles);

		/**/
		void TrailingCallback(FParticlesType& Particles);

		/**/
		void BindParticleCallbackMappingPart1( );
		void BindParticleCallbackMappingPart2( );

	private:

		int32 CurrentFrame;
		float MTime;
		float MLastDt;
		float MMaxDeltaTime;
		float TimeStepMultiplier;

		bool bDoGenerateCollisionData;
		bool bDoGenerateBreakingData;
		bool bDoGenerateTrailingData;

#if CHAOS_WITH_PAUSABLE_SOLVER
		bool bPaused;
#endif
		bool bEnabled;
		bool bHasFloor;
		bool bIsFloorAnalytic;
		float FloorHeight;
		int32 FloorIndex;

		float MassScale;

		FSolverCollisionFilterSettings CollisionFilterSettings;
		FSolverBreakingFilterSettings BreakingFilterSettings;
		FSolverTrailingFilterSettings TrailingFilterSettings;

		TSharedPtr<FEvent> MCurrentEvent;
		TSharedPtr<FCriticalSection> MCurrentLock;

		TUniquePtr<FPBDRigidsEvolution> MEvolution;

		int32 FieldForceNum;
		TArrayCollectionArray<FVector> FieldForce;
		TArrayCollectionArray<FVector> FieldTorque;
		TArray<FKinematicProxy> KinematicProxies;

		TArray<FKinematicProxy> KinematicProxiesForObjects;

		FPositionConstraints PositionTarget;
		FPositionConstraintsRule PositionConstraintsRule;
		TMap<int32, int32> PositionTargetedParticles;

		FRigidDynamicSpringConstraints DynamicConstraints;
		FRigidDynamicSpringConstraintsRule DynamicConstraintsRule;
		TSet<int32> DynamicConstraintParticles;

		FJointConstraints JointConstraints;
		FJointConstraintsRule JointConstraintsRule;

		int32 LastSwapMappingSyncSize;
		int32 LastMappingSyncSize;

		// Mappings
		TArrayCollectionArray<SolverObjectWrapper> SolverObjectReverseMapping; // ParticleIndex -> SolverObject
		TArrayCollectionArray<int32> ParticleIndexReverseMapping; // ParticleIndex -> TransformIndex

		TQueue<TFunction<void(FPBDRigidsSolver*)>, EQueueMode::Mpsc> CommandQueue;

		// Storage for objects
		//  - Objects -> Physics thread side objects. Never read from any other thread
		//  - Objects_GameThread -> Game thread side objects. This can be iterated and
		//                          then GT only data read from the objects. This doesn't
		//                          guarantee all data in the object is safe, only that the
		//                          size of the storage won't change off the game thread
		//  - RemovedObjects -> Temporary storage when objects are removed until the runtime
		//                      hits a final sync opportunity
		FSolverObjectStorage Objects;
		FSolverObjectStorage Objects_GameThread;
		FSolverObjectStorage RemovedObjects;

		TAtomic<bool> GameThreadHasSynced;

	public:
		mutable FRWLock SolverResourceLock;
		mutable FRWLock SolverObjectReverseMappingLock;
		mutable FRWLock ParticleIndexReverseMappingLock;

		const FScopedGetEventsData ScopedGetEventsData() const { return FScopedGetEventsData(this); }
		EMultiBufferMode BufferMode;

	private:
		TUniquePtr<IBufferResource<FSolverObjectReverseMapping>> SolverObjectReverseMappingBuffer;
		TUniquePtr<IBufferResource<FParticleIndexReverseMapping>> ParticleIndexReverseMappingBuffer;

	public:
		const FAllCollisionData* GetAllCollisions_FromSequencerCache_NEEDSLOCK() const { return AllCollisions_FromSequencerCache.Get(); }
		const FAllBreakingData* GetAllBreakings_FromSequencerCache_NEEDSLOCK() const { return AllBreakings_FromSequencerCache.Get(); }
		const FAllTrailingData* GetAllTrailings_FromSequencerCache_NEEDSLOCK() const { return AllTrailings_FromSequencerCache.Get(); }
	private:

		TUniquePtr<FAllCollisionData> AllCollisions_FromSequencerCache;
		TUniquePtr<FAllBreakingData> AllBreakings_FromSequencerCache;
		TUniquePtr<FAllTrailingData> AllTrailings_FromSequencerCache;
		//mutable FCriticalSection SequencerCacheLock;
	//private:
		TUniquePtr<IBufferResource<FAllCollisionData>> AllCollisionsBuffer;
		TUniquePtr<IBufferResource<FAllCollisionsIndicesBySolverObject>> AllCollisionsIndicesBySolverObjectBuffer;
		FAllCollisionDataMaps AllCollisionDataMaps;


		/* Breakings */
		TUniquePtr<IBufferResource<FAllBreakingData>> AllBreakingsBuffer;
		TUniquePtr<IBufferResource<FAllBreakingsIndicesBySolverObject>> AllBreakingsIndicesBySolverObjectBuffer;

		FAllBreakingDataMaps AllBreakingDataMaps;

		TUniquePtr<IBufferResource<FAllTrailingData>> AllTrailingsBuffer;
		TUniquePtr<IBufferResource<FAllTrailingsIndicesBySolverObject>> AllTrailingsIndicesBySolverObjectBuffer;

		FAllTrailingDataMaps AllTrailingDataMaps;

		TUniquePtr<FSolverCollisionEventFilter> SolverCollisionEventFilter;
		TUniquePtr<FSolverBreakingEventFilter> SolverBreakingEventFilter;
		TUniquePtr<FSolverTrailingEventFilter> SolverTrailingEventFilter;
		
		// Previous frame
		int32 NumCollisionsPrevFrame;
		int32 NumBreakingsPrevFrame;
		int32 NumTrailingsPrevFrame;
	};

	struct FSolverReadLock : public FNoncopyable
	{
		FSolverReadLock() = delete;
		FSolverReadLock(const FPBDRigidsSolver* InSolver)
		{
			CachedLock = &(InSolver->SolverResourceLock);
			CachedLock->ReadLock();
		}

		~FSolverReadLock()
		{
			CachedLock->ReadUnlock();
		}

	private:
		FRWLock* CachedLock;
	};

	struct FSolverWriteLock : public FNoncopyable
	{
		FSolverWriteLock() = delete;
		FSolverWriteLock(FPBDRigidsSolver* InSolver)
		{
			CachedLock = &(InSolver->SolverResourceLock);
			CachedLock->WriteLock();
		}

		~FSolverWriteLock()
		{
			CachedLock->WriteUnlock();
		}

	private:
		FRWLock* CachedLock;
	};
}

#endif

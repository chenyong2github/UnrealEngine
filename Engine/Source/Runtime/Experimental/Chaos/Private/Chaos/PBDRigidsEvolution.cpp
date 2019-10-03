// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "Chaos/PBDRigidsEvolution.h"
#include "Chaos/Defines.h"
#include "Chaos/PBDRigidsSOAs.h"
#include "ChaosStats.h"
#include "Chaos/PBDRigidsEvolutionGBF.h"
#include "Chaos/ParticleHandle.h"

namespace Chaos
{
	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	TPBDRigidsEvolutionBase<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::TPBDRigidsEvolutionBase(TPBDRigidsSOAs<T, d>& InParticles, int32 InNumIterations)
		: Particles(InParticles)
		, bExternalReady(false)
		, Clustering(static_cast<FPBDRigidsEvolution&>(*this), Particles.GetClusteredParticles())
		, NumIterations(InNumIterations)
	{
		Particles.GetParticleHandles().AddArray(&PhysicsMaterials);
		Particles.GetParticleHandles().AddArray(&PerParticlePhysicsMaterials);
		Particles.GetParticleHandles().AddArray(&ParticleDisableCount);
		Particles.GetParticleHandles().AddArray(&Collided);
		
		for (auto& Particle : InParticles.GetNonDisabledView())
		{
			DirtyParticle(Particle);
		}

		ComputeIntermediateSpatialAcceleration();
	}

	DECLARE_CYCLE_STAT(TEXT("ComputeConstraintsBP"), STAT_ComputeConstraintsBP, STATGROUP_Chaos);
	DECLARE_CYCLE_STAT(TEXT("CacheAccelerationBounds"), STAT_CacheAccelerationBounds, STATGROUP_Chaos);
	DECLARE_CYCLE_STAT(TEXT("ComputeIntermediateSpatialAcceleration"), STAT_ComputeIntermediateSpatialAcceleration, STATGROUP_Chaos);

	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	TPBDRigidsEvolutionBase<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::FChaosAccelerationStructureTask::FChaosAccelerationStructureTask(const TArray<FAccelerationStructureBuilder>& InBounds
		, TUniquePtr<FAccelerationStructure>& InAccelerationStructure
		, TUniquePtr<FAccelerationStructure>& InAccelerationStructureCopy)
		: CachedSpatialBuilderData(InBounds)
		, AccelerationStructure(InAccelerationStructure)
		, AccelerationStructureCopy(InAccelerationStructureCopy)
	{
	}

	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	TStatId TPBDRigidsEvolutionBase<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::FChaosAccelerationStructureTask::GetStatId()
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FChaosAccelerationStructureTask, STATGROUP_Chaos);
	}

	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	ENamedThreads::Type TPBDRigidsEvolutionBase<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::FChaosAccelerationStructureTask::GetDesiredThread()
	{
		return ENamedThreads::AnyBackgroundThreadNormalTask;
	}

	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	ESubsequentsMode::Type TPBDRigidsEvolutionBase<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::FChaosAccelerationStructureTask::GetSubsequentsMode()
	{
		return ESubsequentsMode::TrackSubsequents;
	}

	int32 BroadphaseType = 1;
	FAutoConsoleVariableRef CVarBroadphaseIsTree(TEXT("p.BroadphaseType"), BroadphaseType, TEXT(""));

	int32 BoundingVolumeNumCells = 35;
	FAutoConsoleVariableRef CVarBoundingVolumeNumCells(TEXT("p.BoundingVolumeNumCells"), BoundingVolumeNumCells, TEXT(""));

	int32 MaxChildrenInLeaf = 5;
	FAutoConsoleVariableRef CVarMaxChildrenInLeaf(TEXT("p.MaxChildrenInLeaf"), MaxChildrenInLeaf, TEXT(""));

	int32 MaxTreeDepth = 200;
	FAutoConsoleVariableRef CVarMaxTreeDepth(TEXT("p.MaxTreeDepth"), MaxTreeDepth, TEXT(""));

	int32 AABBMaxChildrenInLeaf = 500;
	FAutoConsoleVariableRef CVarAABBMaxChildrenInLeaf(TEXT("p.AABBMaxChildrenInLeaf"), AABBMaxChildrenInLeaf, TEXT(""));

	int32 AABBMaxTreeDepth = 200;
	FAutoConsoleVariableRef CVarAABBMaxTreeDepth(TEXT("p.AABBMaxTreeDepth"), AABBMaxTreeDepth, TEXT(""));

	int32 MaxPayloadSize = 20000;
	FAutoConsoleVariableRef CVarMaxPayloadSize(TEXT("p.MaxPayloadSize"), MaxPayloadSize, TEXT(""));

	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	ISpatialAcceleration<TAccelerationStructureHandle<T, d>, T, d>*  TPBDRigidsEvolutionBase<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::CreateNewSpatialStructure(const TArray<FAccelerationStructureBuilder>& Bounds)
	{
		if (BroadphaseType == 0)
		{
			return new TBoundingVolume<TAccelerationStructureHandle<T, d>, T, d>(Bounds, false, 0, BoundingVolumeNumCells, MaxPayloadSize);
		}
		else if(BroadphaseType == 1)
		{
			return new TAABBTree<TAccelerationStructureHandle<T, d>, TAABBTreeLeafArray<TAccelerationStructureHandle<T, d>, T>, T>(Bounds, MaxChildrenInLeaf, MaxTreeDepth, MaxPayloadSize);
		}
		else
		{
			return new TAABBTree<TAccelerationStructureHandle<T, d>, TBoundingVolume<TAccelerationStructureHandle<T, d>, T, d>, T>(Bounds, AABBMaxChildrenInLeaf, AABBMaxTreeDepth, MaxPayloadSize);
		}
	}

	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	void TPBDRigidsEvolutionBase<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::FChaosAccelerationStructureTask::DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		AccelerationStructure.Reset(CreateNewSpatialStructure(CachedSpatialBuilderData));
		AccelerationStructureCopy = AccelerationStructure->Copy();
	}

	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	void TPBDRigidsEvolutionBase<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::ApplyParticlePendingData(TGeometryParticleHandle<T, d>* Particle, const FPendingSpatialData& SpatialData, FAccelerationStructure& AccelerationStructure, bool bAsync)
	{
		//Note: we collapsed several update delete events into one struct. If memory is reused this can lead to problems
		//Luckily there are only 3 states we care about:
		//While pending we updated an object several times, this collapses into one update
		//While pending we may have updated an object, we may have also created and destroyed the object, but the final event is a delete, so just remove from acceleration structure
		//While pending we destroyed, recreated using the same memory address, and then did an update. In this case we should remove first and then update as global bounds may have changed
		//As long as we delete first and update second this will be respected
		if (SpatialData.bDelete)
		{
			AccelerationStructure.RemoveElement(SpatialData.AccelerationHandle);

			if (bAsync)
			{
				if (int32* CacheIdxPtr = ParticleToCacheIdx.Find(Particle))
				{
					const int32 CacheIdx = *CacheIdxPtr;
					if (CacheIdx + 1 < CachedSpatialBuilderData.Num())	//will get swapped with last element, so update it
					{
						//in cached bounds so must be in mapping, update mapping to new position
						int32& PrevIdx = ParticleToCacheIdx.FindChecked(CachedSpatialBuilderData.Last().CachedSpatialPayload.GetGeometryParticleHandle_PhysicsThread());
						PrevIdx = CacheIdx;
					}

					CachedSpatialBuilderData.RemoveAtSwap(CacheIdx);
					ParticleToCacheIdx.Remove(Particle);
				}
			}
		}

		if(SpatialData.bUpdate)
		{
			AccelerationStructure.UpdateElement(Particle, Particle->WorldSpaceInflatedBounds(), Particle->HasBounds());
			
			if (bAsync)
			{
				//make sure in mapping
				int32 CacheIdx;
				if (int32* CacheIdxPtr = ParticleToCacheIdx.Find(Particle))
				{
					CacheIdx = *CacheIdxPtr;
				}
				else
				{
					CacheIdx = CachedSpatialBuilderData.Num();
					ParticleToCacheIdx.Add(Particle, CacheIdx);
					CachedSpatialBuilderData.AddUninitialized();
				}

				//update cache itself
				CachedSpatialBuilderData[CacheIdx] = FAccelerationStructureBuilder{ Particle->HasBounds(), Particle->WorldSpaceInflatedBounds(), SpatialData.AccelerationHandle };
			}
		}
	}

	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	void TPBDRigidsEvolutionBase<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::FlushInternalAccelerationQueue()
	{
		for (auto Itr : InternalAccelerationQueue)
		{
			ApplyParticlePendingData(Itr.Key, Itr.Value, *InternalAcceleration, false);
		}
		InternalAccelerationQueue.Empty();
	}

	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	void TPBDRigidsEvolutionBase<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::FlushAsyncAccelerationQueue()
	{
		for (auto Itr : AsyncAccelerationQueue)
		{
			ApplyParticlePendingData(Itr.Key, Itr.Value, *AsyncInternalAcceleration, true);
			ApplyParticlePendingData(Itr.Key, Itr.Value, *AsyncExternalAcceleration, true);
		}
		AsyncAccelerationQueue.Empty();

		//other queues are no longer needed since we've flushed all operations and now have a pristine structure
		InternalAccelerationQueue.Empty();
		ExternalAccelerationQueue.Empty();
	}

	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	void TPBDRigidsEvolutionBase<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::FlushExternalAccelerationQueue(FAccelerationStructure& Acceleration)
	{
		for (auto Itr : ExternalAccelerationQueue)
		{
			ApplyParticlePendingData(Itr.Key, Itr.Value, Acceleration, false);
		}
		ExternalAccelerationQueue.Empty();
	}

	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	void TPBDRigidsEvolutionBase<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::WaitOnAccelerationStructure()
	{
		if (AccelerationStructureTaskComplete.GetReference())
		{
			FGraphEventArray ThingsToComplete;
			ThingsToComplete.Add(AccelerationStructureTaskComplete);
			QUICK_SCOPE_CYCLE_COUNTER(STAT_TPBDRigidsEvolutionBase_WaitAccelerationStructure);
			FTaskGraphInterface::Get().WaitUntilTasksComplete(ThingsToComplete);
		}
	}

	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	void TPBDRigidsEvolutionBase<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::ComputeIntermediateSpatialAcceleration(bool bBlock)
	{
		SCOPE_CYCLE_COUNTER(STAT_ComputeIntermediateSpatialAcceleration);
		CHAOS_SCOPED_TIMER(ComputeIntermediateSpatialAcceleration);
		if (!AccelerationStructureTaskComplete)
		{
			//initial frame so make empty structure
			InternalAcceleration = TUniquePtr<FAccelerationStructure>(CreateNewSpatialStructure(TArray<FAccelerationStructureBuilder>()));
			ScratchExternalAcceleration = TUniquePtr<FAccelerationStructure>(CreateNewSpatialStructure(TArray<FAccelerationStructureBuilder>()));
			FlushInternalAccelerationQueue();
			FlushExternalAccelerationQueue(*ScratchExternalAcceleration);
			bExternalReady = true;
		}

		if (bBlock)
		{
			WaitOnAccelerationStructure();
		}

		if (!AccelerationStructureTaskComplete || AccelerationStructureTaskComplete->IsComplete())
		{
			if (AccelerationStructureTaskComplete)
			{
				FlushAsyncAccelerationQueue();

				//swap acceleration structure for new one
				std::swap(InternalAcceleration, AsyncInternalAcceleration);	//swap to avoid free on sync part as this can be expensive
				std::swap(ScratchExternalAcceleration, AsyncExternalAcceleration);
				bExternalReady = true;
			}

			AccelerationStructureTaskComplete = TGraphTask<FChaosAccelerationStructureTask>::CreateTask().ConstructAndDispatchWhenReady(CachedSpatialBuilderData, AsyncInternalAcceleration, AsyncExternalAcceleration);
		}
		else
		{
			FlushInternalAccelerationQueue();
		}
	}

	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	void TPBDRigidsEvolutionBase<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::UpdateExternalAccelerationStructure(TUniquePtr<ISpatialAcceleration<TAccelerationStructureHandle<T, d>, T, d>>& StructToUpdate)
	{
		DECLARE_SCOPE_CYCLE_COUNTER(TEXT("CreateExternalAccelerationStructure"), STAT_CreateExternalAccelerationStructure, STATGROUP_Physics);
		if (bExternalReady)
		{
			std::swap(StructToUpdate, ScratchExternalAcceleration);
		}
		bExternalReady = false;

		if (ensure(StructToUpdate))
		{
			FlushExternalAccelerationQueue(*StructToUpdate);
		}
	}
}

#ifdef __clang__
template class CHAOS_API Chaos::TPBDRigidsEvolutionBase<Chaos::TPBDRigidsEvolutionGBF<float, 3>, Chaos::TPBDCollisionConstraint<float,3>, float, 3>;
#else
template class Chaos::TPBDRigidsEvolutionBase<Chaos::TPBDRigidsEvolutionGBF<float, 3>, Chaos::TPBDCollisionConstraint<float,3>, float, 3>;
#endif

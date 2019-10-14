// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "Chaos/PBDRigidsEvolution.h"
#include "Chaos/Defines.h"
#include "Chaos/PBDRigidsSOAs.h"
#include "ChaosStats.h"
#include "Chaos/PBDRigidsEvolutionGBF.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/ISpatialAccelerationCollection.h"

namespace Chaos
{
	int32 BroadphaseType = 3;
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

	float MaxPayloadSize = 20000;
	FAutoConsoleVariableRef CVarMaxPayloadSize(TEXT("p.MaxPayloadSize"), MaxPayloadSize, TEXT(""));

	template<typename T, int d>
	struct TDefaultCollectionFactory : public ISpatialAccelerationCollectionFactory<T, d>
	{
		virtual TUniquePtr<ISpatialAccelerationCollection<TAccelerationStructureHandle<T, d>, T, d>> CreateEmptyCollection() override
		{
			const TArray<TAccelerationStructureBuilder<T, d>> Empty;
			if (BroadphaseType == 0)
			{
				using AccelType = TBoundingVolume<TAccelerationStructureHandle<T, d>, T, d>;
				TArray<TSpatialAccelerationParams<TArray<TAccelerationStructureBuilder<T, d>>, TAccelerationStructureHandle<T, d>, T, d>> Buckets = { {AccelType::StaticType, MaxPayloadSize, BoundingVolumeNumCells } };

				auto Structure = MakeUnique<AccelType>(Empty, false, 0, Buckets[0].MaxCells, Buckets[0].MaxPayloadBounds);
				auto Collection = new TSpatialAccelerationCollection<TArray<TAccelerationStructureBuilder<T, d>>, AccelType>(MoveTemp(Buckets));

				Collection->AddSubstructure(MoveTemp(Structure), 0);

				return TUniquePtr<ISpatialAccelerationCollection<TAccelerationStructureHandle<T, d>, T, d>>(Collection);
			}
			else if (BroadphaseType == 1)
			{
				using AccelType = TAABBTree<TAccelerationStructureHandle<T, d>, TAABBTreeLeafArray<TAccelerationStructureHandle<T, d>, T>, T>;
				TArray<TSpatialAccelerationParams<TArray<TAccelerationStructureBuilder<T, d>>, TAccelerationStructureHandle<T, d>, T, d>> Buckets = { {AccelType::StaticType, MaxPayloadSize, 0, MaxChildrenInLeaf, MaxTreeDepth} };

				auto Structure = MakeUnique<AccelType>(Empty, Buckets[0].MaxChildrenInLeaf, Buckets[0].MaxTreeDepth, Buckets[0].MaxPayloadBounds);
				auto Collection = new TSpatialAccelerationCollection<TArray<TAccelerationStructureBuilder<T, d>>, AccelType>(MoveTemp(Buckets));
				Collection->AddSubstructure(MoveTemp(Structure), 0);
				return TUniquePtr<ISpatialAccelerationCollection<TAccelerationStructureHandle<T, d>, T, d>>(Collection);
			}
			else if (BroadphaseType == 2)
			{
				using AccelType = TAABBTree<TAccelerationStructureHandle<T, d>, TBoundingVolume<TAccelerationStructureHandle<T, d>, T, d>, T>;
				TArray<TSpatialAccelerationParams<TArray<TAccelerationStructureBuilder<T, d>>, TAccelerationStructureHandle<T, d>, T, d>> Buckets = { {AccelType::StaticType, MaxPayloadSize, 0, AABBMaxChildrenInLeaf, AABBMaxTreeDepth} };

				auto Structure = MakeUnique<AccelType>(Empty, Buckets[0].MaxChildrenInLeaf, Buckets[0].MaxTreeDepth, Buckets[0].MaxPayloadBounds);
				auto Collection = new TSpatialAccelerationCollection<TArray<TAccelerationStructureBuilder<T, d>>, AccelType>(MoveTemp(Buckets));
				Collection->AddSubstructure(MoveTemp(Structure), 0);
				return TUniquePtr<ISpatialAccelerationCollection<TAccelerationStructureHandle<T, d>, T, d>>(Collection);
			}
			else
			{
				using AccelType = TAABBTree<TAccelerationStructureHandle<T, d>, TAABBTreeLeafArray<TAccelerationStructureHandle<T, d>, T>, T>;
				using GridType = TBoundingVolume<TAccelerationStructureHandle<T, d>, T, d>;

				TArray<TSpatialAccelerationParams<TArray<TAccelerationStructureBuilder<T, d>>, TAccelerationStructureHandle<T, d>, T, d>> Buckets = {
					{AccelType::StaticType, MaxPayloadSize, 0, AABBMaxChildrenInLeaf, AABBMaxTreeDepth},
					{GridType::StaticType, MaxPayloadSize * 100, BoundingVolumeNumCells, AABBMaxChildrenInLeaf, AABBMaxTreeDepth},
				};

				auto Structure0 = MakeUnique<AccelType>(Empty, Buckets[0].MaxChildrenInLeaf, Buckets[0].MaxTreeDepth, Buckets[0].MaxPayloadBounds);

				auto Structure1 = MakeUnique<GridType>(Empty, false, 0, Buckets[1].MaxCells, Buckets[1].MaxPayloadBounds);
				auto Collection = new TSpatialAccelerationCollection<TArray<TAccelerationStructureBuilder<T, d>>, AccelType, GridType>(MoveTemp(Buckets));
				Collection->AddSubstructure(MoveTemp(Structure0), 0);
				Collection->AddSubstructure(MoveTemp(Structure1), 1);

				return TUniquePtr<ISpatialAccelerationCollection<TAccelerationStructureHandle<T, d>, T, d>>(Collection);
			}
		}

		virtual void Serialize(TUniquePtr<ISpatialAccelerationCollection<TAccelerationStructureHandle<T, d>, T, d>>& Ptr, FChaosArchive& Ar) override
		{
			if (Ar.IsLoading())
			{
				//todo: actually read in parameters
				Ptr = CreateEmptyCollection();
				Ptr->Serialize(Ar);
			}
			else
			{
				//todo: actually save out parameters
				Ptr->Serialize(Ar);
			}
		}
	};

	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	TPBDRigidsEvolutionBase<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::TPBDRigidsEvolutionBase(TPBDRigidsSOAs<T, d>& InParticles, int32 InNumIterations)
		: Particles(InParticles)
		, bExternalReady(false)
		, Clustering(static_cast<FPBDRigidsEvolution&>(*this), Particles.GetClusteredParticles())
		, NumIterations(InNumIterations)
		, SpatialCollectionFactory(new TDefaultCollectionFactory<T,d>())
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

	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	TPBDRigidsEvolutionBase<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::~TPBDRigidsEvolutionBase()
	{
		Particles.GetParticleHandles().RemoveArray(&PhysicsMaterials);
		Particles.GetParticleHandles().RemoveArray(&PerParticlePhysicsMaterials);
		Particles.GetParticleHandles().RemoveArray(&ParticleDisableCount);
		Particles.GetParticleHandles().RemoveArray(&Collided);
		WaitOnAccelerationStructure();
	}

	
	DECLARE_CYCLE_STAT(TEXT("CacheAccelerationBounds"), STAT_CacheAccelerationBounds, STATGROUP_Chaos);
	DECLARE_CYCLE_STAT(TEXT("ComputeIntermediateSpatialAcceleration"), STAT_ComputeIntermediateSpatialAcceleration, STATGROUP_Chaos);

	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	TPBDRigidsEvolutionBase<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::FChaosAccelerationStructureTask::FChaosAccelerationStructureTask(const TArray<FAccelerationStructBuilderCache>& CacheMap
		, TUniquePtr<FAccelerationStructure>& InAccelerationStructure
		, TUniquePtr<FAccelerationStructure>& InAccelerationStructureCopy)
		: BuilderCacheMap(CacheMap)
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

	template<typename T, int d>
	TUniquePtr<ISpatialAccelerationCollection<TAccelerationStructureHandle<T, d>, T, d>> CreateNewSpatialStructureFromSubStructure(TUniquePtr<ISpatialAcceleration<TAccelerationStructureHandle<T,d>, T,d>>&& Substructure)
	{
		using BVType = TBoundingVolume<TAccelerationStructureHandle<T, d>, T, d>;
		using AABBType = TAABBTree<TAccelerationStructureHandle<T, d>, TAABBTreeLeafArray<TAccelerationStructureHandle<T, d>, T>, T>;
		
		if (Substructure->template As<BVType>())
		{
			TArray<TSpatialAccelerationParams<TArray<TAccelerationStructureBuilder<T,d>>, TAccelerationStructureHandle<T, d>, T, d>> Buckets = { {BVType::StaticType } };
			auto Collection = MakeUnique<TSpatialAccelerationCollection<TArray<TAccelerationStructureBuilder<T, d>>, BVType>>(MoveTemp(Buckets));
			Collection->AddSubstructure(MoveTemp(Substructure), 0);
			return Collection;
		}
		else if (Substructure->template As<AABBType>())
		{
			TArray<TSpatialAccelerationParams<TArray<TAccelerationStructureBuilder<T,d>>, TAccelerationStructureHandle<T, d>, T, d>> Buckets = { {AABBType::StaticType } };
			auto Collection = MakeUnique<TSpatialAccelerationCollection<TArray<TAccelerationStructureBuilder<T, d>>, AABBType>>(MoveTemp(Buckets));
			Collection->AddSubstructure(MoveTemp(Substructure), 0);
			return Collection;
		}
		else
		{
			using AccelType = TAABBTree<TAccelerationStructureHandle<T, d>, TBoundingVolume<TAccelerationStructureHandle<T, d>, T, d>, T>;
			TArray<TSpatialAccelerationParams<TArray<TAccelerationStructureBuilder<T,d>>, TAccelerationStructureHandle<T, d>, T, d>> Buckets = { {AccelType::StaticType } };
			auto Collection = MakeUnique<TSpatialAccelerationCollection<TArray<TAccelerationStructureBuilder<T, d>>, AccelType>>(MoveTemp(Buckets));
			Collection->AddSubstructure(MoveTemp(Substructure), 0);
			return Collection;
		}
	}

	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	void TPBDRigidsEvolutionBase<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::FChaosAccelerationStructureTask::DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		//todo: no reason to do this in serial
		for (const FAccelerationStructBuilderCache& Cache : BuilderCacheMap)
		{
			auto OldStruct = AccelerationStructure->RemoveSubstructure(Cache.SpatialIdx);
			//This is a hack, need to have a Reinitialize API instead of downcasting
			using BVType = TBoundingVolume<TAccelerationStructureHandle<T, d>, T, d>;
			using AABBType = TAABBTree<TAccelerationStructureHandle<T, d>, TAABBTreeLeafArray<TAccelerationStructureHandle<T, d>, T>, T>;
			ISpatialAcceleration<TAccelerationStructureHandle<T,d>,T,d>* NewStruct;
			if (OldStruct->template As<BVType>())
			{
				NewStruct = new BVType(*Cache.CachedSpatialBuilderData);
			}
			else if (OldStruct->template As<AABBType>())
			{
				NewStruct = new AABBType(*Cache.CachedSpatialBuilderData);
			}
			else
			{
				using AccelType = TAABBTree<TAccelerationStructureHandle<T, d>, TBoundingVolume<TAccelerationStructureHandle<T, d>, T, d>, T>;
				NewStruct = new AccelType(*Cache.CachedSpatialBuilderData);
			}

			AccelerationStructure->AddSubstructure(TUniquePtr<ISpatialAcceleration<TAccelerationStructureHandle<T, d>, T,d>>(NewStruct), Cache.SpatialIdx.Bucket); //this assumes bucket size is 1, which will not be true in future. Need to fix all this
		}
		AccelerationStructureCopy = AsUniqueSpatialAccelerationChecked<FAccelerationStructure>(AccelerationStructure->Copy());
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
			AccelerationStructure.RemoveElementFrom(SpatialData.AccelerationHandle, SpatialData.DeletedSpatialIdx);

			if (bAsync)
			{
				if (int32* CacheIdxPtr = ParticleToCacheIdx.Find(Particle))
				{
					const auto SpatialIdx = SpatialData.DeletedSpatialIdx;
					auto MapFound = CachedSpatialBuilderDataMap.FindByPredicate([SpatialIdx](const auto& Cache) { return Cache.SpatialIdx == SpatialIdx; });
					TArray<TAccelerationStructureBuilder<T,d>>& CachedSpatialBuilderData = *(MapFound ? MapFound->CachedSpatialBuilderData : CachedSpatialBuilderDataMap.Last().CachedSpatialBuilderData);
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
			AccelerationStructure.UpdateElementIn(Particle, Particle->WorldSpaceInflatedBounds(), Particle->HasBounds(), SpatialData.UpdatedSpatialIdx);
			
			if (bAsync)
			{
				const auto SpatialIdx = SpatialData.UpdatedSpatialIdx;
				auto MapFound = CachedSpatialBuilderDataMap.FindByPredicate([SpatialIdx](const auto& Cache) { return Cache.SpatialIdx == SpatialIdx; });
				TArray<TAccelerationStructureBuilder<T,d>>& CachedSpatialBuilderData = *(MapFound ? MapFound->CachedSpatialBuilderData : CachedSpatialBuilderDataMap.Last().CachedSpatialBuilderData);

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
				CachedSpatialBuilderData[CacheIdx] = TAccelerationStructureBuilder<T,d>{ Particle->HasBounds(), Particle->WorldSpaceInflatedBounds(), SpatialData.AccelerationHandle };
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
			//initial frame so make empty structures

			InternalAcceleration = TUniquePtr<FAccelerationStructure>(SpatialCollectionFactory->CreateEmptyCollection());
			ScratchExternalAcceleration = TUniquePtr<FAccelerationStructure>(SpatialCollectionFactory->CreateEmptyCollection());
			AsyncInternalAcceleration = TUniquePtr<FAccelerationStructure>(SpatialCollectionFactory->CreateEmptyCollection());
			AsyncExternalAcceleration = TUniquePtr<FAccelerationStructure>(SpatialCollectionFactory->CreateEmptyCollection());
			FlushInternalAccelerationQueue();
			FlushExternalAccelerationQueue(*ScratchExternalAcceleration);
			bExternalReady = true;
			InitializeAccelerationCache();
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

			AccelerationStructureTaskComplete = TGraphTask<FChaosAccelerationStructureTask>::CreateTask().ConstructAndDispatchWhenReady(CachedSpatialBuilderDataMap, AsyncInternalAcceleration, AsyncExternalAcceleration);
		}
		else
		{
			FlushInternalAccelerationQueue();
		}
	}

	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	void TPBDRigidsEvolutionBase<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::InitializeAccelerationCache()
	{
		WaitOnAccelerationStructure();
		CachedSpatialBuilderDataMap.Empty();
		const auto SpatialIndices = InternalAcceleration->GetAllSpatialIndices();
		for (const auto& Idx : SpatialIndices)
		{
			CachedSpatialBuilderDataMap.Add(Idx);
		}
	}

	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	void TPBDRigidsEvolutionBase<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::UpdateExternalAccelerationStructure(TUniquePtr<ISpatialAccelerationCollection<TAccelerationStructureHandle<T, d>, T, d>>& StructToUpdate)
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

	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	void TPBDRigidsEvolutionBase<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::FlushSpatialAcceleration()
	{
		//force build acceleration structure with latest data
		ComputeIntermediateSpatialAcceleration(true);
		ComputeIntermediateSpatialAcceleration(true);	//having to do it multiple times because of the various caching involved over multiple frames.
		ComputeIntermediateSpatialAcceleration(true);
	}

	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	void TPBDRigidsEvolutionBase<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::Serialize(FChaosArchive& Ar)
	{
		Particles.Serialize(Ar);

		Ar.UsingCustomVersion(FExternalPhysicsCustomObjectVersion::GUID);
		if (Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) >= FExternalPhysicsCustomObjectVersion::SerializeEvolutionBV)
		{
			if (Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) < FExternalPhysicsCustomObjectVersion::SerializeMultiStructures)
			{
				//old path assumes single sub-structure
				TUniquePtr<ISpatialAcceleration<TAccelerationStructureHandle<T, d>, T, d>> SubStructure;
				if (!Ar.IsLoading())
				{
					SubStructure = InternalAcceleration->RemoveSubstructure(FSpatialAccelerationIdx{ 0,0 });
					Ar << SubStructure;
					InternalAcceleration->AddSubstructure(MoveTemp(SubStructure), 0);
				}
				else
				{
					Ar << SubStructure;
					InternalAcceleration = CreateNewSpatialStructureFromSubStructure(MoveTemp(SubStructure));
					InitializeAccelerationCache();
				}
			}
			else
			{
				SpatialCollectionFactory->Serialize(InternalAcceleration, Ar);
			}

			SerializePendingMap(Ar, InternalAccelerationQueue);
			SerializePendingMap(Ar, AsyncAccelerationQueue);
			SerializePendingMap(Ar, ExternalAccelerationQueue);

			ScratchExternalAcceleration = AsUniqueSpatialAccelerationChecked<FAccelerationStructure>(InternalAcceleration->Copy());
		}
		else if (Ar.IsLoading())
		{
			AccelerationStructureTaskComplete = nullptr;
			for (auto& Particle : Particles.GetNonDisabledView())
			{
				Particle.SetSpatialIdx(FSpatialAccelerationIdx{ 0,0 });
				DirtyParticle(Particle);
			}

			FlushSpatialAcceleration();
		}
	}
}

#ifdef __clang__
template class CHAOS_API Chaos::TPBDRigidsEvolutionBase<Chaos::TPBDRigidsEvolutionGBF<float, 3>, Chaos::TPBDCollisionConstraint<float,3>, float, 3>;
#else
template class Chaos::TPBDRigidsEvolutionBase<Chaos::TPBDRigidsEvolutionGBF<float, 3>, Chaos::TPBDCollisionConstraint<float,3>, float, 3>;
#endif

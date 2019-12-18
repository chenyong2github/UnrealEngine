// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "Chaos/PBDRigidsEvolution.h"
#include "Chaos/Defines.h"
#include "Chaos/PBDRigidsSOAs.h"
#include "ChaosStats.h"
#include "Chaos/PBDRigidsEvolutionGBF.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/SpatialAccelerationCollection.h"

namespace Chaos
{
	struct FAccelerationConfig
	{
		int32 BroadphaseType;
		int32 BVNumCells;
		int32 MaxChildrenInLeaf;
		int32 MaxTreeDepth;
		int32 AABBMaxChildrenInLeaf;
		int32 AABBMaxTreeDepth;
		float MaxPayloadSize;
		int32 IterationsPerTimeSlice;

		FAccelerationConfig()
		{
			BroadphaseType = 3;
			BVNumCells = 35;
			MaxChildrenInLeaf = 5;
			MaxTreeDepth = 200;
			AABBMaxChildrenInLeaf = 500;
			AABBMaxTreeDepth = 200;
			MaxPayloadSize = 100000;
			IterationsPerTimeSlice = 40000;
		}
	} ConfigSettings;

	FAutoConsoleVariableRef CVarBroadphaseIsTree(TEXT("p.BroadphaseType"), ConfigSettings.BroadphaseType, TEXT(""));
	FAutoConsoleVariableRef CVarBoundingVolumeNumCells(TEXT("p.BoundingVolumeNumCells"), ConfigSettings.BVNumCells, TEXT(""));
	FAutoConsoleVariableRef CVarMaxChildrenInLeaf(TEXT("p.MaxChildrenInLeaf"), ConfigSettings.MaxChildrenInLeaf, TEXT(""));
	FAutoConsoleVariableRef CVarMaxTreeDepth(TEXT("p.MaxTreeDepth"), ConfigSettings.MaxTreeDepth, TEXT(""));
	FAutoConsoleVariableRef CVarAABBMaxChildrenInLeaf(TEXT("p.AABBMaxChildrenInLeaf"), ConfigSettings.AABBMaxChildrenInLeaf, TEXT(""));
	FAutoConsoleVariableRef CVarAABBMaxTreeDepth(TEXT("p.AABBMaxTreeDepth"), ConfigSettings.AABBMaxTreeDepth, TEXT(""));
	FAutoConsoleVariableRef CVarMaxPayloadSize(TEXT("p.MaxPayloadSize"), ConfigSettings.MaxPayloadSize, TEXT(""));
	FAutoConsoleVariableRef CVarIterationsPerTimeSlice(TEXT("p.IterationsPerTimeSlice"), ConfigSettings.IterationsPerTimeSlice, TEXT(""));

	template<typename T, int d>
	struct TDefaultCollectionFactory : public ISpatialAccelerationCollectionFactory<T, d>
	{
		FAccelerationConfig Config;

		using BVType = TBoundingVolume<TAccelerationStructureHandle<T, d>, T, d>;
		using AABBTreeType = TAABBTree<TAccelerationStructureHandle<T, d>, TAABBTreeLeafArray<TAccelerationStructureHandle<T, d>, T>, T>;
		using AABBTreeOfGridsType = TAABBTree<TAccelerationStructureHandle<T, d>, TBoundingVolume<TAccelerationStructureHandle<T, d>, T, d>, T>;

		TUniquePtr<ISpatialAccelerationCollection<TAccelerationStructureHandle<T, d>, T, d>> CreateEmptyCollection() override
		{
			TConstParticleView<TSpatialAccelerationCache<T, d>> Empty;

			const uint16 NumBuckets = ConfigSettings.BroadphaseType >= 3 ? 2 : 1;
			auto Collection = new TSpatialAccelerationCollection<AABBTreeType, BVType, AABBTreeOfGridsType>();

			for (uint16 BucketIdx = 0; BucketIdx < NumBuckets; ++BucketIdx)
			{
				Collection->AddSubstructure(CreateAccelerationPerBucket_Threaded(Empty, BucketIdx, true), BucketIdx);
			}

			return TUniquePtr<ISpatialAccelerationCollection<TAccelerationStructureHandle<T, d>, T, d>>(Collection);
		}

		virtual uint8 GetActiveBucketsMask() const
		{
			return ConfigSettings.BroadphaseType >= 3 ? 3 : 1;
		}

		virtual TUniquePtr<ISpatialAcceleration<TAccelerationStructureHandle<T, d>, T, d>> CreateAccelerationPerBucket_Threaded(const TConstParticleView<TSpatialAccelerationCache<T, d>>& Particles, uint16 BucketIdx, bool ForceFullBuild) override
		{
			switch (BucketIdx)
			{
			case 0:
			{
				if (ConfigSettings.BroadphaseType == 0)
				{
					return MakeUnique<BVType>(Particles, false, 0, ConfigSettings.BVNumCells, ConfigSettings.MaxPayloadSize);
				}
				else if (ConfigSettings.BroadphaseType == 1 || ConfigSettings.BroadphaseType == 3)
				{
					return MakeUnique<AABBTreeType>(Particles, ConfigSettings.MaxChildrenInLeaf, ConfigSettings.MaxTreeDepth, ConfigSettings.MaxPayloadSize, ForceFullBuild ? 0 : ConfigSettings.IterationsPerTimeSlice);
				}
				else if (ConfigSettings.BroadphaseType == 4 || ConfigSettings.BroadphaseType == 2)
				{
					return MakeUnique<AABBTreeOfGridsType>(Particles, ConfigSettings.AABBMaxChildrenInLeaf, ConfigSettings.AABBMaxTreeDepth, ConfigSettings.MaxPayloadSize);
				}
			}
			case 1:
			{
				ensure(ConfigSettings.BroadphaseType == 3 || ConfigSettings.BroadphaseType == 4);
				return MakeUnique<BVType>(Particles, false, 0, ConfigSettings.BVNumCells, ConfigSettings.MaxPayloadSize);
			}
			default:
			{
				check(false);
				return nullptr;
			}
			}
		}

		virtual void Serialize(TUniquePtr<ISpatialAccelerationCollection<TAccelerationStructureHandle<T, d>, T, d>>& Ptr, FChaosArchive& Ar) override
		{
			if (Ar.IsLoading())
			{
				Ptr = CreateEmptyCollection();
				Ptr->Serialize(Ar);
			}
			else
			{
				Ptr->Serialize(Ar);
			}
		}
	};

	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	TPBDRigidsEvolutionBase<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::TPBDRigidsEvolutionBase(TPBDRigidsSOAs<T, d>& InParticles, int32 InNumIterations, int32 InNumPushOutIterations, bool InIsSingleThreaded)
		: Particles(InParticles)
		, bExternalReady(false)
		, bIsSingleThreaded(InIsSingleThreaded)
		, Clustering(static_cast<FPBDRigidsEvolution&>(*this), Particles.GetClusteredParticles())
		, NumIterations(InNumIterations)
		, NumPushOutIterations(InNumPushOutIterations)
		, SpatialCollectionFactory(new TDefaultCollectionFactory<T, d>())
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
	DECLARE_CYCLE_STAT(TEXT("CopyAccelerationStructure"), STAT_CopyAccelerationStructure, STATGROUP_Chaos);
	DECLARE_CYCLE_STAT(TEXT("SwapAccelerationStructures"), STAT_SwapAccelerationStructures, STATGROUP_Chaos);
	DECLARE_CYCLE_STAT(TEXT("AccelerationStructureTimeSlice"), STAT_AccelerationStructureTimeSlice, STATGROUP_Chaos);
	DECLARE_CYCLE_STAT(TEXT("CreateInitialAccelerationStructure"), STAT_CreateInitialAccelerationStructure, STATGROUP_Chaos);


	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	TPBDRigidsEvolutionBase<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::FChaosAccelerationStructureTask::FChaosAccelerationStructureTask(
		ISpatialAccelerationCollectionFactory<T, d>& InSpatialCollectionFactory
		, const TMap<FSpatialAccelerationIdx, TUniquePtr<TSpatialAccelerationCache<T, d>>>& InSpatialAccelerationCache
		, TUniquePtr<FAccelerationStructure>& InAccelerationStructure
		, TUniquePtr<FAccelerationStructure>& InAccelerationStructureCopy
		, bool InForceFullBuild
		, bool InIsSingleThreaded)
		: SpatialCollectionFactory(InSpatialCollectionFactory)
		, SpatialAccelerationCache(InSpatialAccelerationCache)
		, AccelerationStructure(InAccelerationStructure)
		, AccelerationStructureCopy(InAccelerationStructureCopy)
		, IsForceFullBuild(InForceFullBuild)
		, bIsSingleThreaded(InIsSingleThreaded)
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
	TUniquePtr<ISpatialAccelerationCollection<TAccelerationStructureHandle<T, d>, T, d>> CreateNewSpatialStructureFromSubStructure(TUniquePtr<ISpatialAcceleration<TAccelerationStructureHandle<T, d>, T, d>>&& Substructure)
	{
		using BVType = TBoundingVolume<TAccelerationStructureHandle<T, d>, T, d>;
		using AABBType = TAABBTree<TAccelerationStructureHandle<T, d>, TAABBTreeLeafArray<TAccelerationStructureHandle<T, d>, T>, T>;

		if (Substructure->template As<BVType>())
		{
			auto Collection = MakeUnique<TSpatialAccelerationCollection<BVType>>();
			Collection->AddSubstructure(MoveTemp(Substructure), 0);
			return Collection;
		}
		else if (Substructure->template As<AABBType>())
		{
			auto Collection = MakeUnique<TSpatialAccelerationCollection<AABBType>>();
			Collection->AddSubstructure(MoveTemp(Substructure), 0);
			return Collection;
		}
		else
		{
			using AccelType = TAABBTree<TAccelerationStructureHandle<T, d>, TBoundingVolume<TAccelerationStructureHandle<T, d>, T, d>, T>;
			auto Collection = MakeUnique<TSpatialAccelerationCollection<AccelType>>();
			Collection->AddSubstructure(MoveTemp(Substructure), 0);
			return Collection;
		}
	}

	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	void TPBDRigidsEvolutionBase<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::FChaosAccelerationStructureTask::DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		LLM_SCOPE(ELLMTag::Chaos);

		uint8 ActiveBucketsMask = SpatialCollectionFactory.GetActiveBucketsMask();
		TArray<TSOAView<TSpatialAccelerationCache<T, d>>> ViewsPerBucket[8];

		bool IsTimeSlicingProgressing = false;

		//merge buckets. todo: support multiple entries per bucket (i.e. dynamic vs static)
		for (const auto& Itr : SpatialAccelerationCache)
		{
			const FSpatialAccelerationIdx SpatialIdx = Itr.Key;
			const TSpatialAccelerationCache<T, d>& Cache = *Itr.Value;
			const uint8 BucketIdx = (1 << SpatialIdx.Bucket) & ActiveBucketsMask ? SpatialIdx.Bucket : 0;
			if (AccelerationStructure->GetSubstructure(SpatialIdx) && !AccelerationStructure->GetSubstructure(SpatialIdx)->IsAsyncTimeSlicingComplete())
			{
				SCOPE_CYCLE_COUNTER(STAT_AccelerationStructureTimeSlice);

				AccelerationStructure->GetSubstructure(SpatialIdx)->ProgressAsyncTimeSlicing(IsForceFullBuild);

				// is it still progressing or now complete
				IsTimeSlicingProgressing = !AccelerationStructure->GetSubstructure(SpatialIdx)->IsAsyncTimeSlicingComplete();
			}
			else
			{
				ViewsPerBucket[BucketIdx].Add(const_cast<TSpatialAccelerationCache<T, d>*>(&Cache));
				if (AccelerationStructure->IsBucketActive(SpatialIdx.Bucket))
				{
					AccelerationStructure->RemoveSubstructure(SpatialIdx);
				}
			}
		}

		//todo: creation can go wide, insertion to collection cannot
		for (uint8 BucketIdx = 0; BucketIdx < 8; ++BucketIdx)
		{
			if (ViewsPerBucket[BucketIdx].Num())
			{
				SCOPE_CYCLE_COUNTER(STAT_CreateInitialAccelerationStructure);

				auto ParticleView = MakeConstParticleView(MoveTemp(ViewsPerBucket[BucketIdx]));
				auto NewStruct = SpatialCollectionFactory.CreateAccelerationPerBucket_Threaded(ParticleView, BucketIdx, IsForceFullBuild);

				// we kicked of the creation of a new structure and it's going to time-slice the work
				if (!NewStruct->IsAsyncTimeSlicingComplete())
				{
					IsTimeSlicingProgressing = true;
				}

				AccelerationStructure->AddSubstructure(MoveTemp(NewStruct), BucketIdx);

			}
		}

		AccelerationStructure->SetAllAsyncTrasksComplete(!IsTimeSlicingProgressing);

		// If it's not progressing then it is finished so we can perform the final copy if required
		if (!IsTimeSlicingProgressing)
		{
			if (!bIsSingleThreaded)
			{
			// This operation is slow!
			SCOPE_CYCLE_COUNTER(STAT_CopyAccelerationStructure);
			AccelerationStructureCopy = AsUniqueSpatialAccelerationChecked<FAccelerationStructure>(AccelerationStructure->Copy());
		}
	}
	}

	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	void TPBDRigidsEvolutionBase<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::ApplyParticlePendingData(const FPendingSpatialData& SpatialData, FAccelerationStructure& AccelerationStructure, bool bUpdateCache)
	{
		//Note: we collapsed several update delete events into one struct. If memory is reused this can lead to problems
		//Luckily there are only 3 states we care about:
		//While pending we updated an object several times, this collapses into one update
		//While pending we may have updated an object, we may have also created and destroyed the object, but the final event is a delete, so just remove from acceleration structure
		//While pending we destroyed, recreated using the same memory address, and then did an update. In this case we should remove first and then update as global bounds may have changed
		//As long as we delete first and update second this will be respected

		if (SpatialData.bDelete)
		{
			AccelerationStructure.RemoveElementFrom(SpatialData.DeleteAccelerationHandle, SpatialData.DeletedSpatialIdx);
			TGeometryParticleHandle<T, d>* DeleteParticle = SpatialData.DeleteAccelerationHandle.GetGeometryParticleHandle_PhysicsThread();

			if (bUpdateCache)
			{
				if (uint32* InnerIdxPtr = ParticleToCacheInnerIdx.Find(DeleteParticle))
				{
					const auto SpatialIdx = SpatialData.DeletedSpatialIdx;
					TSpatialAccelerationCache<T, d>& Cache = *SpatialAccelerationCache.FindChecked(SpatialIdx);	//can't delete from cache that doesn't exist
					const uint32 CacheInnerIdx = *InnerIdxPtr;
					if (CacheInnerIdx + 1 < Cache.Size())	//will get swapped with last element, so update it
					{
						TGeometryParticleHandle<T, d>* LastParticleInCache = Cache.Payload(Cache.Size() - 1).GetGeometryParticleHandle_PhysicsThread();
						ParticleToCacheInnerIdx.FindChecked(LastParticleInCache) = CacheInnerIdx;
					}

					Cache.DestroyElement(CacheInnerIdx);
					ParticleToCacheInnerIdx.Remove(DeleteParticle);
				}
			}
		}

		if (SpatialData.bUpdate)
		{

			TGeometryParticleHandle<T, d>* UpdateParticle = SpatialData.UpdateAccelerationHandle.GetGeometryParticleHandle_PhysicsThread();

			AccelerationStructure.UpdateElementIn(UpdateParticle, UpdateParticle->WorldSpaceInflatedBounds(), UpdateParticle->HasBounds(), SpatialData.UpdatedSpatialIdx);

			if (bUpdateCache)
			{
				TUniquePtr<TSpatialAccelerationCache<T, d>>* CachePtrPtr = SpatialAccelerationCache.Find(SpatialData.UpdatedSpatialIdx);
				if (CachePtrPtr == nullptr)
				{
					CachePtrPtr = &SpatialAccelerationCache.Add(SpatialData.UpdatedSpatialIdx, TUniquePtr<TSpatialAccelerationCache<T, d>>(new TSpatialAccelerationCache<T, d>));
				}

				TSpatialAccelerationCache<T, d>& Cache = **CachePtrPtr;

				//make sure in mapping
				uint32 CacheInnerIdx;
				if (uint32* CacheInnerIdxPtr = ParticleToCacheInnerIdx.Find(UpdateParticle))
				{
					CacheInnerIdx = *CacheInnerIdxPtr;
				}
				else
				{
					CacheInnerIdx = Cache.Size();
					Cache.AddElements(1);
					ParticleToCacheInnerIdx.Add(UpdateParticle, CacheInnerIdx);
				}

				//update cache entry
				Cache.HasBounds(CacheInnerIdx) = UpdateParticle->HasBounds();
				Cache.Bounds(CacheInnerIdx) = UpdateParticle->WorldSpaceInflatedBounds();
				Cache.Payload(CacheInnerIdx) = SpatialData.UpdateAccelerationHandle;
			}
		}
	}

	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	void TPBDRigidsEvolutionBase<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::FlushInternalAccelerationQueue()
	{
		for (auto Itr : InternalAccelerationQueue)
		{
			ApplyParticlePendingData(Itr.Value, *InternalAcceleration, false);
		}
		InternalAccelerationQueue.Empty();
	}

	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	void TPBDRigidsEvolutionBase<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::FlushAsyncAccelerationQueue()
	{
		for (auto Itr : AsyncAccelerationQueue)
		{
			ApplyParticlePendingData(Itr.Value, *AsyncInternalAcceleration, true); //only the first queue needs to update the cached acceleration
			if (!bIsSingleThreaded)
			{
			ApplyParticlePendingData(Itr.Value, *AsyncExternalAcceleration, false);
		}
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
			ApplyParticlePendingData(Itr.Value, Acceleration, false);
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

		bool ForceFullBuild = InternalAccelerationQueue.Num() > 1000;

		if (!AccelerationStructureTaskComplete)
		{
			//initial frame so make empty structures

			InternalAcceleration = TUniquePtr<FAccelerationStructure>(SpatialCollectionFactory->CreateEmptyCollection());
			AsyncInternalAcceleration = TUniquePtr<FAccelerationStructure>(SpatialCollectionFactory->CreateEmptyCollection());
			if (!bIsSingleThreaded)
			{
			ScratchExternalAcceleration = TUniquePtr<FAccelerationStructure>(SpatialCollectionFactory->CreateEmptyCollection());
			AsyncExternalAcceleration = TUniquePtr<FAccelerationStructure>(SpatialCollectionFactory->CreateEmptyCollection());
			}
			FlushInternalAccelerationQueue();

			if (!bIsSingleThreaded)
			{
			FlushExternalAccelerationQueue(*ScratchExternalAcceleration);
			bExternalReady = true;
		}
		}

		if (bBlock)
		{
			WaitOnAccelerationStructure();
		}

		bool AsyncComplete = !AccelerationStructureTaskComplete || AccelerationStructureTaskComplete->IsComplete();

		if (AsyncComplete)
		{
			// only copy when the acceleration structures have completed time-slicing
			if (AccelerationStructureTaskComplete && AsyncInternalAcceleration->IsAllAsyncTrasksComplete())
			{
				SCOPE_CYCLE_COUNTER(STAT_SwapAccelerationStructures);

				check(AsyncInternalAcceleration->IsAllAsyncTrasksComplete());

				FlushAsyncAccelerationQueue();

				//swap acceleration structure for new one
				std::swap(InternalAcceleration, AsyncInternalAcceleration);	//swap to avoid free on sync part as this can be expensive

				if (!bIsSingleThreaded)
				{
				std::swap(ScratchExternalAcceleration, AsyncExternalAcceleration);
				}
				bExternalReady = true;
			}

			// we run the task for both starting a new accel structure as well as for the timeslicing
			// we run the task for both starting a new accel structure as well as for the timeslicing
			AccelerationStructureTaskComplete = TGraphTask<FChaosAccelerationStructureTask>::CreateTask().ConstructAndDispatchWhenReady(*SpatialCollectionFactory, SpatialAccelerationCache, AsyncInternalAcceleration, AsyncExternalAcceleration, ForceFullBuild, bIsSingleThreaded);

		}
		else
		{
			FlushInternalAccelerationQueue();
		}
	}

	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	void TPBDRigidsEvolutionBase<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::UpdateExternalAccelerationStructure(TUniquePtr<ISpatialAccelerationCollection<TAccelerationStructureHandle<T, d>, T, d>>& StructToUpdate)
	{
		DECLARE_SCOPE_CYCLE_COUNTER(TEXT("CreateExternalAccelerationStructure"), STAT_CreateExternalAccelerationStructure, STATGROUP_Physics);

		check(!bIsSingleThreaded);

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
	void TPBDRigidsEvolutionBase<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::RebuildSpatialAccelerationForPerfTest()
	{
		WaitOnAccelerationStructure();

		ParticleToCacheInnerIdx.Empty();
		AsyncAccelerationQueue.Empty();
		InternalAccelerationQueue.Empty();
		ExternalAccelerationQueue.Empty();

		AccelerationStructureTaskComplete = nullptr;
		const auto& NonDisabled = Particles.GetNonDisabledView();
		for (auto& Particle : NonDisabled)
		{
			DirtyParticle(Particle);
		}

		FlushSpatialAcceleration();
	}

	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	void TPBDRigidsEvolutionBase<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::Serialize(FChaosArchive& Ar)
	{
		int32 DefaultBroadphaseType = ConfigSettings.BroadphaseType;

		Ar.UsingCustomVersion(FExternalPhysicsCustomObjectVersion::GUID);
		if (Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) >= FExternalPhysicsCustomObjectVersion::SerializeBroadphaseType)
		{
			Ar << ConfigSettings.BroadphaseType;
		}
		else
		{
			//older archives just assume type 3
			ConfigSettings.BroadphaseType = 3;
		}

		Particles.Serialize(Ar);

		if (Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) >= FExternalPhysicsCustomObjectVersion::SerializeEvolutionBV)
		{
			if (Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) >= FExternalPhysicsCustomObjectVersion::FlushEvolutionInternalAccelerationQueue)
			{
				FlushInternalAccelerationQueue();
			}

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
				}
			}
			else
			{
				SpatialCollectionFactory->Serialize(InternalAcceleration, Ar);
			}

			if (Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) < FExternalPhysicsCustomObjectVersion::FlushEvolutionInternalAccelerationQueue)
			{
				SerializePendingMap(Ar, InternalAccelerationQueue);
				SerializePendingMap(Ar, AsyncAccelerationQueue);
				SerializePendingMap(Ar, ExternalAccelerationQueue);
			}

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

		ConfigSettings.BroadphaseType = DefaultBroadphaseType;

	}
}

#ifdef __clang__
template class CHAOS_API Chaos::TPBDRigidsEvolutionBase<Chaos::TPBDRigidsEvolutionGBF<float, 3>, Chaos::TPBDCollisionConstraints<float,3>, float, 3>;
#else
template class Chaos::TPBDRigidsEvolutionBase<Chaos::TPBDRigidsEvolutionGBF<float, 3>, Chaos::TPBDCollisionConstraints<float,3>, float, 3>;
#endif

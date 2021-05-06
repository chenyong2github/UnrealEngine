// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/PBDRigidsEvolution.h"
#include "Chaos/Defines.h"
#include "Chaos/PBDRigidsSOAs.h"
#include "ChaosStats.h"
#include "Chaos/PBDRigidsEvolutionGBF.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/SpatialAccelerationCollection.h"

int32 ChaosRigidsEvolutionApplyAllowEarlyOutCVar = 1;
FAutoConsoleVariableRef CVarChaosRigidsEvolutionApplyAllowEarlyOut(TEXT("p.ChaosRigidsEvolutionApplyAllowEarlyOut"), ChaosRigidsEvolutionApplyAllowEarlyOutCVar, TEXT("Allow Chaos Rigids Evolution apply iterations to early out when resolved.[def:1]"));

int32 ChaosRigidsEvolutionApplyPushoutAllowEarlyOutCVar = 1;
FAutoConsoleVariableRef CVarChaosRigidsEvolutionApplyPushoutAllowEarlyOut(TEXT("p.ChaosRigidsEvolutionApplyPushoutAllowEarlyOut"), ChaosRigidsEvolutionApplyPushoutAllowEarlyOutCVar, TEXT("Allow Chaos Rigids Evolution apply-pushout iterations to early out when resolved.[def:1]"));

int32 ChaosNumPushOutIterationsOverride = -1;
FAutoConsoleVariableRef CVarChaosNumPushOutIterationsOverride(TEXT("p.ChaosNumPushOutIterationsOverride"), ChaosNumPushOutIterationsOverride, TEXT("Override for num push out iterations if >= 0 [def:-1]"));

int32 ChaosNumContactIterationsOverride = -1;
FAutoConsoleVariableRef CVarChaosNumContactIterationsOverride(TEXT("p.ChaosNumContactIterationsOverride"), ChaosNumContactIterationsOverride, TEXT("Override for num contact iterations if >= 0. [def:-1]"));

namespace Chaos
{
	CHAOS_API int32 FixBadAccelerationStructureRemoval = 1;
	FAutoConsoleVariableRef CVarFixBadAccelerationStructureRemoval(TEXT("p.FixBadAccelerationStructureRemoval"), FixBadAccelerationStructureRemoval, TEXT(""));

	struct FAccelerationConfig
	{
		int32 BroadphaseType;
		int32 BVNumCells;
		int32 MaxChildrenInLeaf;
		int32 MaxTreeDepth;
		int32 AABBMaxChildrenInLeaf;
		int32 AABBMaxTreeDepth;
		FReal MaxPayloadSize;
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
			IterationsPerTimeSlice = 4000;
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

	struct FDefaultCollectionFactory : public ISpatialAccelerationCollectionFactory
	{
		FAccelerationConfig Config;

		using BVType = TBoundingVolume<FAccelerationStructureHandle>;
		using AABBTreeType = TAABBTree<FAccelerationStructureHandle, TAABBTreeLeafArray<FAccelerationStructureHandle>>;
		using AABBTreeOfGridsType = TAABBTree<FAccelerationStructureHandle, TBoundingVolume<FAccelerationStructureHandle>>;

		TUniquePtr<ISpatialAccelerationCollection<FAccelerationStructureHandle, FReal, 3>> CreateEmptyCollection() override
		{
			TConstParticleView<FSpatialAccelerationCache> Empty;

			const uint16 NumBuckets = ConfigSettings.BroadphaseType >= 3 ? 2 : 1;
			auto Collection = new TSpatialAccelerationCollection<AABBTreeType, BVType, AABBTreeOfGridsType>();

			for (uint16 BucketIdx = 0; BucketIdx < NumBuckets; ++BucketIdx)
			{
				Collection->AddSubstructure(CreateAccelerationPerBucket_Threaded(Empty, BucketIdx, true), BucketIdx);
			}

			return TUniquePtr<ISpatialAccelerationCollection<FAccelerationStructureHandle, FReal, 3>>(Collection);
		}

		virtual uint8 GetActiveBucketsMask() const
		{
			return ConfigSettings.BroadphaseType >= 3 ? 3 : 1;
		}

		virtual bool IsBucketTimeSliced(uint16 BucketIdx) const
		{
			// TODO: Unduplicate switch statement here with CreateAccelerationPerBucket_Threaded and refactor so that bucket index mapping is better.
			switch (BucketIdx)
			{
			case 0:
			{
				if (ConfigSettings.BroadphaseType == 0)
				{
					// BVType
					return false;
				}
				else if (ConfigSettings.BroadphaseType == 1 || ConfigSettings.BroadphaseType == 3)
				{
					// AABBTreeType
					return true;
				}
				else if (ConfigSettings.BroadphaseType == 4 || ConfigSettings.BroadphaseType == 2)
				{
					// AABBTreeOfGridsType
					return true;
				}
			}
			case 1:
			{
				// BVType
				ensure(ConfigSettings.BroadphaseType == 3 || ConfigSettings.BroadphaseType == 4);
				return false;
			}
			default:
			{
				check(false);
				return false;
			}
			}
		}

		virtual TUniquePtr<ISpatialAcceleration<FAccelerationStructureHandle, FReal, 3>> CreateAccelerationPerBucket_Threaded(const TConstParticleView<FSpatialAccelerationCache>& Particles, uint16 BucketIdx, bool ForceFullBuild) override
		{
			// TODO: Unduplicate switch statement here with IsBucketTimeSliced and refactor so that bucket index mapping is better.
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

		virtual void Serialize(TUniquePtr<ISpatialAccelerationCollection<FAccelerationStructureHandle, FReal, 3>>& Ptr, FChaosArchive& Ar) override
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

	FPBDRigidsEvolutionBase::FPBDRigidsEvolutionBase(FPBDRigidsSOAs& InParticles, THandleArray<FChaosPhysicsMaterial>& InSolverPhysicsMaterials, int32 InNumIterations, int32 InNumPushOutIterations, bool InIsSingleThreaded)
	    : Particles(InParticles)
		, SolverPhysicsMaterials(InSolverPhysicsMaterials)
		, InternalAcceleration(nullptr)
		, AsyncInternalAcceleration(nullptr)
		, AsyncExternalAcceleration(nullptr)
		, bIsSingleThreaded(InIsSingleThreaded)
		, bCanStartAsyncTasks(true)
		, LatestExternalTimestampConsumed_Internal(-1)
		, NumIterations(InNumIterations)
		, NumPushOutIterations(InNumPushOutIterations)
		, SpatialCollectionFactory(new FDefaultCollectionFactory())
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

	FPBDRigidsEvolutionBase::~FPBDRigidsEvolutionBase()
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
	DECLARE_CYCLE_STAT(TEXT("CreateNonSlicedStructures"), STAT_CreateNonSlicedStructures, STATGROUP_Chaos);

	FPBDRigidsEvolutionBase::FChaosAccelerationStructureTask::FChaosAccelerationStructureTask(
		ISpatialAccelerationCollectionFactory& InSpatialCollectionFactory
		, const TMap<FSpatialAccelerationIdx, TUniquePtr<FSpatialAccelerationCache>>& InSpatialAccelerationCache
		, FAccelerationStructure* InInternalAccelerationStructure
		, FAccelerationStructure* InExternalAccelerationStructure
		, bool InForceFullBuild
		, bool InIsSingleThreaded
		, bool InNeedsReset)
		: SpatialCollectionFactory(InSpatialCollectionFactory)
		, SpatialAccelerationCache(InSpatialAccelerationCache)
		, InternalStructure(InInternalAccelerationStructure)
		, ExternalStructure(InExternalAccelerationStructure)
		, IsForceFullBuild(InForceFullBuild)
		, bIsSingleThreaded(InIsSingleThreaded)
		, bNeedsReset(InNeedsReset)
	{

	}

	TStatId FPBDRigidsEvolutionBase::FChaosAccelerationStructureTask::GetStatId()
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FChaosAccelerationStructureTask, STATGROUP_Chaos);
	}

	ENamedThreads::Type FPBDRigidsEvolutionBase::FChaosAccelerationStructureTask::GetDesiredThread()
	{
		return ENamedThreads::AnyBackgroundThreadNormalTask;
	}

	ESubsequentsMode::Type FPBDRigidsEvolutionBase::FChaosAccelerationStructureTask::GetSubsequentsMode()
	{
		return ESubsequentsMode::TrackSubsequents;
	}

	TUniquePtr<ISpatialAccelerationCollection<FAccelerationStructureHandle, FReal, 3>> CreateNewSpatialStructureFromSubStructure(TUniquePtr<ISpatialAcceleration<FAccelerationStructureHandle, FReal, 3>>&& Substructure)
	{
		using BVType = TBoundingVolume<FAccelerationStructureHandle>;
		using AABBType = TAABBTree<FAccelerationStructureHandle, TAABBTreeLeafArray<FAccelerationStructureHandle>>;

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
			using AccelType = TAABBTree<FAccelerationStructureHandle, TBoundingVolume<FAccelerationStructureHandle>>;
			auto Collection = MakeUnique<TSpatialAccelerationCollection<AccelType>>();
			Collection->AddSubstructure(MoveTemp(Substructure), 0);
			return Collection;
		}
	}

	void FPBDRigidsEvolutionBase::FChaosAccelerationStructureTask::UpdateStructure(FAccelerationStructure* AccelerationStructure)
	{
		LLM_SCOPE(ELLMTag::ChaosAcceleration);

		if (bNeedsReset)
		{
			AccelerationStructure->Reset();
		}

		uint8 ActiveBucketsMask = SpatialCollectionFactory.GetActiveBucketsMask();
		TArray<TSOAView<FSpatialAccelerationCache>> ViewsPerBucket[FSpatialAccelerationIdx::MaxBuckets];
		TArray<uint8> TimeSlicedBucketsToCreate;
		TArray<uint8> NonTimeSlicedBucketsToCreate;

		bool IsTimeSlicingProgressing = false;

		//merge buckets. todo: support multiple entries per bucket (i.e. dynamic vs static)
		for(const auto& Itr : SpatialAccelerationCache)
		{
			const FSpatialAccelerationIdx SpatialIdx = Itr.Key;
			const FSpatialAccelerationCache& Cache = *Itr.Value;
			const uint8 BucketIdx = (1 << SpatialIdx.Bucket) & ActiveBucketsMask ? SpatialIdx.Bucket : 0;
			if(AccelerationStructure->GetSubstructure(SpatialIdx) && !AccelerationStructure->GetSubstructure(SpatialIdx)->IsAsyncTimeSlicingComplete())
			{
				SCOPE_CYCLE_COUNTER(STAT_AccelerationStructureTimeSlice);

				AccelerationStructure->GetSubstructure(SpatialIdx)->ProgressAsyncTimeSlicing(IsForceFullBuild);

				// is it still progressing or now complete
				if(!AccelerationStructure->GetSubstructure(SpatialIdx)->IsAsyncTimeSlicingComplete())
				{
					IsTimeSlicingProgressing = true;
				}
			}
			else
			{
				ViewsPerBucket[BucketIdx].Add(const_cast<FSpatialAccelerationCache*>(&Cache));
				if(AccelerationStructure->IsBucketActive(SpatialIdx.Bucket))
				{
					AccelerationStructure->RemoveSubstructure(SpatialIdx);
				}

				if(SpatialCollectionFactory.IsBucketTimeSliced(BucketIdx))
				{
					TimeSlicedBucketsToCreate.Add(SpatialIdx.Bucket);
				} else
				{
					NonTimeSlicedBucketsToCreate.Add(SpatialIdx.Bucket);
				}
			}
		}

		//todo: creation can go wide, insertion to collection cannot
		for(uint8 BucketIdx : TimeSlicedBucketsToCreate)
		{
			if(ViewsPerBucket[BucketIdx].Num())
			{
				SCOPE_CYCLE_COUNTER(STAT_CreateInitialAccelerationStructure);

				auto ParticleView = MakeConstParticleView(MoveTemp(ViewsPerBucket[BucketIdx]));
				auto NewStruct = SpatialCollectionFactory.CreateAccelerationPerBucket_Threaded(ParticleView,BucketIdx,IsForceFullBuild);

				//If new structure is not done mark time slicing in progress
				IsTimeSlicingProgressing |= !NewStruct->IsAsyncTimeSlicingComplete();

				AccelerationStructure->AddSubstructure(MoveTemp(NewStruct),BucketIdx);

			}
		}

		AccelerationStructure->SetAllAsyncTasksComplete(!IsTimeSlicingProgressing);

		// If it's not progressing then it is finished so we can perform the final copy if required
		if(!IsTimeSlicingProgressing)
		{
			//todo: creation can go wide, insertion to collection cannot
			for(uint8 BucketIdx : NonTimeSlicedBucketsToCreate)
			{
				if(ViewsPerBucket[BucketIdx].Num())
				{
					SCOPE_CYCLE_COUNTER(STAT_CreateNonSlicedStructures);

					auto ParticleView = MakeConstParticleView(MoveTemp(ViewsPerBucket[BucketIdx]));
					auto NewStruct = SpatialCollectionFactory.CreateAccelerationPerBucket_Threaded(ParticleView,BucketIdx,IsForceFullBuild);

					AccelerationStructure->AddSubstructure(MoveTemp(NewStruct),BucketIdx);

				}
			}
		}
	}

	void FPBDRigidsEvolutionBase::FChaosAccelerationStructureTask::DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		LLM_SCOPE(ELLMTag::ChaosAcceleration);

		//Rebuild both structures. TODO: probably faster to time slice the copy instead of doing two time sliced builds
		UpdateStructure(InternalStructure);
		UpdateStructure(ExternalStructure);
	}

	void FPBDRigidsEvolutionBase::ApplyParticlePendingData(const FPendingSpatialData& SpatialData, FAccelerationStructure& AccelerationStructure, bool bUpdateCache)
	{
		if (SpatialData.bDelete)
		{
			AccelerationStructure.RemoveElementFrom(SpatialData.AccelerationHandle, SpatialData.SpatialIdx);

			if (bUpdateCache)
			{
				if (uint32* InnerIdxPtr = ParticleToCacheInnerIdx.Find(SpatialData.UniqueIdx()))
				{
					FSpatialAccelerationCache& Cache = *SpatialAccelerationCache.FindChecked(SpatialData.SpatialIdx);	//can't delete from cache that doesn't exist
					const uint32 CacheInnerIdx = *InnerIdxPtr;
					if (CacheInnerIdx + 1 < Cache.Size())	//will get swapped with last element, so update it
					{
						const FUniqueIdx LastParticleInCacheUniqueIdx = Cache.Payload(Cache.Size() - 1).UniqueIdx();
						ParticleToCacheInnerIdx.FindChecked(LastParticleInCacheUniqueIdx) = CacheInnerIdx;
					}

					Cache.DestroyElement(CacheInnerIdx);
					ParticleToCacheInnerIdx.Remove(SpatialData.UniqueIdx());
				}
			}
		}
		else
		{
			FGeometryParticleHandle* UpdateParticle = SpatialData.AccelerationHandle.GetGeometryParticleHandle_PhysicsThread();

			AccelerationStructure.UpdateElementIn(UpdateParticle, UpdateParticle->WorldSpaceInflatedBounds(), UpdateParticle->HasBounds(), SpatialData.SpatialIdx);

			if (bUpdateCache)
			{
				TUniquePtr<FSpatialAccelerationCache>* CachePtrPtr = SpatialAccelerationCache.Find(SpatialData.SpatialIdx);
				if (CachePtrPtr == nullptr)
				{
					CachePtrPtr = &SpatialAccelerationCache.Add(SpatialData.SpatialIdx, TUniquePtr<FSpatialAccelerationCache>(new FSpatialAccelerationCache));
				}

				FSpatialAccelerationCache& Cache = **CachePtrPtr;

				//make sure in mapping
				uint32 CacheInnerIdx;
				if (uint32* CacheInnerIdxPtr = ParticleToCacheInnerIdx.Find(SpatialData.UniqueIdx()))
				{
					CacheInnerIdx = *CacheInnerIdxPtr;
				}
				else
				{
					CacheInnerIdx = Cache.Size();
					Cache.AddElements(1);
					ParticleToCacheInnerIdx.Add(SpatialData.UniqueIdx(), CacheInnerIdx);
				}

				//update cache entry
				Cache.HasBounds(CacheInnerIdx) = UpdateParticle->HasBounds();
				Cache.Bounds(CacheInnerIdx) = UpdateParticle->WorldSpaceInflatedBounds();
				Cache.Payload(CacheInnerIdx) = SpatialData.AccelerationHandle;
			}
		}
	}

	void FPBDRigidsEvolutionBase::FlushInternalAccelerationQueue()
	{
		for (const FPendingSpatialData& PendingData : InternalAccelerationQueue.PendingData)
		{
			ApplyParticlePendingData(PendingData, *InternalAcceleration, false);
		}
		InternalAcceleration->SetSyncTimestamp(LatestExternalTimestampConsumed_Internal);
		InternalAccelerationQueue.Reset();
	}

	void FPBDRigidsEvolutionBase::FlushAsyncAccelerationQueue()
	{
		for (const FPendingSpatialData& PendingData : AsyncAccelerationQueue.PendingData)
		{
			ApplyParticlePendingData(PendingData, *AsyncInternalAcceleration, true); //only the first queue needs to update the cached acceleration
			ApplyParticlePendingData(PendingData, *AsyncExternalAcceleration, false);
		}

				//NOTE: This assumes that we are never creating a PT particle that is replicated to GT
				//At the moment that is true, and it seems like we have enough mechanisms to avoid this direction
				//If we want to support that, the UniqueIndex must be kept around until GT goes away
				//This is hard to do, but would probably mean the ownership of the index is in the proxy
		for (FUniqueIdx UniqueIdx : UniqueIndicesPendingRelease)
		{
			ReleaseIdx(UniqueIdx);
		}
		UniqueIndicesPendingRelease.Reset();
		AsyncAccelerationQueue.Reset();

		//other queues are no longer needed since we've flushed all operations and now have a pristine structure
		InternalAccelerationQueue.Reset();

		AsyncInternalAcceleration->SetSyncTimestamp(LatestExternalTimestampConsumed_Internal);
		AsyncExternalAcceleration->SetSyncTimestamp(LatestExternalTimestampConsumed_Internal);
	}

	//TODO: make static and _External suffix
	void FPBDRigidsEvolutionBase::FlushExternalAccelerationQueue(FAccelerationStructure& Acceleration, FPendingSpatialDataQueue& ExternalQueue)
	{
		//update structure with any pending operations. Note that we must keep those operations around in case next structure still hasn't consumed them (async mode)
		const int32 SyncTimestamp = Acceleration.GetSyncTimestamp();
		for (int32 Idx = ExternalQueue.PendingData.Num() - 1; Idx >=0; --Idx)
		{
			const FPendingSpatialData& SpatialData = ExternalQueue.PendingData[Idx];
			if(SpatialData.SyncTimestamp > SyncTimestamp)
			{
				//operation still pending so update structure
				//note: do we care about roll over? if game ticks at 60fps we'd get 385+ days
				if(SpatialData.bDelete)
				{
					Acceleration.RemoveElementFrom(SpatialData.AccelerationHandle,SpatialData.SpatialIdx);
				}
				else
				{
					FGeometryParticle* UpdateParticle = SpatialData.AccelerationHandle.GetExternalGeometryParticle_ExternalThread();
					FAABB3 WorldBounds;
					const bool bHasBounds = UpdateParticle->Geometry() && UpdateParticle->Geometry()->HasBoundingBox();
					if(bHasBounds)
					{
						TRigidTransform<FReal,3> WorldTM(UpdateParticle->X(),UpdateParticle->R());
						WorldBounds = UpdateParticle->Geometry()->BoundingBox().TransformedAABB(WorldTM);
					}
					Acceleration.UpdateElementIn(UpdateParticle,WorldBounds,bHasBounds,SpatialData.SpatialIdx);
				}
			}
			else
			{
				//operation was already considered by sim, so remove it
				//going in reverse order so PendingData will stay valid
				ExternalQueue.Remove(SpatialData.UniqueIdx());
			}
		}
	}

	void FPBDRigidsEvolutionBase::WaitOnAccelerationStructure()
	{
		if (AccelerationStructureTaskComplete.GetReference())
		{
			FGraphEventArray ThingsToComplete;
			ThingsToComplete.Add(AccelerationStructureTaskComplete);
			QUICK_SCOPE_CYCLE_COUNTER(STAT_TPBDRigidsEvolutionBase_WaitAccelerationStructure);
			FTaskGraphInterface::Get().WaitUntilTasksComplete(ThingsToComplete);
		}
	}

	typename FPBDRigidsEvolutionBase::FAccelerationStructure* FPBDRigidsEvolutionBase::GetFreeSpatialAcceleration_Internal()
	{
		FAccelerationStructure* Structure;
		if(!ExternalStructuresPool.Dequeue(Structure))
		{
			AccelerationBackingBuffer.Add(SpatialCollectionFactory->CreateEmptyCollection());
			Structure = AccelerationBackingBuffer.Last().Get();
		}
		
		return Structure;
	}

	void FPBDRigidsEvolutionBase::FreeSpatialAcceleration_External(FAccelerationStructure* Structure)
	{
		//don't need to reset as rebuild task does that for us
		ExternalStructuresPool.Enqueue(Structure);
	}
	
	void FPBDRigidsEvolutionBase::ComputeIntermediateSpatialAcceleration(bool bBlock)
	{
		LLM_SCOPE(ELLMTag::ChaosAcceleration);
		SCOPE_CYCLE_COUNTER(STAT_ComputeIntermediateSpatialAcceleration);
		CHAOS_SCOPED_TIMER(ComputeIntermediateSpatialAcceleration);

		bool ForceFullBuild = InternalAccelerationQueue.Num() > 1000;

		if (!AccelerationStructureTaskComplete)
		{
			//initial frame so make empty structures

			InternalAcceleration = GetFreeSpatialAcceleration_Internal();
			AsyncInternalAcceleration = GetFreeSpatialAcceleration_Internal();
			AsyncExternalAcceleration = GetFreeSpatialAcceleration_Internal();
			FlushInternalAccelerationQueue();
		}

		if (bBlock)
		{
			WaitOnAccelerationStructure();
		}

		bool AsyncComplete = !AccelerationStructureTaskComplete || AccelerationStructureTaskComplete->IsComplete();

		if (AsyncComplete)
		{
			bool bNeedsReset = false;

			// only copy when the acceleration structures have completed time-slicing
			if (AccelerationStructureTaskComplete && AsyncInternalAcceleration->IsAllAsyncTasksComplete())
			{
				SCOPE_CYCLE_COUNTER(STAT_SwapAccelerationStructures);

				check(AsyncInternalAcceleration->IsAllAsyncTasksComplete());

				bNeedsReset = true;

				FlushAsyncAccelerationQueue();

				//swap acceleration structure for new one
				std::swap(InternalAcceleration, AsyncInternalAcceleration);

				//mark structure as ready for external thread
				ExternalStructuresQueue.Enqueue(AsyncExternalAcceleration);

				//get a new structure to work on while we wait for external thread to consume the one we just finished
				AsyncExternalAcceleration = GetFreeSpatialAcceleration_Internal();
			}
			else
			{
				FlushInternalAccelerationQueue();
			}
			
			if (bCanStartAsyncTasks)
			{
				// we run the task for both starting a new accel structure as well as for the time-slicing
			AccelerationStructureTaskComplete = TGraphTask<FChaosAccelerationStructureTask>::CreateTask().ConstructAndDispatchWhenReady(*SpatialCollectionFactory, SpatialAccelerationCache, AsyncInternalAcceleration, AsyncExternalAcceleration, ForceFullBuild, bIsSingleThreaded, bNeedsReset);
			}
		}
		else
		{
			FlushInternalAccelerationQueue();
		}
	}

	void FPBDRigidsEvolutionBase::UpdateExternalAccelerationStructure_External(
		ISpatialAccelerationCollection<FAccelerationStructureHandle, FReal, 3>*& StructToUpdate, FPendingSpatialDataQueue& PendingExternal)
	{
		DECLARE_SCOPE_CYCLE_COUNTER(TEXT("CreateExternalAccelerationStructure"), STAT_CreateExternalAccelerationStructure, STATGROUP_Physics);
		LLM_SCOPE(ELLMTag::ChaosAcceleration);

		FAccelerationStructure* NewStruct = nullptr;
		while(ExternalStructuresQueue.Dequeue(NewStruct))	//get latest structure
		{
			//free old struct
			if(StructToUpdate)
			{
				FreeSpatialAcceleration_External(StructToUpdate);
			}

			StructToUpdate = NewStruct;
		}

		if (ensure(StructToUpdate) && NewStruct != nullptr)
		{
			FlushExternalAccelerationQueue(*StructToUpdate, PendingExternal);
		}
	}

	void FPBDRigidsEvolutionBase::FlushSpatialAcceleration()
	{
		//force build acceleration structure with latest data
		ComputeIntermediateSpatialAcceleration(true);
		ComputeIntermediateSpatialAcceleration(true);	//having to do it multiple times because of the various caching involved over multiple frames.
		ComputeIntermediateSpatialAcceleration(true);
	}

	void FPBDRigidsEvolutionBase::RebuildSpatialAccelerationForPerfTest()
	{
		WaitOnAccelerationStructure();

		ParticleToCacheInnerIdx.Reset();
		AsyncAccelerationQueue.Reset();
		InternalAccelerationQueue.Reset();

		AccelerationStructureTaskComplete = nullptr;
		const auto& NonDisabled = Particles.GetNonDisabledView();
		for (auto& Particle : NonDisabled)
		{
			DirtyParticle(Particle);
		}

		FlushSpatialAcceleration();
	}

	void FPBDRigidsEvolutionBase::ReleaseIdx(FUniqueIdx Idx)
	{
		PendingReleaseIndices.Add(Idx);
	}

	void FPBDRigidsEvolutionBase::ReleasePendingIndices()
	{
		for (FUniqueIdx Idx : PendingReleaseIndices)
		{
			Particles.GetUniqueIndices().ReleaseIdx(Idx);
		}
		PendingReleaseIndices.Reset();
	}


	void FPBDRigidsEvolutionBase::Serialize(FChaosArchive& Ar)
	{
		ensure(false);	//disabled transient data serialization. Need to rethink
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
				TUniquePtr<ISpatialAcceleration<FAccelerationStructureHandle, FReal, 3>> SubStructure;
				if (!Ar.IsLoading())
				{
					SubStructure = InternalAcceleration->RemoveSubstructure(FSpatialAccelerationIdx{ 0,0 });
					Ar << SubStructure;
					InternalAcceleration->AddSubstructure(MoveTemp(SubStructure), 0);
				}
				else
				{
					Ar << SubStructure;
					AccelerationBackingBuffer.Add(CreateNewSpatialStructureFromSubStructure(MoveTemp(SubStructure)));
					InternalAcceleration = AccelerationBackingBuffer.Last().Get();
				}
			}
			else
			{
				TUniquePtr<FAccelerationStructure> InternalUnique;
				SpatialCollectionFactory->Serialize(InternalUnique, Ar);
				InternalAcceleration = InternalUnique.Get();
				AccelerationBackingBuffer.Add(MoveTemp(InternalUnique));
			}

			/*if (Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) < FExternalPhysicsCustomObjectVersion::FlushEvolutionInternalAccelerationQueue)
			{
				SerializePendingMap(Ar, InternalAccelerationQueue);
				SerializePendingMap(Ar, AsyncAccelerationQueue);
				SerializePendingMap(Ar, ExternalAccelerationQueue);
			}*/
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

// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Core.h"
#include "Chaos/Framework/MultiBufferResource.h"
#include "Chaos/GeometryParticlesfwd.h"
#include "Chaos/ParticleHandleFwd.h"
#include "Chaos/PBDRigidsEvolutionFwd.h"
#include "Containers/Queue.h"

#ifndef WITH_TODO_COLLISION_DISABLE
#define WITH_TODO_COLLISION_DISABLE 0
#endif

namespace Chaos
{
	enum class ECollisionConstraintFlags : uint32
	{
		CCF_None                       = 0x0,
		CCF_BroadPhaseIgnoreCollisions = 0x1,
		CCF_DummyFlag
	};

	class CHAOS_API FIgnoreCollisionManager
	{
	public:
		using FHandleID = FUniqueIdx;
		using FDeactivationSet = TSet<FUniqueIdx>;
		using FActiveMap = TMap<FHandleID, TArray<FHandleID> >;
		using FPendingMap = TMap<FHandleID, TArray<FHandleID> >;
		struct FStorageData
		{
			FPendingMap PendingActivations;
			FDeactivationSet PendingDeactivations;
			int32 ExternalTimestamp = INDEX_NONE;

			void Reset()
			{
				PendingActivations.Reset();
				PendingDeactivations.Reset();
				ExternalTimestamp = INDEX_NONE;
			}
		};

		FIgnoreCollisionManager()
			: StorageDataProducer(nullptr)
		{
			StorageDataProducer = GetNewStorageData();
		}

		bool ContainsHandle(FHandleID Body0);

		bool IgnoresCollision(FHandleID Body0, FHandleID Body1);

		int32 NumIgnoredCollision(FHandleID Body0);

		void AddIgnoreCollisionsFor(FHandleID Body0, FHandleID Body1);

		void RemoveIgnoreCollisionsFor(FHandleID Body0, FHandleID Body1);

		FPendingMap& GetPendingActivationsForGameThread(int32 ExternalTimestamp) 
		{
			if (StorageDataProducer->ExternalTimestamp == INDEX_NONE)
			{
				StorageDataProducer->ExternalTimestamp = ExternalTimestamp;
			}
			else
			{
				ensure(StorageDataProducer->ExternalTimestamp == ExternalTimestamp);
			}

			return StorageDataProducer->PendingActivations;
		}

		FDeactivationSet& GetPendingDeactivationsForGameThread(int32 ExternalTimestamp)
		{
			if (StorageDataProducer->ExternalTimestamp == INDEX_NONE)
			{
				StorageDataProducer->ExternalTimestamp = ExternalTimestamp;
			}
			else
			{
				ensure(StorageDataProducer->ExternalTimestamp == ExternalTimestamp);
			}

			return StorageDataProducer->PendingDeactivations;
		}

		void PushProducerStorageData_External(int32 ExternalTimestamp)
		{
			if (StorageDataProducer->ExternalTimestamp != INDEX_NONE)
			{
				ensure(ExternalTimestamp == StorageDataProducer->ExternalTimestamp);
				StorageDataQueue.Enqueue(StorageDataProducer);
				StorageDataProducer = GetNewStorageData();
			}
		}

		/*
		*
		*/
		void ProcessPendingQueues();

		/*
		*
		*/
		void PopStorageData_Internal(int32 ExternalTimestamp);

	private:

		FStorageData* GetNewStorageData()
		{
			FStorageData* StorageData;
			if (StorageDataFreePool.Dequeue(StorageData))
			{
				return StorageData;
			}

			StorageDataBackingBuffer.Emplace(MakeUnique<FStorageData>());
			return StorageDataBackingBuffer.Last().Get();
		}

		void ReleaseStorageData(FStorageData *InStorageData)
		{
			InStorageData->Reset();
			StorageDataFreePool.Enqueue(InStorageData);
		}

		FActiveMap IgnoreCollisionsList;

		FPendingMap PendingActivations;
		FDeactivationSet PendingDeactivations;

		// Producer storage data, pending changes written here until pushed into queue.
		FStorageData* StorageDataProducer;

		TQueue<FStorageData*, EQueueMode::Spsc> StorageDataQueue; // Queue of storage data being passed to physics thread
		TQueue<FStorageData*,EQueueMode::Spsc> StorageDataFreePool;	//free pool of storage data
		TArray<TUniquePtr<FStorageData>> StorageDataBackingBuffer;	// Holds unique ptrs for storage data allocation
	};

} // Chaos

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Async/AsyncWork.h"
#include "Async/Future.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Containers/RingBuffer.h"
#include "DerivedDataRequest.h"
#include "HAL/CriticalSection.h"
#include "Serialization/BulkDataRegistry.h"
#include "Templates/RefCounting.h"
#include "TickableEditorObject.h"
#include "Virtualization/VirtualizedBulkData.h"

class FCompressedBuffer;
class UPackage;
struct FIoHash;

namespace UE::BulkDataRegistry::Private
{

class FBulkDataRegistryEditorDomain;

/** Struct for storage of a BulkData in the registry, including the BulkData itself and data about cache status. */
struct FRegisteredBulk
{
	FRegisteredBulk() = default;
	FRegisteredBulk(UE::Virtualization::FVirtualizedUntypedBulkData&& InBulkData, FName InPackageNameToUpdate = NAME_None)
		:BulkData(MoveTemp(InBulkData)), PackageNameToUpdate(InPackageNameToUpdate)
	{
	}

	UE::Virtualization::FVirtualizedUntypedBulkData BulkData;
	FName PackageNameToUpdate;
	bool bHasTempPayload = false;
};

/** Serialize an array of BulkDatas into or out of bytes saved/load from the registry's persistent cache. */
void Serialize(FArchive& Ar, TArray<UE::Virtualization::FVirtualizedUntypedBulkData>& InDatas);

/** A collection of bulkdatas that should be sent to the cache for the given package. */
class FPendingPackage
{
public:
	FPendingPackage(FName PackageName, FBulkDataRegistryEditorDomain* InOwner);
	FPendingPackage(FPendingPackage&& Other) = default;
	FPendingPackage(const FPendingPackage& Other) = delete;
	~FPendingPackage();

	bool IsReadFinished() const;
	void UpdateCache();
	double GetCreateTime() const
	{
		return CreateTime;
	}
	void AddBulkData(const UE::Virtualization::FVirtualizedUntypedBulkData& BulkData)
	{
		BulkDatas.Add(BulkData);
	}

private:
	void OnCacheResults(FSharedBuffer Buffer);

	FName PackageName;
	TArray<UE::Virtualization::FVirtualizedUntypedBulkData> BulkDatas;
	TArray<UE::Virtualization::FVirtualizedUntypedBulkData> CachedBulkDatas;
	UE::DerivedData::FRequest CacheRequest;
	FBulkDataRegistryEditorDomain* Owner;
	double CreateTime;
};

/** Data about a BulkData that has loaded its payload for TryGetMeta and should drop it after GetData or a timeout. */
struct FTempLoadedPayload
{
	FGuid Guid;
	uint64 PayloadSize;
	double EndTime;
};

/** An Active flag and a lock around it for informing AutoDeleteAsyncTasks that their shared data is no longer available. */
class FTaskSharedDataLock : public FThreadSafeRefCountedObject
{
public:
	FRWLock ActiveLock;
	bool bActive = true;
};

/** A worker that updates the PayloadId for a BulkData that is missing its RawHash. */
class FUpdatePayloadWorker : public FNonAbandonableTask
{
public:
	FUpdatePayloadWorker(FBulkDataRegistryEditorDomain* InBulkDataRegistry,
		const UE::Virtualization::FVirtualizedUntypedBulkData& InSourceBulk);

	void DoWork();
	TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FUpdatePayloadWorker, STATGROUP_ThreadPoolAsyncTasks);
	}

private:
	UE::Virtualization::FVirtualizedUntypedBulkData BulkData;
	TRefCountPtr<FTaskSharedDataLock> SharedDataLock;
	FBulkDataRegistryEditorDomain* BulkDataRegistry;
};

/** Data storage for the FUpdatePayloadWorker that is updated while in flight for additional requesters. */
struct FUpdatingPayload
{
	/** Pointer to the task. Once set to non-null, is never modified. The task ensure this FUpdatingPayload is destroyed before the task destructs. */
	FAutoDeleteAsyncTask<FUpdatePayloadWorker>* AsyncTask = nullptr;
	TArray<TUniqueFunction<void(bool bValid, const FCompressedBuffer& Buffer)>> Requesters;
};


/** Implementation of a BulkDataRegistry that stores its persistent data in a DDC bucket. */
class FBulkDataRegistryEditorDomain : public IBulkDataRegistry, public FTickableEditorObject, public FTickableCookObject
{
public:
	FBulkDataRegistryEditorDomain();
	virtual ~FBulkDataRegistryEditorDomain();

	// IBulkDataRegistry interface
	virtual void Register(UPackage* Owner, const UE::Virtualization::FVirtualizedUntypedBulkData& BulkData) override;
	virtual void OnExitMemory(const UE::Virtualization::FVirtualizedUntypedBulkData& BulkData) override;
	virtual TFuture<UE::BulkDataRegistry::FMetaData> GetMeta(const FGuid& BulkDataId) override;
	virtual TFuture<UE::BulkDataRegistry::FData> GetData(const FGuid& BulkDataId) override;

	// FTickableEditorObject/FTickableCookObject interface
	virtual void Tick(float DeltaTime) override;
	virtual void TickCook(float DeltaTime, bool bTickComplete) override;
	virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Always; }
	virtual TStatId GetStatId() const override { return TStatId(); }

private:
	void AddPendingPackageBulkData(FName PackageName, const UE::Virtualization::FVirtualizedUntypedBulkData& BulkData);
	void AddPendingPackageBulkDataInsideLock(FName PackageName, const UE::Virtualization::FVirtualizedUntypedBulkData& BulkData);
	void PollPendingPackages(bool bWaitForCooldown);
	void AddTempLoadedPayload(const FGuid& RegistryKey, uint64 PayloadSize);
	void PruneTempLoadedPayloads();
	friend class FPendingPackage;
	friend class FUpdatePayloadWorker;

	// All three locks can  be held at the same time. They must always be entered in order: SharedDataLock, RegistryLock, PendingPackageLock.
	TRefCountPtr<FTaskSharedDataLock> SharedDataLock;
	FRWLock RegistryLock;
	FCriticalSection PendingPackageLock;

	TMap<FGuid, FRegisteredBulk> Registry;
	TMap<FGuid, FUpdatingPayload> UpdatingPayloads;
	TMap<FName, TUniquePtr<FPendingPackage>> PendingPackages;
	TRingBuffer<FTempLoadedPayload> TempLoadedPayloads;
	uint64 TempLoadedPayloadsSize = 0;
	bool bActive = true;
};

} // namespace UE::BulkDataRegistry::Private

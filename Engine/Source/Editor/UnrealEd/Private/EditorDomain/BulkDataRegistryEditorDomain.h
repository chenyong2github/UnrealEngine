// Copyright Epic Games, Inc. All Rights Reserved.

#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Containers/RingBuffer.h"
#include "DerivedDataRequest.h"
#include "HAL/CriticalSection.h"
#include "Serialization/BulkDataRegistry.h"
#include "TickableEditorObject.h"
#include "Virtualization/VirtualizedBulkData.h"

class FCompressedBuffer;
class UPackage;
struct FIoHash;

namespace UE::BulkDataRegistry::Private
{

class FBulkDataRegistryEditorDomain;

/** Struct for storage of a BulkData in the registry, including the BulkData itself and data about cache status. */
struct FBulkSource
{
	FBulkSource() = default;
	FBulkSource(UE::Virtualization::FVirtualizedUntypedBulkData&& InBulkData, FName InPackageNameToUpdate = NAME_None)
		:BulkData(MoveTemp(InBulkData)), PackageNameToUpdate(InPackageNameToUpdate)
	{
	}

	UE::Virtualization::FVirtualizedUntypedBulkData BulkData;
	FName PackageNameToUpdate;
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
	void PollPendingPackages(bool bWaitForCooldown);
	void AddTempLoadedPayload(const FGuid& RegistryKey, uint64 PayloadSize);
	void PruneTempLoadedPayloads();
	friend class FPendingPackage;

	FCriticalSection PendingPackageLock;
	FRWLock RegistryLock;
	TMap<FGuid, FBulkSource> Registry;
	TMap<FName, TUniquePtr<FPendingPackage>> PendingPackages;
	TRingBuffer<FTempLoadedPayload> TempLoadedPayloads;
	uint64 TempLoadedPayloadsSize = 0;
	bool bAllowCacheUpdates = true;
};

} // namespace UE::BulkDataRegistry::Private

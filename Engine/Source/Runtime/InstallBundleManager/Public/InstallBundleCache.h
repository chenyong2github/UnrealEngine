// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "InstallBundleTypes.h"

class FInstallBundleCache;

struct FInstallBundleCacheInitInfo
{
	FName CacheName;
	uint64 Size = 0;
};

struct FInstallBundleCacheBundleInfo
{
	FName BundleName;
	uint64 FullInstallSize = 0; // Total disk footprint when this bundle is fully installed
	uint64 CurrentInstallSize = 0; // Disk footprint of the bundle in it's current state
	FDateTime TimeStamp = FDateTime::MinValue(); // Last access time for the bundle.  Used for eviction order
};

enum class EInstallBundleCacheReserveResult : int8
{
	Fail_CacheFull, // Cache is full and it's not possible to evict anything else from the cache
	Fail_NeedsEvict, // Cache is full but it' possible to evict released bundles to make room for this one
	Fail_PendingEvict, // This bundle can't be reserved because it's currently being evicted
	Success, // Bundle was reserved successfully
};

struct FInstallBundleCacheReserveResult
{
	TMap<FName, TArray<EInstallBundleSourceType>> BundlesToEvict;
	EInstallBundleCacheReserveResult Result = EInstallBundleCacheReserveResult::Success;
};

struct FInstallBundleCacheStats
{
	FName CacheName;
	uint64 MaxSize = 0;
	uint64 UsedSize = 0;
	uint64 ReservedSize = 0;
	uint64 FreeSize = 0;
};

class INSTALLBUNDLEMANAGER_API FInstallBundleCache : public TSharedFromThis<FInstallBundleCache>
{
public:
	virtual ~FInstallBundleCache();

	void Init(FInstallBundleCacheInitInfo InitInfo);

	// Add a bundle to the cache.  
	void AddOrUpdateBundle(EInstallBundleSourceType Source, const FInstallBundleCacheBundleInfo& AddInfo);

	void RemoveBundle(EInstallBundleSourceType Source, FName BundleName);

	TOptional<FInstallBundleCacheBundleInfo> GetBundleInfo(EInstallBundleSourceType Source, FName BundleName);

	// Return the total size of the cache
	uint64 GetSize() const;
	// Return the amount of space in use.  This could possbly exceed GetSize() if the cache size is changed or more 
	// bundles are added the to cache.
	uint64 GetUsedSize() const;
	// Return the amount of free space in the cache, clamped to [0, GetSize()]
	uint64 GetFreeSpace() const;

	// Called from bundle manager
	FInstallBundleCacheReserveResult Reserve(FName BundleName);

	// Called from bundle manager to make the files for this bundle eligible for eviction
	bool Release(FName BundleName);

	bool SetPendingEvict(FName BundleName);

	// Hint to the cache that this bundle is requested, and we should prefer to evict non-requested bundles if possible
	void HintRequested(FName BundleName, bool bRequested);

	FInstallBundleCacheStats GetStats(bool bDumpToLog = false) const;

private:
	uint64 GetFreeSpaceInternal(uint64 UsedSize) const;

	void CheckInvariants() const;
	
	void UpdateCacheInfoFromSourceInfo(FName BundleName);

private:
	struct FPerSourceBundleCacheInfo
	{
		uint64 FullInstallSize = 0;
		uint64 CurrentInstallSize = 0;
		FDateTime TimeStamp = FDateTime::MinValue();
	};

	enum class ECacheState : uint8
	{
		Released, //Transitions to Reserved or PendingEvict
		Reserved, //Transitions to Released
		PendingEvict, // Transitions to Released
	};

	struct FBundleCacheInfo 
	{
		uint64 FullInstallSize = 0;
		uint64 CurrentInstallSize = 0;
		FDateTime TimeStamp = FDateTime::MinValue();
		ECacheState State = ECacheState::Released;
		bool bHintReqeusted = false; // Hint to the cache that this bundle is requested, and we should prefer to evict non-requested bundles if possible

		uint64 GetSize() const
		{
			if (State == ECacheState::Released)
				return CurrentInstallSize;

			// Just consider any pending evictions to be 0 size.
			// Bundle Manager will still wait on them if necessary when reserving.
			if (State == ECacheState::PendingEvict)
				return 0;

			if (CurrentInstallSize > FullInstallSize)
				return CurrentInstallSize;

			return FullInstallSize;
		}
	};

private:

	TMap<FName, TMap<EInstallBundleSourceType, FPerSourceBundleCacheInfo>> PerSourceCacheInfo;

	TMap<FName, FBundleCacheInfo> CacheInfo;

	uint64 TotalSize = 0;

	FName CacheName;
};

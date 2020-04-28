// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "InstallBundleTypes.h"

class FInstallBundleCache;

DECLARE_DELEGATE_TwoParams(FInstallBundleCacheEvictDelegate, TSharedRef<FInstallBundleCache> /*Cache*/, FName /*BundleName*/);

struct FInstallBundleCacheInitInfo
{
	FName CacheName;
	uint64 Size = 0;
	FInstallBundleCacheEvictDelegate DeleteBundleFiles;
};

struct FInstallBundleCacheAddBundleInfo
{
	FName BundleName;
	uint64 FullInstallSize = 0; // Total disk footprint when this bundle is fully installed
	uint64 CurrentInstallSize = 0; // Disk footprint of the bundle in it's current state
};

enum class EInstallBundleCacheReserveResult : int8
{
	Failure,
	Success,
	NeedsEvict,
};

struct FInstallBundleCacheReserveResult
{
	TArray<FName> BundlesToEvict;
	EInstallBundleCacheReserveResult Result = EInstallBundleCacheReserveResult::Failure;
};

class INSTALLBUNDLEMANAGER_API FInstallBundleCache : public TSharedFromThis<FInstallBundleCache>
{
public:
	virtual ~FInstallBundleCache();

	void Init(FInstallBundleCacheInitInfo InitInfo);

	// Add a bundle to the cache.  
	void AddOrUpdateBundle(const FInstallBundleCacheAddBundleInfo& AddInfo);

	void RemoveBundle(FName BundleName);

	bool HasBundle(FName BundleName);

	// Return the total size of the cache
	uint64 GetSize() const;
	// Return the amount of free space in the cache
	uint64 GetFreeSpace() const;

	// Called from bundle manager
	FInstallBundleCacheReserveResult Reserve(FName BundleName);

	// Called from bundle manager to make the files for this bundle eligible for eviction
	bool Release(FName BundleName);

	bool SetPendingEvict(FName BundleName);

	// Hint to the cache that this bundle is requested, and we should prefer to evict non-requested bundles if possible
	void HintRequested(FName BundleName, bool bRequested);

private:
	void CheckInvariants() const;

private:
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
		ECacheState State = ECacheState::Released;
		bool bHintReqeusted = false; // Hint to the cache that this bundle is requested, and we should prefer to evict non-requested bundles if possible

		uint64 GetSize() const
		{
			if (State == ECacheState::Released)
				return CurrentInstallSize;

			// Just consider any pending evictions to be 0 size.
			// We will flush any required evictions before installing
			// bundles that use this cache.
			if (State == ECacheState::PendingEvict)
				return 0;

			if (CurrentInstallSize > FullInstallSize)
				return CurrentInstallSize;

			return FullInstallSize;
		}
	};

private:
	FInstallBundleCacheEvictDelegate DeleteBundleFiles;

	TMap<FName, FBundleCacheInfo> CacheInfo;

	uint64 TotalSize = 0;

	FName CacheName;
};

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DerivedDataCacheRecord.h"
#include "HAL/FileManager.h"
#include "FileBackedDerivedDataBackend.h"
#include "ProfilingDebugging/CookStats.h"
#include "DerivedDataCacheUsageStats.h"
#include "Misc/ScopeLock.h"

class Error;

namespace UE::DerivedData::Backends
{

/** 
 * A simple thread safe, memory based backend. This is used for Async puts and the boot cache.
**/
class FMemoryDerivedDataBackend : public FFileBackedDerivedDataBackend
{
public:
	explicit FMemoryDerivedDataBackend(ICacheFactory& InFactory, const TCHAR* InName, int64 InMaxCacheSize = -1, bool bCanBeDisabled = false);
	~FMemoryDerivedDataBackend();

	/** Return a name for this interface */
	virtual FString GetName() const override { return Name; }

	/** return true if this cache is writable **/
	virtual bool IsWritable() const override;

	/** Returns a class of speed for this interface **/
	virtual ESpeedClass GetSpeedClass()  const override;

	/**
	 * Synchronous test for the existence of a cache item
	 *
	 * @param	CacheKey	Alphanumeric+underscore key of this cache item
	 * @return				true if the data probably will be found, this can't be guaranteed because of concurrency in the backends, corruption, etc
	 */
	virtual bool CachedDataProbablyExists(const TCHAR* CacheKey) override;

	/**
	 * Synchronous retrieve of a cache item
	 *
	 * @param	CacheKey	Alphanumeric+underscore key of this cache item
	 * @param	OutData		Buffer to receive the results, if any were found
	 * @return				true if any data was found, and in this case OutData is non-empty
	 */
	virtual bool GetCachedData(const TCHAR* CacheKey, TArray<uint8>& OutData) override;

	/**
	 * Asynchronous, fire-and-forget placement of a cache item
	 *
	 * @param	CacheKey	Alphanumeric+underscore key of this cache item
	 * @param	InData		Buffer containing the data to cache, can be destroyed after the call returns, immediately
	 * @param	bPutEvenIfExists	If true, then do not attempt skip the put even if CachedDataProbablyExists returns true
	 */
	virtual EPutStatus PutCachedData(const TCHAR* CacheKey, TArrayView<const uint8> InData, bool bPutEvenIfExists) override;

	virtual void RemoveCachedData(const TCHAR* CacheKey, bool bTransient) override;

	/**
	 * Save the cache to disk
	 * @param	Filename	Filename to save
	 * @return	true if file was saved successfully
	 */
	bool SaveCache(const TCHAR* Filename);

	/**
	 * Load the cache from disk
	 * @param	Filename	Filename to load
	 * @return	true if file was loaded successfully
	 */
	bool LoadCache(const TCHAR* Filename);

	/**
	 * Disable cache and ignore all subsequent requests
	 */
	void Disable() override;

	virtual TSharedRef<FDerivedDataCacheStatsNode> GatherUsageStats() const override;

	virtual bool TryToPrefetch(TConstArrayView<FString> CacheKeys) override;

	/**
	 *  Determines if we would cache the provided data
	 */
	virtual bool WouldCache(const TCHAR* CacheKey, TArrayView<const uint8> InData) override;

	/**
	 * Apply debug options
	 */
	bool ApplyDebugOptions(FBackendDebugOptions& InOptions) override;

	virtual FRequest Put(
		TArrayView<FCacheRecord> Records,
		FStringView Context,
		ECachePolicy Policy,
		EPriority Priority,
		FOnCachePutComplete&& OnComplete) override;

	virtual FRequest Get(
		TConstArrayView<FCacheKey> Keys,
		FStringView Context,
		ECachePolicy Policy,
		EPriority Priority,
		FOnCacheGetComplete&& OnComplete) override;

	virtual FRequest GetPayload(
		TConstArrayView<FCachePayloadKey> Keys,
		FStringView Context,
		ECachePolicy Policy,
		EPriority Priority,
		FOnCacheGetPayloadComplete&& OnComplete) override;

	virtual void CancelAll() override
	{
	}

private:
	/** Name of the cache file loaded (if any). */
	FString CacheFilename;
	FDerivedDataCacheUsageStats UsageStats;

	struct FCacheValue
	{
		int32 Age;
		TArray<uint8> Data;
		FCacheValue(TArrayView<const uint8> InData, int32 InAge = 0)
			: Age(InAge)
			, Data(InData.GetData(), InData.Num())
		{
		}
	};

	FORCEINLINE int32 CalcCacheValueSize(const FString& Key, const FCacheValue& Val)
	{
		return (Key.Len() + 1) * sizeof(TCHAR) + sizeof(Val.Age) + Val.Data.Num();
	}

	int64 CalcRawCacheRecordSize(const FCacheRecord& Record) const;
	int64 CalcSerializedCacheRecordSize(const FCacheRecord& Record) const;

	/** Factory used to construct cache records. */
	ICacheFactory& Factory;

	/** Name of this cache (used for debugging) */
	FString Name;

	/** Set of files that are being written to disk asynchronously. */
	TMap<FString, FCacheValue*> CacheItems;
	/** Set of records in this cache. */
	TSet<FCacheRecord, FCacheRecordKeyFuncs> CacheRecords;
	/** Maximum size the cached items can grow up to ( in bytes ) */
	int64 MaxCacheSize;
	/** When set to true, this cache is disabled...ignore all requests. */
	bool bDisabled;
	/** Object used for synchronization via a scoped lock						*/
	mutable FCriticalSection SynchronizationObject;
	/** Current estimated cache size in bytes */
	int64 CurrentCacheSize;
	/** Indicates that the cache max size has been exceeded. This is used to avoid
		warning spam after the size has reached the limit. */
	bool bMaxSizeExceeded;

	/** When a memory cache can be disabled, it won't return true for CachedDataProbablyExists calls.
	  * This is to avoid having the Boot DDC tells it has some resources that will suddenly disappear after the boot.
	  * Get() requests will still get fulfilled and other cache level will be properly back-filled 
	  * offering the speed benefit of the boot cache while maintaining coherency at all cache levels.
	  * 
	  * The problem is that most asset types (audio/staticmesh/texture) will always verify if their different LODS/Chunks can be found in the cache using CachedDataProbablyExists.
	  * If any of the LOD/MIP can't be found, a build of the asset is triggered, otherwise they skip asset compilation altogether.
	  * However, we should not skip the compilation based on the CachedDataProbablyExists result of the boot cache because it is a lie and will disappear at some point.
	  * When the boot cache disappears and the streamer tries to fetch a LOD that it has been told was cached, it will fail and will then have no choice but to rebuild the asset synchronously.
	  * This obviously causes heavy game-thread stutters.

	  * However, if the bootcache returns false during CachedDataProbablyExists. The async compilation will be triggered and data will be put in the both the boot.ddc and the local cache.
	  * This way, no more heavy game-thread stutters during streaming...

	  * This can be reproed when you clear the local cache but do not clear the boot.ddc file, but even if it's a corner case, I stumbled upon it enough times that I though it was worth to fix so the caches are coherent.
	  */
	bool bCanBeDisabled = false;
	bool bShuttingDown  = false;

	enum 
	{
		/** Magic number to use in header */
		MemCache_Magic = 0x0cac0ddc,
		/** Magic number to use in header (new, > 2GB size compatible) */
		MemCache_Magic64 = 0x0cac1ddc,
		/** Oldest cache items to keep */
		MaxAge = 3,
		/** Size of data that is stored in the cachefile apart from the cache entries (64 bit size). */
		SerializationSpecificDataSize = sizeof(uint32)	// Magic
									  + sizeof(int64)	// Size
									  + sizeof(uint32), // CRC
	};

protected:

	/* Debug helpers */
	bool DidSimulateMiss(const TCHAR* InKey);
	bool ShouldSimulateMiss(const TCHAR* InKey);

	/** Debug Options */
	FBackendDebugOptions DebugOptions;

	/** Keys we ignored due to miss rate settings */
	FCriticalSection MissedKeysCS;
	TSet<FName> DebugMissedKeys;
};

} // UE::DerivedData::Backends

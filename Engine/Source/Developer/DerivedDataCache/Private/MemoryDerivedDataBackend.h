// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/FileManager.h"
#include "FileBackedDerivedDataBackend.h"
#include "ProfilingDebugging/CookStats.h"
#include "DerivedDataCacheUsageStats.h"
#include "Misc/ScopeLock.h"

class Error;

/** 
 * A simple thread safe, memory based backend. This is used for Async puts and the boot cache.
**/
class FMemoryDerivedDataBackend : public FFileBackedDerivedDataBackend
{
public:
	explicit FMemoryDerivedDataBackend(const TCHAR* InName, int64 InMaxCacheSize = -1);
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
	virtual void PutCachedData(const TCHAR* CacheKey, TArrayView<const uint8> InData, bool bPutEvenIfExists) override;

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

	virtual bool TryToPrefetch(const TCHAR* CacheKey) override;

	/**
	 *  Determines if we would cache the provided data
	 */
	virtual bool WouldCache(const TCHAR* CacheKey, TArrayView<const uint8> InData) override;

	/**
	 * Apply debug options
	 */
	bool ApplyDebugOptions(FBackendDebugOptions& InOptions) override;
	

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

	/** Name of this cache (used for debugging) */
	FString Name;

	/** Set of files that are being written to disk asynchronously. */
	TMap<FString, FCacheValue*> CacheItems;
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


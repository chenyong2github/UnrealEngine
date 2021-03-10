// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "CoreMinimal.h"
#include "Stats/Stats.h"

class FDerivedDataCacheUsageStats;
class FDerivedDataCacheStatsNode;

DECLARE_LOG_CATEGORY_EXTERN(LogDerivedDataCache, Log, All);

DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Num Gets"),STAT_DDC_NumGets,STATGROUP_DDC, );
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Num Puts"),STAT_DDC_NumPuts,STATGROUP_DDC, );
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Num Build"),STAT_DDC_NumBuilds,STATGROUP_DDC, );
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Num Exists"),STAT_DDC_NumExist,STATGROUP_DDC, );
DECLARE_FLOAT_ACCUMULATOR_STAT_EXTERN(TEXT("Sync Get Time"),STAT_DDC_SyncGetTime,STATGROUP_DDC, );
DECLARE_FLOAT_ACCUMULATOR_STAT_EXTERN(TEXT("ASync Wait Time"),STAT_DDC_ASyncWaitTime,STATGROUP_DDC, );
DECLARE_FLOAT_ACCUMULATOR_STAT_EXTERN(TEXT("Sync Put Time"),STAT_DDC_PutTime,STATGROUP_DDC, );
DECLARE_FLOAT_ACCUMULATOR_STAT_EXTERN(TEXT("Sync Build Time"),STAT_DDC_SyncBuildTime,STATGROUP_DDC, );
DECLARE_FLOAT_ACCUMULATOR_STAT_EXTERN(TEXT("Exists Time"),STAT_DDC_ExistTime,STATGROUP_DDC, );



/** 
 * Interface for cache server backends. 
 * The entire API should be callable from any thread (except the singleton can be assumed to be called at least once before concurrent access).
**/
class FDerivedDataBackendInterface
{
public:

	/*
		Speed classes. Higher values are faster so > / < comparisons make sense.
	*/
	enum class ESpeedClass
	{
		Unknown,		/* Don't know yet*/
		Slow,			/* Slow, likely a remote drive. Some benefit but handle with care */
		Ok,				/* Ok but not great.  */
		Fast,			/* Fast but seek times still have an impact */
		Local			/* Little to no impact from seek times and extremly fast reads */
	};

	/* Debug options that can be applied to backends to simulate different behavior */
	struct FBackendDebugOptions
	{
		/* Percentage of requests that should result in random misses */
		int					RandomMissRate;

		/* Apply behavior of this speed class */
		ESpeedClass			SpeedClass;		

		/* Types of DDC entries that should always be a miss */
		TArray<FString>		SimulateMissTypes;

		FBackendDebugOptions()
			: RandomMissRate(0)
			, SpeedClass(ESpeedClass::Unknown)
		{
		}

		/* Fill in the provided structure based on the name of the node (e.g. 'shared') and the provided token stream */
		static bool ParseFromTokens(FBackendDebugOptions& OutOptions, const TCHAR* InNodeName, const TCHAR* InTokens);

		/* 
			Returns true if, according to the properties of this struct, the provided key should be treated as a miss.
			Implementing that miss and accounting for any behaviour impact (e.g. skipping a subsequent put) is left to
			each backend.
		*/
		bool ShouldSimulateMiss(const TCHAR* InCacheKey);
	};

	virtual ~FDerivedDataBackendInterface()
	{
	}

	/** Return a name for this interface */
	virtual FString GetName() const = 0;

	/** Return a name for this interface */
	virtual FString GetDisplayName() const { return GetName(); }

	/** return true if this cache is writable **/
	virtual bool IsWritable() const = 0;

	/** 
	 * return true if hits on this cache should propagate to lower cache level. Typically false for a PAK file. 
	 * Caution! This generally isn't propagated, so the thing that returns false must be a direct child of the heirarchical cache.
	 **/
	virtual bool BackfillLowerCacheLevels() const
	{
		return true;
	}

	/**
	 * Returns a class of speed for this interface
	 **/
	virtual ESpeedClass GetSpeedClass() const = 0;

	/**
	 * Synchronous test for the existence of a cache item
	 *
	 * @param	CacheKey	Alphanumeric+underscore key of this cache item
	 * @return				true if the data probably will be found, this can't be guaranteed because of concurrency in the backends, corruption, etc
	 */
	virtual bool CachedDataProbablyExists(const TCHAR* CacheKey)=0;
	/**
	 * Synchronous retrieve of a cache item
	 *
	 * @param	CacheKey	Alphanumeric+underscore key of this cache item
	 * @param	OutData		Buffer to receive the results, if any were found
	 * @return				true if any data was found, and in this case OutData is non-empty
	 */
	virtual bool GetCachedData(const TCHAR* CacheKey, TArray<uint8>& OutData)=0;
	/**
	 * Asynchronous, fire-and-forget placement of a cache item
	 *
	 * @param	CacheKey			Alphanumeric+underscore key of this cache item
	 * @param	InData				Buffer containing the data to cache, can be destroyed after the call returns, immediately
	 * @param	bPutEvenIfExists	If true, then do not attempt skip the put even if CachedDataProbablyExists returns true
	 */
	virtual void PutCachedData(const TCHAR* CacheKey, TArrayView<const uint8> InData, bool bPutEvenIfExists) = 0;

	/**
	 * Remove data from cache (used in the event that corruption is detected at a higher level and possibly house keeping)
	 *
	 * @param	CacheKey	Alphanumeric+underscore key of this cache item
	 * @param	bTransient	true if the data is transient and it is up to the backend to decide when and if to remove cached data.
	 */
	virtual void RemoveCachedData(const TCHAR* CacheKey, bool bTransient)=0;

	/**
	 * Retrieve usage stats for this backend. If the backend holds inner backends, this is expected to be passed down recursively.
	 */
	virtual TSharedRef<FDerivedDataCacheStatsNode> GatherUsageStats() const = 0;

	/**
	 * Synchronous attempt to make sure the cached data will be available as optimally as possible. This is left up to the implementation 
	 * to implement or just skip.
	 * @param	CacheKey	Alphanumeric+underscore key of this cache item
	 * @return				true if any steps were performed to optimize future retrieval
	 */
	virtual bool TryToPrefetch(const TCHAR* CacheKey) = 0;

	/**
	 * Allows the DDC backend to determine if it wants to cache the provided data. Reasons for returning false could be a slow connection,
	 * a file size limit, etc.
	 */
	virtual bool WouldCache(const TCHAR* CacheKey, TArrayView<const uint8> InData) = 0;

	/**
	 *  Ask a backend to apply debug behavior to simulate different conditions. Backends that don't support these options should return 
		false which will result in a warning if an attempt is made to apply these options.
	 */
	virtual bool ApplyDebugOptions(FBackendDebugOptions& InOptions) = 0;
};

class FDerivedDataBackend
{
public:
	/**
	 * Singleton to retrieve the GLOBAL backend
	 *
	 * @return Reference to the global cache backend
	 */
	static FDerivedDataBackend& Get();

	/**
	 * Singleton to retrieve the root cache
	 * @return Reference to the global cache root
	 */
	virtual FDerivedDataBackendInterface& GetRoot() = 0;

	//--------------------
	// System Interface, copied from FDerivedDataCacheInterface
	//--------------------

	virtual void NotifyBootComplete() = 0;
	virtual void AddToAsyncCompletionCounter(int32 Addend) = 0;
	virtual bool AnyAsyncRequestsRemaining() = 0;
	virtual void WaitForQuiescence(bool bShutdown = false) = 0;
	virtual void GetDirectories(TArray<FString>& OutResults) = 0;
	virtual bool GetUsingSharedDDC() const = 0;
	virtual const TCHAR* GetGraphName() const = 0;
	virtual const TCHAR* GetDefaultGraphName() const = 0;

	/**
	 * Mounts a read-only pak file.
	 *
	 * @param PakFilename Pak filename
	 */
	virtual FDerivedDataBackendInterface* MountPakFile(const TCHAR* PakFilename) = 0;

	/**
	 * Unmounts a read-only pak file.
	 *
	 * @param PakFilename Pak filename
	 */
	virtual bool UnmountPakFile(const TCHAR* PakFilename) = 0;

	/**
	 *  Gather the usage of the DDC hierarchically.
	 */
	virtual TSharedRef<FDerivedDataCacheStatsNode> GatherUsageStats() const = 0;
};

/* Lexical conversions from and to enums */
DERIVEDDATACACHE_API const TCHAR* LexToString(FDerivedDataBackendInterface::ESpeedClass SpeedClass);
DERIVEDDATACACHE_API void LexFromString(FDerivedDataBackendInterface::ESpeedClass& OutValue, const TCHAR* Buffer);
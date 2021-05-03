// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "DerivedDataBackendInterface.h"
#include "ProfilingDebugging/CookStats.h"
#include "DerivedDataCacheUsageStats.h"
#include "Misc/ScopeLock.h"
#include "Misc/FileHelper.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Templates/UniquePtr.h"

class Error;

namespace UE::DerivedData::Backends
{

/** 
 * A simple thread safe, pak file based backend. 
**/
class FPakFileDerivedDataBackend : public FDerivedDataBackendInterface
{
public:
	FPakFileDerivedDataBackend(ICacheFactory& InFactory, const TCHAR* InFilename, bool bInWriting);
	~FPakFileDerivedDataBackend();

	void Close();

	/** Return a name for this interface */
	virtual FString GetName() const override { return Filename; }

	/** return true if this cache is writable **/
	virtual bool IsWritable() const override;

	/** Returns a class of speed for this interface **/
	virtual ESpeedClass GetSpeedClass() const override;

	virtual bool BackfillLowerCacheLevels() const override;

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
	 * @return	true if file was saved sucessfully
	 */
	bool SaveCache();

	/**
	 * Load the cache to disk	 * @param	Filename	Filename to load
	 * @return	true if file was loaded sucessfully
	 */
	bool LoadCache(const TCHAR* InFilename);

	/**
	 * Merges another cache file into this one.
	 * @return true on success
	 */
	void MergeCache(FPakFileDerivedDataBackend* OtherPak);
	
	const FString& GetFilename() const
	{
		return Filename;
	}

	static bool SortAndCopy(ICacheFactory& InFactory, const FString &InputFilename, const FString &OutputFilename);

	virtual TSharedRef<FDerivedDataCacheStatsNode> GatherUsageStats() const override;

	virtual bool TryToPrefetch(TConstArrayView<FString> CacheKeys) override { return false; }

	virtual bool WouldCache(const TCHAR* CacheKey, TArrayView<const uint8> InData) override { return true; }

	virtual bool ApplyDebugOptions(FBackendDebugOptions& InOptions) override { return false; }

	virtual FRequest Put(
		TConstArrayView<FCacheRecord> Records,
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
	FDerivedDataCacheUsageStats UsageStats;

	struct FCacheValue
	{
		int64 Offset;
		int64 Size;
		uint32 Crc;
		FCacheValue(int64 InOffset, uint32 InSize, uint32 InCrc)
			: Offset(InOffset)
			, Size(InSize)
			, Crc(InCrc)
		{
		}
	};

	ICacheFactory& Factory;
	/** When set to true, we are a pak writer (we don't do reads). */
	bool bWriting;
	/** When set to true, we are a pak writer and we saved, so we shouldn't be used anymore. Also, a read cache that failed to open. */
	bool bClosed;
	/** Object used for synchronization via a scoped lock						*/
	FCriticalSection	SynchronizationObject;
	/** Set of files that are being written to disk asynchronously. */
	TMap<FString, FCacheValue> CacheItems;
	/** File handle of pak. */
	TUniquePtr<FArchive> FileHandle;
	/** File name of pak. */
	FString Filename;
	enum 
	{
		/** Magic number to use in header */
		PakCache_Magic = 0x0c7c0ddc,
	};
};

class FCompressedPakFileDerivedDataBackend : public FPakFileDerivedDataBackend
{
public:
	FCompressedPakFileDerivedDataBackend(ICacheFactory& InFactory, const TCHAR* InFilename, bool bInWriting);

	virtual EPutStatus PutCachedData(const TCHAR* CacheKey, TArrayView<const uint8> InData, bool bPutEvenIfExists) override;
	virtual bool GetCachedData(const TCHAR* CacheKey, TArray<uint8>& OutData) override;

	/** Returns a class of speed for this interface **/
	virtual ESpeedClass GetSpeedClass() const override
	{
		return ESpeedClass::Fast;
	}

private:
	static const EName CompressionFormat = NAME_Zlib;
	static const ECompressionFlags CompressionFlags = COMPRESS_BiasMemory;
};

} // UE::DerivedData::Backends

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/StringFwd.h"
#include "DerivedDataBackendInterface.h"
#include "DerivedDataCacheUsageStats.h"
#include "HAL/CriticalSection.h"
#include "ProfilingDebugging/CookStats.h"
#include "Templates/UniquePtr.h"

class FCompressedBuffer;
class IFileHandle;

namespace UE::DerivedData { class FOptionalCacheRecord; }
namespace UE::DerivedData { class FValueWithId; }

namespace UE::DerivedData::CacheStore::PakFile
{

/** 
 * A simple thread safe, pak file based backend. 
**/
class FPakFileDerivedDataBackend : public FDerivedDataBackendInterface
{
public:
	FPakFileDerivedDataBackend(const TCHAR* InFilename, bool bInWriting);
	~FPakFileDerivedDataBackend();

	void Close();

	/** Return a name for this interface */
	virtual FString GetName() const override;

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
		return CachePath;
	}

	static bool SortAndCopy(const FString &InputFilename, const FString &OutputFilename);

	virtual TSharedRef<FDerivedDataCacheStatsNode> GatherUsageStats() const override;

	virtual bool TryToPrefetch(TConstArrayView<FString> CacheKeys) override
	{
		return CachedDataProbablyExistsBatch(CacheKeys).CountSetBits() == CacheKeys.Num();
	}

	virtual bool WouldCache(const TCHAR* CacheKey, TArrayView<const uint8> InData) override { return true; }

	virtual bool ApplyDebugOptions(FBackendDebugOptions& InOptions) override { return false; }

	virtual void Put(
		TConstArrayView<FCachePutRequest> Requests,
		IRequestOwner& Owner,
		FOnCachePutComplete&& OnComplete) override;

	virtual void Get(
		TConstArrayView<FCacheGetRequest> Requests,
		IRequestOwner& Owner,
		FOnCacheGetComplete&& OnComplete) override;

	virtual void GetChunks(
		TConstArrayView<FCacheChunkRequest> Requests,
		IRequestOwner& Owner,
		FOnCacheChunkComplete&& OnComplete) override;

private:
	bool PutCacheRecord(FStringView Name, const FCacheRecord& Record, const FCacheRecordPolicy& Policy);
	bool PutCacheContent(FStringView Name, const FCompressedBuffer& Content);

	FOptionalCacheRecord GetCacheRecordOnly(
		FStringView Name,
		const FCacheKey& Key,
		const FCacheRecordPolicy& Policy);
	FOptionalCacheRecord GetCacheRecord(
		FStringView Name,
		const FCacheKey& Key,
		const FCacheRecordPolicy& Policy,
		EStatus& OutStatus);
	void GetCacheContent(
		FStringView Name,
		const FCacheKey& Key,
		ECachePolicy Policy,
		FValueWithId& InOutValue,
		EStatus& InOutStatus);

	bool SaveFile(FStringView Path, FStringView DebugName, TFunctionRef<void (FArchive&)> WriteFunction);
	FSharedBuffer LoadFile(FStringView Path, FStringView DebugName);
	bool FileExists(FStringView Path);

private:
	FDerivedDataCacheUsageStats UsageStats;

	struct FCacheValue
	{
		int64 Offset;
		int64 Size;
		uint32 Crc;
		FCacheValue(int64 InOffset, int64 InSize, uint32 InCrc)
			: Offset(InOffset)
			, Size(InSize)
			, Crc(InCrc)
		{
		}
	};

	/** When set to true, we are a pak writer (we don't do reads). */
	bool bWriting;
	/** When set to true, we are a pak writer and we saved, so we shouldn't be used anymore. Also, a read cache that failed to open. */
	bool bClosed;
	/** Object used for synchronization via scoped read or write locks. */
	FRWLock SynchronizationObject;
	/** Set of files that are being written to disk asynchronously. */
	TMap<FString, FCacheValue> CacheItems;
	/** File handle of pak. */
	TUniquePtr<IFileHandle> FileHandle;
	/** File name of pak. */
	FString CachePath;

	/** Maximum total size of compressed data stored within a record package with multiple attachments. */
	uint64 MaxRecordSizeKB = 256;
	/** Maximum total size of compressed data stored within a value package, or a record package with one attachment. */
	uint64 MaxValueSizeKB = 1024;

	enum 
	{
		/** Magic number to use in header */
		PakCache_Magic = 0x0c7c0ddc,
	};
};

class FCompressedPakFileDerivedDataBackend : public FPakFileDerivedDataBackend
{
public:
	FCompressedPakFileDerivedDataBackend(const TCHAR* InFilename, bool bInWriting);

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

} // UE::DerivedData::CacheStore::PakFile

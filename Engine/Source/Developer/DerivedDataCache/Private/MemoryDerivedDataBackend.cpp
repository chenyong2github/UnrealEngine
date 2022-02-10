// Copyright Epic Games, Inc. All Rights Reserved.

#include "Algo/AllOf.h"
#include "DerivedDataCachePrivate.h"
#include "DerivedDataCacheRecord.h"
#include "DerivedDataCacheUsageStats.h"
#include "DerivedDataValue.h"
#include "FileBackedDerivedDataBackend.h"
#include "HAL/FileManager.h"
#include "Misc/ScopeExit.h"
#include "Misc/ScopeLock.h"
#include "Misc/ScopeRWLock.h"
#include "ProfilingDebugging/CookStats.h"
#include "Serialization/CompactBinary.h"
#include "Templates/UniquePtr.h"

namespace UE::DerivedData::CacheStore::Memory
{

/**
 * A simple thread safe, memory based backend. This is used for Async puts and the boot cache.
 */
class FMemoryDerivedDataBackend : public FFileBackedDerivedDataBackend
{
public:
	explicit FMemoryDerivedDataBackend(const TCHAR* InName, int64 InMaxCacheSize = -1, bool bCanBeDisabled = false);
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

	virtual TBitArray<> TryToPrefetch(TConstArrayView<FString> CacheKeys) override;

	/**
	 *  Determines if we would cache the provided data
	 */
	virtual bool WouldCache(const TCHAR* CacheKey, TArrayView<const uint8> InData) override;

	/**
	 * Apply debug options
	 */
	bool ApplyDebugOptions(FBackendDebugOptions& InOptions) override;

	virtual void Put(
		TConstArrayView<FCachePutRequest> Requests,
		IRequestOwner& Owner,
		FOnCachePutComplete&& OnComplete) override;

	virtual void Get(
		TConstArrayView<FCacheGetRequest> Requests,
		IRequestOwner& Owner,
		FOnCacheGetComplete&& OnComplete) override;

	virtual void PutValue(
		TConstArrayView<FCachePutValueRequest> Requests,
		IRequestOwner& Owner,
		FOnCachePutValueComplete&& OnComplete) override;

	virtual void GetValue(
		TConstArrayView<FCacheGetValueRequest> Requests,
		IRequestOwner& Owner,
		FOnCacheGetValueComplete&& OnComplete) override;

	virtual void GetChunks(
		TConstArrayView<FCacheGetChunkRequest> Requests,
		IRequestOwner& Owner,
		FOnCacheGetChunkComplete&& OnComplete) override;

private:
	/** Name of the cache file loaded (if any). */
	FString CacheFilename;
	FDerivedDataCacheUsageStats UsageStats;

	struct FCacheValue
	{
		int32 Age;
		int32 Size;
		FRWLock DataLock;
		TArray<uint8> Data;
		FCacheValue(int32 InSize, int32 InAge = 0)
			: Age(InAge)
			, Size(InSize)
		{
		}

		~FCacheValue()
		{
			// Ensure we don't begin destruction of this value while our own PutCachedData function is holding this lock
			FWriteScopeLock WriteLock(DataLock);
		}
	};

	FORCEINLINE int32 CalcSerializedCacheValueSize(const FString& Key, const FCacheValue& Val)
	{
		return (Key.Len() + 1) * sizeof(TCHAR) + sizeof(FCacheValue::Age) + Val.Size;
	}

	FORCEINLINE int32 CalcSerializedCacheValueSize(const FString& Key, const TArrayView<const uint8>& Data)
	{
		return (Key.Len() + 1) * sizeof(TCHAR) + sizeof(FCacheValue::Age) + Data.Num();
	}

	/** Name of this cache (used for debugging) */
	FString Name;

	/** Set of files that are being written to disk asynchronously. */
	TMap<FString, FCacheValue*> CacheItems;
	/** Set of records in this cache. */
	TSet<FCacheRecord, FCacheRecordKeyFuncs> CacheRecords;
	/** Set of values in this cache. */
	TMap<FCacheKey, FValue> CacheValues;
	/** Maximum size the cached items can grow up to ( in bytes ) */
	int64 MaxCacheSize;
	/** When set to true, this cache is disabled...ignore all requests. */
	std::atomic<bool> bDisabled;
	/** Object used for synchronization via a scoped lock						*/
	mutable FRWLock SynchronizationObject;
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
	bool ShouldSimulateMiss(const TCHAR* InKey);
	bool ShouldSimulateMiss(const FCacheKey& InKey);

	/** Debug Options */
	FBackendDebugOptions DebugOptions;

	/** Keys we ignored due to miss rate settings */
	FCriticalSection MissedKeysCS;
	TSet<FName> DebugMissedKeys;
	TSet<FCacheKey> DebugMissedCacheKeys;
};

FMemoryDerivedDataBackend::FMemoryDerivedDataBackend(const TCHAR* InName, int64 InMaxCacheSize, bool bInCanBeDisabled)
	: Name(InName)
	, MaxCacheSize(InMaxCacheSize)
	, bDisabled( false )
	, CurrentCacheSize( SerializationSpecificDataSize )
	, bMaxSizeExceeded(false)
	, bCanBeDisabled(bInCanBeDisabled)
{
}

FMemoryDerivedDataBackend::~FMemoryDerivedDataBackend()
{
	bShuttingDown = true;
	Disable();
}

bool FMemoryDerivedDataBackend::IsWritable() const
{
	return !bDisabled;
}

FDerivedDataBackendInterface::ESpeedClass FMemoryDerivedDataBackend::GetSpeedClass() const
{
	return ESpeedClass::Local;
}

bool FMemoryDerivedDataBackend::CachedDataProbablyExists(const TCHAR* CacheKey)
{
	// See comments on the declaration of bCanBeDisabled variable.
	if (bCanBeDisabled)
	{
		return false;
	}

	COOK_STAT(auto Timer = UsageStats.TimeProbablyExists());

	if (ShouldSimulateMiss(CacheKey))
	{
		return false;
	}

	if (bDisabled)
	{
		return false;
	}

	FReadScopeLock ScopeLock(SynchronizationObject);
	bool Result = CacheItems.Contains(FString(CacheKey));
	if (Result)
	{
		COOK_STAT(Timer.AddHit(0));
	}
	return Result;
}

bool FMemoryDerivedDataBackend::GetCachedData(const TCHAR* CacheKey, TArray<uint8>& OutData)
{
	COOK_STAT(auto Timer = UsageStats.TimeGet());

	if (ShouldSimulateMiss(CacheKey))
	{
		return false;
	}
	
	if (!bDisabled)
	{
		FReadScopeLock ScopeLock(SynchronizationObject);

		FCacheValue* Item = CacheItems.FindRef(FString(CacheKey));
		if (Item)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(FMemoryDerivedDataBackend::GetCachedData);
			FReadScopeLock ItemLock(Item->DataLock);

			OutData = Item->Data;
			Item->Age = 0;
			check(OutData.Num());
			COOK_STAT(Timer.AddHit(OutData.Num()));
			return true;
		}
	}
	OutData.Empty();
	return false;
}

TBitArray<> FMemoryDerivedDataBackend::TryToPrefetch(TConstArrayView<FString> CacheKeys)
{
	return CachedDataProbablyExistsBatch(CacheKeys);
}

bool FMemoryDerivedDataBackend::WouldCache(const TCHAR* CacheKey, TArrayView<const uint8> InData)
{
	if (bDisabled || bMaxSizeExceeded)
	{
		return false;
	}

	return true;
}

FDerivedDataBackendInterface::EPutStatus FMemoryDerivedDataBackend::PutCachedData(const TCHAR* CacheKey, TArrayView<const uint8> InData, bool bPutEvenIfExists)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FMemoryDerivedDataBackend::PutCachedData);
	COOK_STAT(auto Timer = UsageStats.TimePut());

	FString Key;
	{
		FReadScopeLock ReadScopeLock(SynchronizationObject);

		if (ShouldSimulateMiss(CacheKey))
		{
			return EPutStatus::Skipped;
		}

		// Should never hit this as higher level code should be checking..
		if (!WouldCache(CacheKey, InData))
		{
			//UE_LOG(LogDerivedDataCache, Warning, TEXT("WouldCache was not called prior to attempted Put!"));
			return EPutStatus::NotCached;
		}

		Key = CacheKey;
		FCacheValue* Item = CacheItems.FindRef(Key);
		if (Item)
		{
			//check(Item->Data == InData); // any second attempt to push data should be identical data
			return EPutStatus::Cached;
		}
	}
	
	// Compute the size of the cache before copying the data to avoid alloc/memcpy of something we might throw away
	// if we bust the cache size.
	int32 CacheValueSize = CalcSerializedCacheValueSize(Key, InData);

	FCacheValue* Val = nullptr;

	{
		FWriteScopeLock WriteScopeLock(SynchronizationObject);

		// check if we haven't exceeded the MaxCacheSize
		if (MaxCacheSize > 0 && (CurrentCacheSize + CacheValueSize) > MaxCacheSize)
		{
			UE_LOG(LogDerivedDataCache, Display, TEXT("Failed to cache data. Maximum cache size reached. CurrentSize %d kb / MaxSize: %d kb"), CurrentCacheSize / 1024, MaxCacheSize / 1024);
			bMaxSizeExceeded = true;
			return EPutStatus::NotCached;
		}

		if (CacheItems.Contains(Key))
		{
			// Another thread already beat us, just return.
			return EPutStatus::Cached;
		}

		COOK_STAT(Timer.AddHit(InData.Num()));
		Val = new FCacheValue(InData.Num());
		// Make sure no other thread can access the data until we finish copying it
		Val->DataLock.WriteLock();
		CacheItems.Add(Key, Val);

		CurrentCacheSize += CacheValueSize;
	}

	// Data is copied outside of the lock so that we only lock
	// for this specific key instead of always locking all keys by default.
	Val->Data = InData;
	// It is now safe for other thread to access this data, unlock
	Val->DataLock.WriteUnlock();

	return EPutStatus::Cached;
}

void FMemoryDerivedDataBackend::RemoveCachedData(const TCHAR* CacheKey, bool bTransient)
{
	if (bDisabled || bTransient)
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(FMemoryDerivedDataBackend::RemoveCachedData);
	FString Key(CacheKey);
	FCacheValue* Item = nullptr;
	{
		FWriteScopeLock WriteScopeLock(SynchronizationObject);
		
		if (CacheItems.RemoveAndCopyValue(Key, Item) && Item)
		{
			CurrentCacheSize -= CalcSerializedCacheValueSize(Key, *Item);
			bMaxSizeExceeded = false;
		}
	}

	if (Item)
	{
		// Just make sure any other thread has finished writing or reading the data before deleting.
		Item->DataLock.WriteLock();

		// Avoid deleting the item with a locked FRWLock, unlocking is safe because
		// the item is now unreachable from other threads
		Item->DataLock.WriteUnlock();
		delete Item;
	}
}

bool FMemoryDerivedDataBackend::SaveCache(const TCHAR* Filename)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FMemoryDerivedDataBackend::SaveCache);

	double StartTime = FPlatformTime::Seconds();
	TUniquePtr<FArchive> SaverArchive(IFileManager::Get().CreateFileWriter(Filename, FILEWRITE_EvenIfReadOnly));
	if (!SaverArchive)
	{
		UE_LOG(LogDerivedDataCache, Error, TEXT("Could not save memory cache %s."), Filename);
		return false;
	}

	FArchive& Saver = *SaverArchive;
	uint32 Magic = MemCache_Magic64;
	Saver << Magic;
	const int64 DataStartOffset = Saver.Tell();
	{
		FWriteScopeLock ScopeLock(SynchronizationObject);
		check(!bDisabled);
		for (TMap<FString, FCacheValue*>::TIterator It(CacheItems); It; ++It )
		{
			Saver << It.Key();
			Saver << It.Value()->Age;
			FReadScopeLock ReadScopeLock(It.Value()->DataLock);
			Saver << It.Value()->Data;
		}
	}
	const int64 DataSize = Saver.Tell(); // Everything except the footer
	int64 Size = DataSize;
	uint32 Crc = MemCache_Magic64; // Crc takes more time than I want to spend  FCrc::MemCrc_DEPRECATED(&Buffer[0], Size);
	Saver << Size;
	Saver << Crc;

	check(SerializationSpecificDataSize + DataSize <= MaxCacheSize || MaxCacheSize <= 0);

	UE_LOG(LogDerivedDataCache, Log, TEXT("Saved boot cache %4.2fs %lldMB %s."), float(FPlatformTime::Seconds() - StartTime), DataSize / (1024 * 1024), Filename);
	return true;
}

bool FMemoryDerivedDataBackend::LoadCache(const TCHAR* Filename)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FMemoryDerivedDataBackend::LoadCache);

	double StartTime = FPlatformTime::Seconds();
	const int64 FileSize = IFileManager::Get().FileSize(Filename);
	if (FileSize < 0)
	{
		UE_LOG(LogDerivedDataCache, Warning, TEXT("Could not find memory cache %s."), Filename);
		return false;
	}
	// We test 3 * uint32 which is the old format (< SerializationSpecificDataSize). We'll test
	// against SerializationSpecificDataSize later when we read the magic number from the cache.
	if (FileSize < sizeof(uint32) * 3)
	{
		UE_LOG(LogDerivedDataCache, Error, TEXT("Memory cache was corrputed (short) %s."), Filename);
		return false;
	}
	if (FileSize > MaxCacheSize*2 && MaxCacheSize > 0)
	{
		UE_LOG(LogDerivedDataCache, Error, TEXT("Refusing to load DDC cache %s. Size exceeds doubled MaxCacheSize."), Filename);
		return false;
	}

	TUniquePtr<FArchive> LoaderArchive(IFileManager::Get().CreateFileReader(Filename));
	if (!LoaderArchive)
	{
		UE_LOG(LogDerivedDataCache, Warning, TEXT("Could not read memory cache %s."), Filename);
		return false;
	}

	FArchive& Loader = *LoaderArchive;
	uint32 Magic = 0;
	Loader << Magic;
	if (Magic != MemCache_Magic && Magic != MemCache_Magic64)
	{
		UE_LOG(LogDerivedDataCache, Error, TEXT("Memory cache was corrputed (magic) %s."), Filename);
		return false;
	}
	// Check the file size again, this time against the correct minimum size.
	if (Magic == MemCache_Magic64 && FileSize < SerializationSpecificDataSize)
	{
		UE_LOG(LogDerivedDataCache, Error, TEXT("Memory cache was corrputed (short) %s."), Filename);
		return false;
	}
	// Calculate expected DataSize based on the magic number (footer size difference)
	const int64 DataSize = FileSize - (Magic == MemCache_Magic64 ? (SerializationSpecificDataSize - sizeof(uint32)) : (sizeof(uint32) * 2));		
	Loader.Seek(DataSize);
	int64 Size = 0;
	uint32 Crc = 0;
	if (Magic == MemCache_Magic64)
	{
		Loader << Size;
	}
	else
	{
		uint32 Size32 = 0;
		Loader << Size32;
		Size = (int64)Size32;
	}
	Loader << Crc;
	if (Size != DataSize)
	{
		UE_LOG(LogDerivedDataCache, Error, TEXT("Memory cache was corrputed (size) %s."), Filename);
		return false;
	}
	if ((Crc != MemCache_Magic && Crc != MemCache_Magic64) || Crc != Magic)
	{
		UE_LOG(LogDerivedDataCache, Warning, TEXT("Memory cache was corrputed (crc) %s."), Filename);
		return false;
	}
	// Seek to data start offset (skip magic number)
	Loader.Seek(sizeof(uint32));
	{
		TArray<uint8> Working;
		FWriteScopeLock ScopeLock(SynchronizationObject);
		check(!bDisabled);
		while (Loader.Tell() < DataSize)
		{
			FString Key;
			int32 Age;
			Loader << Key;
			Loader << Age;
			Age++;
			Loader << Working;
			if (Age < MaxAge)
			{
				CacheItems.Add(Key, new FCacheValue(Working.Num(), Age))->Data = MoveTemp(Working);
			}
			Working.Reset();
		}
		// these are just a double check on ending correctly
		if (Magic == MemCache_Magic64)
		{
			Loader << Size;
		}
		else
		{
			uint32 Size32 = 0;
			Loader << Size32;
			Size = (int64)Size32;
		}
		Loader << Crc;

		CurrentCacheSize = FileSize;
		CacheFilename = Filename;
	}

	UE_LOG(LogDerivedDataCache, Log, TEXT("Loaded boot cache %4.2fs %lldMB %s."), float(FPlatformTime::Seconds() - StartTime), DataSize / (1024 * 1024), Filename);
	return true;
}

void FMemoryDerivedDataBackend::Disable()
{
	check(bCanBeDisabled || bShuttingDown);
	FWriteScopeLock ScopeLock(SynchronizationObject);
	bDisabled = true;
	for (TMap<FString,FCacheValue*>::TIterator It(CacheItems); It; ++It )
	{
		delete It.Value();
	}
	CacheItems.Empty();

	CurrentCacheSize = SerializationSpecificDataSize;
}

TSharedRef<FDerivedDataCacheStatsNode> FMemoryDerivedDataBackend::GatherUsageStats() const
{
	TSharedRef<FDerivedDataCacheStatsNode> Usage =
		MakeShared<FDerivedDataCacheStatsNode>(CacheFilename.IsEmpty() ? TEXT("Memory") : TEXT("Boot"), CacheFilename, /*bIsLocal*/ true);
	Usage->Stats.Add(TEXT(""), UsageStats);
	return Usage;
}

bool FMemoryDerivedDataBackend::ApplyDebugOptions(FBackendDebugOptions& InOptions)
{
	DebugOptions = InOptions;
	return true;
}

bool FMemoryDerivedDataBackend::ShouldSimulateMiss(const TCHAR* InKey)
{
	if (DebugOptions.RandomMissRate == 0 && DebugOptions.SimulateMissTypes.IsEmpty())
	{
		return false;
	}

	const FName Key(InKey);
	const uint32 Hash = GetTypeHash(Key);

	if (FScopeLock Lock(&MissedKeysCS); DebugMissedKeys.ContainsByHash(Hash, Key))
	{
		return true;
	}

	if (DebugOptions.ShouldSimulateMiss(InKey))
	{
		FScopeLock Lock(&MissedKeysCS);
		UE_LOG(LogDerivedDataCache, Verbose, TEXT("Simulating miss in %s for %s"), *GetName(), InKey);
		DebugMissedKeys.AddByHash(Hash, Key);
		return true;
	}

	return false;
}

bool FMemoryDerivedDataBackend::ShouldSimulateMiss(const FCacheKey& Key)
{
	if (DebugOptions.RandomMissRate == 0 && DebugOptions.SimulateMissTypes.IsEmpty())
	{
		return false;
	}

	const uint32 Hash = GetTypeHash(Key);

	if (FScopeLock Lock(&MissedKeysCS); DebugMissedCacheKeys.ContainsByHash(Hash, Key))
	{
		return true;
	}

	if (DebugOptions.ShouldSimulateMiss(Key))
	{
		FScopeLock Lock(&MissedKeysCS);
		DebugMissedCacheKeys.AddByHash(Hash, Key);
		return true;
	}

	return false;
}

void FMemoryDerivedDataBackend::Put(
	const TConstArrayView<FCachePutRequest> Requests,
	IRequestOwner& Owner,
	FOnCachePutComplete&& OnComplete)
{
	for (const FCachePutRequest& Request : Requests)
	{
		const FCacheRecord& Record = Request.Record;
		const FCacheKey& Key = Record.GetKey();
		EStatus Status = EStatus::Error;
		ON_SCOPE_EXIT
		{
			if (OnComplete)
			{
				OnComplete({Request.Name, Key, Request.UserData, Status});
			}
		};

		if (!EnumHasAnyFlags(Request.Policy.GetRecordPolicy(), ECachePolicy::StoreLocal))
		{
			continue;
		}

		if (ShouldSimulateMiss(Key))
		{
			UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Simulated miss for put of %s from '%s'"),
				*GetName(), *WriteToString<96>(Key), *Request.Name);
			continue;
		}

		const TConstArrayView<FValueWithId> Values = Record.GetValues();

		if (!Algo::AllOf(Values, &FValue::HasData))
		{
			continue;
		}

		COOK_STAT(auto Timer = UsageStats.TimePut());
		const int64 RecordSize = Private::GetCacheRecordCompressedSize(Record);
		const bool bReplaceExisting = !EnumHasAnyFlags(Request.Policy.GetRecordPolicy(), ECachePolicy::QueryLocal);

		FWriteScopeLock ScopeLock(SynchronizationObject);
		FCacheRecord* const ExistingRecord = CacheRecords.Find(Key);
		Status = ExistingRecord && !bReplaceExisting ? EStatus::Ok : EStatus::Error;
		if (bDisabled || Status == EStatus::Ok)
		{
			continue;
		}

		const int64 ExistingRecordSize = ExistingRecord ? Private::GetCacheRecordCompressedSize(*ExistingRecord) : 0;
		const int64 RequiredSize = RecordSize - ExistingRecordSize;

		if (MaxCacheSize > 0 && (CurrentCacheSize + RequiredSize) > MaxCacheSize)
		{
			UE_LOG(LogDerivedDataCache, Display, TEXT("Failed to cache data. Maximum cache size reached. CurrentSize %" INT64_FMT " KiB / MaxSize: %" INT64_FMT " KiB"), CurrentCacheSize / 1024, MaxCacheSize / 1024);
			bMaxSizeExceeded = true;
			continue;
		}

		CurrentCacheSize += RequiredSize;
		if (ExistingRecord)
		{
			*ExistingRecord = Record;
		}
		else
		{
			CacheRecords.Add(Record);
		}
		COOK_STAT(Timer.AddHit(RecordSize));
		Status = EStatus::Ok;
	}
}

void FMemoryDerivedDataBackend::Get(
	const TConstArrayView<FCacheGetRequest> Requests,
	IRequestOwner& Owner,
	FOnCacheGetComplete&& OnComplete)
{
	if (bDisabled)
	{
		for (const FCacheGetRequest& Request : Requests)
		{
			if (OnComplete)
			{
				OnComplete({ Request.Name, FCacheRecordBuilder(Request.Key).Build(), Request.UserData, EStatus::Error });
			}
		}
		return;
	}

	for (const FCacheGetRequest& Request : Requests)
	{
		const FCacheKey& Key = Request.Key;
		const FCacheRecordPolicy& Policy = Request.Policy;
		const bool bExistsOnly = EnumHasAllFlags(Policy.GetRecordPolicy(), ECachePolicy::SkipData);
		COOK_STAT(auto Timer = bExistsOnly ? UsageStats.TimeProbablyExists() : UsageStats.TimeGet());
		FOptionalCacheRecord Record;
		EStatus Status = EStatus::Error;
		if (ShouldSimulateMiss(Key))
		{
			UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Simulated miss for get of %s from '%s'"),
				*GetName(), *WriteToString<96>(Key), *Request.Name);
		}
		else if (FReadScopeLock ScopeLock(SynchronizationObject); const FCacheRecord* CacheRecord = CacheRecords.Find(Key))
		{
			Status = EStatus::Ok;
			Record = *CacheRecord;
		}
		if (Record)
		{
			for (const FValueWithId& Value : Record.Get().GetValues())
			{
				if (!Value.HasData() && !EnumHasAnyFlags(Policy.GetValuePolicy(Value.GetId()), ECachePolicy::SkipData))
				{
					Status = EStatus::Error;
					if (!EnumHasAllFlags(Policy.GetRecordPolicy(), ECachePolicy::PartialRecord))
					{
						Record.Reset();
						break;
					}
				}
			}
		}
		if (Record)
		{
			COOK_STAT(Timer.AddHit(Private::GetCacheRecordCompressedSize(Record.Get())));
			if (OnComplete)
			{
				OnComplete({Request.Name, MoveTemp(Record).Get(), Request.UserData, Status});
			}
		}
		else
		{
			if (OnComplete)
			{
				OnComplete({Request.Name, FCacheRecordBuilder(Key).Build(), Request.UserData, EStatus::Error});
			}
		}
	}
}

void FMemoryDerivedDataBackend::PutValue(
	const TConstArrayView<FCachePutValueRequest> Requests,
	IRequestOwner& Owner,
	FOnCachePutValueComplete&& OnComplete)
{
	for (const FCachePutValueRequest& Request : Requests)
	{
		const FValue& Value = Request.Value;
		const FCacheKey& Key = Request.Key;
		EStatus Status = EStatus::Error;
		ON_SCOPE_EXIT
		{
			if (OnComplete)
			{
				OnComplete({Request.Name, Key, Request.UserData, Status});
			}
		};

		if (!EnumHasAnyFlags(Request.Policy, ECachePolicy::StoreLocal))
		{
			continue;
		}

		if (ShouldSimulateMiss(Key))
		{
			UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Simulated miss for put of %s from '%s'"),
				*GetName(), *WriteToString<96>(Key), *Request.Name);
			continue;
		}

		if (!Request.Value.HasData())
		{
			continue;
		}

		COOK_STAT(auto Timer = UsageStats.TimePut());
		const int64 ValueSize = Value.GetData().GetCompressedSize();
		const bool bReplaceExisting = !EnumHasAnyFlags(Request.Policy, ECachePolicy::QueryLocal);

		FWriteScopeLock ScopeLock(SynchronizationObject);
		FValue* const ExistingValue = CacheValues.Find(Key);
		Status = ExistingValue && !bReplaceExisting ? EStatus::Ok : EStatus::Error;
		if (bDisabled || Status == EStatus::Ok)
		{
			continue;
		}

		const int64 ExistingValueSize = ExistingValue ? ExistingValue->GetData().GetCompressedSize() : 0;
		const int64 RequiredSize = ValueSize - ExistingValueSize;

		if (MaxCacheSize > 0 && (CurrentCacheSize + RequiredSize) > MaxCacheSize)
		{
			UE_LOG(LogDerivedDataCache, Display, TEXT("Failed to cache data. Maximum cache size reached. CurrentSize %" INT64_FMT " KiB / MaxSize: %" INT64_FMT " KiB"), CurrentCacheSize / 1024, MaxCacheSize / 1024);
			bMaxSizeExceeded = true;
			continue;
		}

		CurrentCacheSize += RequiredSize;
		if (ExistingValue)
		{
			*ExistingValue = Value;
		}
		else
		{
			CacheValues.Add(Key, Value);
		}
		COOK_STAT(Timer.AddHit(ValueSize));
		Status = EStatus::Ok;
	}
}

void FMemoryDerivedDataBackend::GetValue(
	const TConstArrayView<FCacheGetValueRequest> Requests,
	IRequestOwner& Owner,
	FOnCacheGetValueComplete&& OnComplete)
{
	if (bDisabled)
	{
		CompleteWithStatus(Requests, OnComplete, EStatus::Error);
		return;
	}

	for (const FCacheGetValueRequest& Request : Requests)
	{
		const FCacheKey& Key = Request.Key;
		const ECachePolicy Policy = Request.Policy;
		const bool bExistsOnly = EnumHasAllFlags(Policy, ECachePolicy::SkipData);
		COOK_STAT(auto Timer = bExistsOnly ? UsageStats.TimeProbablyExists() : UsageStats.TimeGet());
		bool bProcessHit = false;
		FValue Value;
		EStatus Status = EStatus::Error;
		if (ShouldSimulateMiss(Key))
		{
			UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Simulated miss for get of %s from '%s'"),
				*GetName(), *WriteToString<96>(Key), *Request.Name);
		}
		else if (FReadScopeLock ScopeLock(SynchronizationObject); const FValue* CacheValue = CacheValues.Find(Key))
		{
			Status = EStatus::Ok;
			Value = *CacheValue;
			bProcessHit = true;
		}

		if (bProcessHit)
		{
			if (!Value.HasData() && !EnumHasAnyFlags(Policy, ECachePolicy::SkipData))
			{
				Status = EStatus::Error;
			}

			COOK_STAT(Timer.AddHit(Value.GetData().GetCompressedSize()));
			if (OnComplete)
			{
				OnComplete({ Request.Name, Request.Key, EnumHasAnyFlags(Policy, ECachePolicy::SkipData) ? Value.RemoveData() : Value, Request.UserData, Status });
			}
		}
		else
		{
			if (OnComplete)
			{
				OnComplete({ Request.Name, Request.Key, {}, Request.UserData, EStatus::Error });
			}
		}
	}
}

void FMemoryDerivedDataBackend::GetChunks(
	const TConstArrayView<FCacheGetChunkRequest> Requests,
	IRequestOwner& Owner,
	FOnCacheGetChunkComplete&& OnComplete)
{
	bool bHasValue = false;
	FValue Value;
	FValueId ValueId;
	FCacheKey ValueKey;
	FCompressedBufferReader Reader;
	for (const FCacheGetChunkRequest& Request : Requests)
	{
		bool bProcessHit = false;
		const bool bExistsOnly = EnumHasAnyFlags(Request.Policy, ECachePolicy::SkipData);
		COOK_STAT(auto Timer = bExistsOnly ? UsageStats.TimeProbablyExists() : UsageStats.TimeGet());
		if (ShouldSimulateMiss(Request.Key))
		{
			UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Simulated miss for get of %s from '%s'"),
				*GetName(), *WriteToString<96>(Request.Key, '/', Request.Id), *Request.Name);
		}
		else if (bHasValue && (ValueKey == Request.Key) && (ValueId == Request.Id) && (bExistsOnly || Reader.HasSource()))
		{
			// Value matches the request.
			bProcessHit = true;
		}
		else
		{
			FReadScopeLock ScopeLock(SynchronizationObject);
			if (Request.Id.IsValid())
			{
				if (const FCacheRecord* Record = CacheRecords.Find(Request.Key))
				{
					const FValueWithId& ValueWithId = Record->GetValue(Request.Id);
					bHasValue = ValueWithId.IsValid();
					Reader.ResetSource();
					Value.Reset();
					Value = ValueWithId;
					ValueId = Request.Id;
					ValueKey = Request.Key;
					Reader.SetSource(Value.GetData());
					bProcessHit = true;
				}
			}
			else
			{
				if (const FValue* ExistingValue = CacheValues.Find(Request.Key))
				{
					bHasValue = true;
					Reader.ResetSource();
					Value.Reset();
					Value = *ExistingValue;
					ValueId.Reset();
					ValueKey = Request.Key;
					Reader.SetSource(Value.GetData());
					bProcessHit = true;
				}
			}
		}

		if (bProcessHit && Request.RawOffset <= Value.GetRawSize())
		{
			const uint64 RawSize = FMath::Min(Value.GetRawSize() - Request.RawOffset, Request.RawSize);
			COOK_STAT(Timer.AddHit(RawSize));
			if (OnComplete)
			{
				FSharedBuffer Buffer;
				if (Value.HasData() && !bExistsOnly)
				{
					Buffer = Reader.Decompress(Request.RawOffset, RawSize);
				}
				const EStatus Status = bExistsOnly || Buffer ? EStatus::Ok : EStatus::Error;
				OnComplete({Request.Name, Request.Key, Request.Id, Request.RawOffset,
					RawSize, Value.GetRawHash(), MoveTemp(Buffer), Request.UserData, Status});
			}
		}
		else
		{
			if (OnComplete)
			{
				OnComplete({Request.Name, Request.Key, Request.Id, Request.RawOffset,
					0, {}, {}, Request.UserData, EStatus::Error});
			}
		}
	}
}

FFileBackedDerivedDataBackend* CreateMemoryDerivedDataBackend(const TCHAR* Name, int64 MaxCacheSize, bool bCanBeDisabled)
{
	return new FMemoryDerivedDataBackend(Name, MaxCacheSize, bCanBeDisabled);
}

} // UE::DerivedData::CacheStore::Memory

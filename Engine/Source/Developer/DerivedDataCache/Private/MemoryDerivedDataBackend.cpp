// Copyright Epic Games, Inc. All Rights Reserved.

#include "MemoryDerivedDataBackend.h"
#include "Templates/UniquePtr.h"

FMemoryDerivedDataBackend::FMemoryDerivedDataBackend(const TCHAR* InName, int64 InMaxCacheSize)
	: Name(InName)
	, MaxCacheSize(InMaxCacheSize)
	, bDisabled( false )
	, CurrentCacheSize( SerializationSpecificDataSize )
	, bMaxSizeExceeded(false)
{
}

FMemoryDerivedDataBackend::~FMemoryDerivedDataBackend()
{
	Disable();
}

bool FMemoryDerivedDataBackend::IsWritable() const
{
	FScopeLock ScopeLock(&SynchronizationObject);
	return !bDisabled;
}

FDerivedDataBackendInterface::ESpeedClass FMemoryDerivedDataBackend::GetSpeedClass() const
{
	return ESpeedClass::Local;
}

bool FMemoryDerivedDataBackend::CachedDataProbablyExists(const TCHAR* CacheKey)
{
	COOK_STAT(auto Timer = UsageStats.TimeProbablyExists());

	if (ShouldSimulateMiss(CacheKey))
	{
		return false;
	}

	if (bDisabled)
	{
		return false;
	}

	FScopeLock ScopeLock(&SynchronizationObject);
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
		FScopeLock ScopeLock(&SynchronizationObject);

		FCacheValue* Item = CacheItems.FindRef(FString(CacheKey));
		if (Item)
		{
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

bool FMemoryDerivedDataBackend::TryToPrefetch(const TCHAR* CacheKey)
{
	return false;
}

bool FMemoryDerivedDataBackend::WouldCache(const TCHAR* CacheKey, TArrayView<const uint8> InData)
{
	if (bDisabled || bMaxSizeExceeded)
	{
		return false;
	}

	return true;
}

void FMemoryDerivedDataBackend::PutCachedData(const TCHAR* CacheKey, TArrayView<const uint8> InData, bool bPutEvenIfExists)
{
	COOK_STAT(auto Timer = UsageStats.TimePut());
	FScopeLock ScopeLock(&SynchronizationObject);

	if (DidSimulateMiss(CacheKey))
	{
		return;
	}
	
	// Should never hit this as higher level code should be checking..
	if (!WouldCache(CacheKey, InData))
	{
		//UE_LOG(LogDerivedDataCache, Warning, TEXT("WouldCache was not called prior to attempted Put!"));
		return;
	}
	
	FString Key(CacheKey);
	FCacheValue* Item = CacheItems.FindRef(FString(CacheKey));
	if (Item)
	{
		//check(Item->Data == InData); // any second attempt to push data should be identical data
	}
	else
	{
		FCacheValue* Val = new FCacheValue(InData);
		int32 CacheValueSize = CalcCacheValueSize(Key, *Val);

		// check if we haven't exceeded the MaxCacheSize
		if (MaxCacheSize > 0 && (CurrentCacheSize + CacheValueSize) > MaxCacheSize)
		{
			delete Val;
			UE_LOG(LogDerivedDataCache, Display, TEXT("Failed to cache data. Maximum cache size reached. CurrentSize %d kb / MaxSize: %d kb"), CurrentCacheSize / 1024, MaxCacheSize / 1024);
			bMaxSizeExceeded = true;
		}
		else
		{
			COOK_STAT(Timer.AddHit(InData.Num()));
			CacheItems.Add(Key, Val);
			CalcCacheValueSize(Key, *Val);

			CurrentCacheSize += CacheValueSize;
		}
	}
}

void FMemoryDerivedDataBackend::RemoveCachedData(const TCHAR* CacheKey, bool bTransient)
{
	FScopeLock ScopeLock(&SynchronizationObject);
	if (bDisabled || bTransient)
	{
		return;
	}
	FString Key(CacheKey);
	FCacheValue* Item = NULL;
	if (CacheItems.RemoveAndCopyValue(Key, Item))
	{
		CurrentCacheSize -= CalcCacheValueSize(Key, *Item);
		bMaxSizeExceeded = false;

		check(Item);
		delete Item;
	}
	else
	{
		check(!Item);
	}
}

bool FMemoryDerivedDataBackend::SaveCache(const TCHAR* Filename)
{
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
		FScopeLock ScopeLock(&SynchronizationObject);
		check(!bDisabled);
		for (TMap<FString, FCacheValue*>::TIterator It(CacheItems); It; ++It )
		{
			Saver << It.Key();
			Saver << It.Value()->Age;
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
		FScopeLock ScopeLock(&SynchronizationObject);
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
				CacheItems.Add(Key, new FCacheValue(Working, Age));
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
	}
		
	CurrentCacheSize = FileSize;
	CacheFilename = Filename;
	UE_LOG(LogDerivedDataCache, Log, TEXT("Loaded boot cache %4.2fs %lldMB %s."), float(FPlatformTime::Seconds() - StartTime), DataSize / (1024 * 1024), Filename);
	return true;
}

void FMemoryDerivedDataBackend::Disable()
{
	FScopeLock ScopeLock(&SynchronizationObject);
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
	TSharedRef<FDerivedDataCacheStatsNode> Usage = MakeShared<FDerivedDataCacheStatsNode>(this, FString::Printf(TEXT("%s.%s"), TEXT("MemoryBackend"), *CacheFilename));
	Usage->Stats.Add(TEXT(""), UsageStats);

	return Usage;
}

bool FMemoryDerivedDataBackend::ApplyDebugOptions(FBackendDebugOptions& InOptions)
{
	DebugOptions = InOptions;
	return true;
}

bool FMemoryDerivedDataBackend::DidSimulateMiss(const TCHAR* InKey)
{
	if (DebugOptions.RandomMissRate == 0 || DebugOptions.SimulateMissTypes.Num() == 0)
	{
		return false;
	}
	FScopeLock Lock(&MissedKeysCS);
	return DebugMissedKeys.Contains(FName(InKey));
}

bool FMemoryDerivedDataBackend::ShouldSimulateMiss(const TCHAR* InKey)
{
	// once missed, always missed
	if (DidSimulateMiss(InKey))
	{
		return true;
	}

	if (DebugOptions.ShouldSimulateMiss(InKey))
	{
		FScopeLock Lock(&MissedKeysCS);
		UE_LOG(LogDerivedDataCache, Verbose, TEXT("Simulating miss in %s for %s"), *GetName(), InKey);
		DebugMissedKeys.Add(FName(InKey));
		return true;
	}

	return false;
}
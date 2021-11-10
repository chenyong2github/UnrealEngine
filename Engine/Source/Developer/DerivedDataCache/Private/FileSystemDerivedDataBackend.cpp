// Copyright Epic Games, Inc. All Rights Reserved.

#include "Algo/Accumulate.h"
#include "Algo/AllOf.h"
#include "Algo/StableSort.h"
#include "Algo/Transform.h"
#include "Async/Async.h"
#include "Async/TaskGraphInterfaces.h"
#include "Containers/StaticBitArray.h"
#include "DerivedDataBackendInterface.h"
#include "DerivedDataCacheInterface.h"
#include "DerivedDataCacheMaintainer.h"
#include "DerivedDataCacheRecord.h"
#include "DerivedDataCacheUsageStats.h"
#include "DerivedDataChunk.h"
#include "DerivedDataPayload.h"
#include "Experimental/Async/LazyEvent.h"
#include "Features/IModularFeatures.h"
#include "HAL/Event.h"
#include "HAL/FileManager.h"
#include "HAL/Thread.h"
#include "Hash/xxhash.h"
#include "HashingArchiveProxy.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Misc/PathViews.h"
#include "Misc/ScopeExit.h"
#include "Misc/ScopeLock.h"
#include "Misc/StringBuilder.h"
#include "ProfilingDebugging/CookStats.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Serialization/CompactBinary.h"
#include "Serialization/CompactBinaryPackage.h"
#include "Serialization/CompactBinaryValidation.h"
#include "Serialization/CompactBinaryWriter.h"
#include "Templates/Greater.h"

#define MAX_BACKEND_KEY_LENGTH (120)
#define MAX_BACKEND_NUMBERED_SUBFOLDER_LENGTH (9)
#if PLATFORM_LINUX	// PATH_MAX on Linux is 4096 (getconf PATH_MAX /, also see limits.h), so this value can be larger (note that it is still arbitrary).
                    // This should not affect sharing the cache between platforms as the absolute paths will be different anyway.
	#define MAX_CACHE_DIR_LEN (3119)
#else
	#define MAX_CACHE_DIR_LEN (119)
#endif // PLATFORM_LINUX
#define MAX_CACHE_EXTENTION_LEN (4)

namespace UE::DerivedData::CacheStore::FileSystem
{

TRACE_DECLARE_INT_COUNTER(FileSystemDDC_Exist, TEXT("FileSystemDDC Exist"));
TRACE_DECLARE_INT_COUNTER(FileSystemDDC_ExistHit, TEXT("FileSystemDDC Exist Hit"));
TRACE_DECLARE_INT_COUNTER(FileSystemDDC_Get, TEXT("FileSystemDDC Get"));
TRACE_DECLARE_INT_COUNTER(FileSystemDDC_GetHit, TEXT("FileSystemDDC Get Hit"));
TRACE_DECLARE_INT_COUNTER(FileSystemDDC_Put, TEXT("FileSystemDDC Put"));
TRACE_DECLARE_INT_COUNTER(FileSystemDDC_PutHit, TEXT("FileSystemDDC Put Hit"));
TRACE_DECLARE_INT_COUNTER(FileSystemDDC_BytesRead, TEXT("FileSystemDDC Bytes Read"));
TRACE_DECLARE_INT_COUNTER(FileSystemDDC_BytesWritten, TEXT("FileSystemDDC Bytes Written"));

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static const TCHAR GBucketsDirectoryName[] = TEXT("Buckets");
static const TCHAR GContentDirectoryName[] = TEXT("Content");

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FString BuildPathForCacheKey(const TCHAR* CacheKey)
{
	FString Key = FString(CacheKey).ToUpper();
	checkf(Algo::AllOf(Key, [](TCHAR C) { return FChar::IsAlnum(C) || FChar::IsUnderscore(C) || C == TEXT('$'); }),
		TEXT("Invalid characters in cache key %s"), CacheKey);
	uint32 Hash = FCrc::StrCrc_DEPRECATED(*Key);
	// this creates a tree of 1000 directories
	FString HashPath = FString::Printf(TEXT("%1d/%1d/%1d/"), (Hash / 100) % 10, (Hash / 10) % 10, Hash % 10);
	return HashPath / Key + TEXT(".udd");
}

void BuildPathForCacheRecord(const FCacheKey& CacheKey, FStringBuilderBase& Path)
{
	const FIoHash::ByteArray& Bytes = CacheKey.Hash.GetBytes();
	Path.Appendf(TEXT("%s/%hs/%02x/%02x/"), GBucketsDirectoryName, CacheKey.Bucket.ToCString(), Bytes[0], Bytes[1]);
	UE::String::BytesToHexLower(MakeArrayView(Bytes).RightChop(2), Path);
	Path << TEXT(".udd"_SV);
}

void BuildPathForCacheContent(const FIoHash& RawHash, FStringBuilderBase& Path)
{
	const FIoHash::ByteArray& Bytes = RawHash.GetBytes();
	Path.Appendf(TEXT("%s/%02x/%02x/"), GContentDirectoryName, Bytes[0], Bytes[1]);
	UE::String::BytesToHexLower(MakeArrayView(Bytes).RightChop(2), Path);
	Path << TEXT(".udd"_SV);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint64 RandFromGuid()
{
	const FGuid Guid = FGuid::NewGuid();
	return FXxHash64::HashBuffer(&Guid, sizeof(FGuid)).Hash;
}

/** A LCG in which the modulus is a power of two where the exponent is the bit width of T. */
template <typename T, T Modulus = 0>
class TLinearCongruentialGenerator
{
	static_assert(!TIsSigned<T>::Value);
	static_assert((Modulus & (Modulus - 1)) == 0, "Modulus must be a power of two.");

public:
	constexpr inline TLinearCongruentialGenerator(T InMultiplier, T InIncrement)
		: Multiplier(InMultiplier)
		, Increment(InIncrement)
	{
	}

	constexpr inline T GetNext(T& Value)
	{
		Value = (Value * Multiplier + Increment) & (Modulus - 1);
		return Value;
	}

private:
	const T Multiplier;
	const T Increment;
};

class FRandomStream
{
public:
	inline explicit FRandomStream(uint32 Seed)
		: Random(1103515245, 12345) // From ANSI C
		, Value(Seed)
	{
	}

	/** Returns a random value in [Min, Max). */
	inline uint32 GetRandRange(uint32 Min, uint32 Max)
	{
		return Min + uint32((uint64(Max - Min) * Random.GetNext(Value)) >> 32);
	}

private:
	TLinearCongruentialGenerator<uint32> Random;
	uint32 Value;
};

template <uint32 Modulus, uint32 Count = Modulus>
class TRandomOrder
{
	static_assert((Modulus & (Modulus - 1)) == 0 && Modulus > 16, "Modulus must be a power of two greater than 16.");
	static_assert(Count > 0 && Count <= Modulus, "Count must be in the range (0, Modulus].");

public:
	inline explicit TRandomOrder(FRandomStream& Stream)
		: Random(Stream.GetRandRange(0, Modulus / 16) * 8 + 5, 12345)
		, First(Stream.GetRandRange(0, Count))
		, Value(First)
	{
	}

	inline uint32 GetFirst() const
	{
		return First;
	}

	inline uint32 GetNext()
	{
		if constexpr (Count < Modulus)
		{
			for (;;)
			{
				if (const uint32 Next = Random.GetNext(Value); Next < Count)
				{
					return Next;
				}
			}
		}
		return Random.GetNext(Value);
	}

private:
	TLinearCongruentialGenerator<uint32, Modulus> Random;
	uint32 First;
	uint32 Value;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct FFileSystemCacheStoreMaintainerParams
{
	/** Files older than this will be deleted. */
	FTimespan MaxFileAge = FTimespan::FromDays(15.0);
	/** Limits the number of files scanned in one second. */
	uint32 MaxFileScanRate = MAX_uint32;
	/** Limits the number of directories scanned in each cache bucket or content root. */
	uint32 MaxDirectoryScanCount = MAX_uint32;
	/** Minimum duration between the start of consecutive scans. */
	FTimespan ScanFrequency = FTimespan::FromHours(1.0);
	/** Time to wait after initialization before maintenance begins. */
	FTimespan TimeToWaitAfterInit = FTimespan::FromMinutes(1.0);
};

class FFileSystemCacheStoreMaintainer final : public ICacheStoreMaintainer
{
public:
	FFileSystemCacheStoreMaintainer(const FFileSystemCacheStoreMaintainerParams& Params, FStringView CachePath);
	~FFileSystemCacheStoreMaintainer();

	bool IsIdle() const final { return bIdle; }
	void WaitForIdle() const { IdleEvent.Wait(); }
	void BoostPriority() final;

private:
	void Loop();
	void Scan();

	void CreateContentRoot();
	void CreateBucketRoots();
	void ScanHashRoot(uint32 RootIndex);
	TStaticBitArray<256> ScanHashDirectory(FStringBuilderBase& Path);

	TStaticBitArray<10> ScanLegacyDirectory(FStringBuilderBase& Path);
	void CreateLegacyRoot();
	void ScanLegacyRoot();

	void ResetRoots();

	void ProcessFile(const TCHAR* Path, const FFileStatData& StatData);

private:
	struct FRoot;
	struct FLegacyRoot;

	FFileSystemCacheStoreMaintainerParams Params;
	/** Path to the root of the cache store. */
	FString CachePath;
	/** True when maintenance is expected to exit. */
	bool bExit = false;
	/** True when there is no active maintenance scan. */
	bool bIdle = false;
	/** Ignore the file scan rate for one maintenance scan. */
	bool bIgnoreFileScanRate = false;

	uint32 ProcessCount = 0;
	uint32 DeleteCount = 0;
	uint64 DeleteSize = 0;

	double BatchStartTime = 0.0;

	IFileManager& FileManager = IFileManager::Get();

	mutable FLazyEvent IdleEvent;
	FEventRef WaitEvent;
	FThread Thread;

	TArray<TUniquePtr<FRoot>> Roots;
	TUniquePtr<FLegacyRoot> LegacyRoot;
	FRandomStream Random{uint32(RandFromGuid())};
};

struct FFileSystemCacheStoreMaintainer::FRoot
{
	inline FRoot(FStringView RootPath, FRandomStream& Stream)
		: Order(Stream)
	{
		Path.Append(RootPath);
	}

	TStringBuilder<256> Path;
	TRandomOrder<256 * 256> Order;
	TStaticBitArray<256> ScannedLevel0;
	TStaticBitArray<256> ExistsLevel0;
	TStaticBitArray<256> ExistsLevel1[256];
	uint32 DirectoryScanCount = 0;
	bool bScannedRoot = false;
};

struct FFileSystemCacheStoreMaintainer::FLegacyRoot
{
	inline explicit FLegacyRoot(FRandomStream& Stream)
		: Order(Stream)
	{
	}

	TRandomOrder<1024, 1000> Order;
	TStaticBitArray<10> ScannedLevel0;
	TStaticBitArray<10> ScannedLevel1[10];
	TStaticBitArray<10> ExistsLevel0;
	TStaticBitArray<10> ExistsLevel1[10];
	TStaticBitArray<10> ExistsLevel2[10][10];
	uint32 DirectoryScanCount = 0;
};

FFileSystemCacheStoreMaintainer::FFileSystemCacheStoreMaintainer(
	const FFileSystemCacheStoreMaintainerParams& InParams,
	FStringView InCachePath)
	: Params(InParams)
	, CachePath(InCachePath)
	, IdleEvent(EEventMode::ManualReset)
	, WaitEvent(EEventMode::AutoReset)
	, Thread(
		TEXT("FileSystemCacheStoreMaintainer"),
		[this] { Loop(); },
		/*StackSize*/ 32 * 1024,
		TPri_BelowNormal)
{
	IModularFeatures::Get().RegisterModularFeature(FeatureName, this);
}

FFileSystemCacheStoreMaintainer::~FFileSystemCacheStoreMaintainer()
{
	bExit = true;
	IModularFeatures::Get().UnregisterModularFeature(FeatureName, this);
	WaitEvent->Trigger();
	Thread.Join();
}

void FFileSystemCacheStoreMaintainer::BoostPriority()
{
	bIgnoreFileScanRate = true;
	WaitEvent->Trigger();
}

void FFileSystemCacheStoreMaintainer::Loop()
{
	WaitEvent->Wait(Params.TimeToWaitAfterInit, /*bIgnoreThreadIdleStats*/ true);

	while (!bExit)
	{
		const FDateTime ScanStart = FDateTime::Now();
		DeleteCount = 0;
		DeleteSize = 0;
		IdleEvent.Reset();
		bIdle = false;
		Scan();
		bIdle = true;
		IdleEvent.Trigger();
		bIgnoreFileScanRate = false;
		const FDateTime ScanEnd = FDateTime::Now();

		UE_LOG(LogDerivedDataCache, Log,
			TEXT("%s: Maintenance finished in %s and deleted %u file(s) with total size %" UINT64_FMT " MiB."),
			*CachePath, *(ScanEnd - ScanStart).ToString(), DeleteCount, DeleteSize / 1024 / 1024);

		if (bExit || Params.ScanFrequency.GetTotalDays() > 365.0)
		{
			break;
		}

		const FDateTime ScanTime = ScanStart + Params.ScanFrequency;
		UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Maintenance is paused until the next scan at %s."),
			*CachePath, *ScanTime.ToString());
		for (FDateTime Now = ScanEnd; !bExit && Now < ScanTime; Now = FDateTime::Now())
		{
			WaitEvent->Wait(ScanTime - Now, /*bIgnoreThreadIdleStats*/ true);
		}
	}
}

void FFileSystemCacheStoreMaintainer::Scan()
{
	CreateContentRoot();
	CreateBucketRoots();
	CreateLegacyRoot();

	while (!bExit)
	{
		const uint32 RootCount = uint32(Roots.Num());
		const uint32 TotalRootCount = uint32(RootCount + LegacyRoot.IsValid());
		if (TotalRootCount == 0)
		{
			break;
		}
		if (const uint32 RootIndex = Random.GetRandRange(0, TotalRootCount); RootIndex < RootCount)
		{
			ScanHashRoot(RootIndex);
		}
		else
		{
			ScanLegacyRoot();
		}
	}

	ResetRoots();
}

void FFileSystemCacheStoreMaintainer::CreateContentRoot()
{
	TStringBuilder<256> ContentPath;
	FPathViews::Append(ContentPath, CachePath, GContentDirectoryName);
	if (FileManager.DirectoryExists(*ContentPath))
	{
		Roots.Add(MakeUnique<FRoot>(ContentPath, Random));
	}
}

void FFileSystemCacheStoreMaintainer::CreateBucketRoots()
{
	TStringBuilder<256> BucketsPath;
	FPathViews::Append(BucketsPath, CachePath, GBucketsDirectoryName);
	FileManager.IterateDirectoryStat(*BucketsPath, [this](const TCHAR* Path, const FFileStatData& StatData) -> bool
	{
		if (StatData.bIsDirectory)
		{
			Roots.Add(MakeUnique<FRoot>(Path, Random));
		}
		return !bExit;
	});
}

void FFileSystemCacheStoreMaintainer::ScanHashRoot(uint32 RootIndex)
{
	FRoot& Root = *Roots[int32(RootIndex)];
	const uint32 DirectoryIndex = Root.Order.GetNext();
	const uint32 IndexLevel0 = DirectoryIndex / 256;
	const uint32 IndexLevel1 = DirectoryIndex % 256;

	bool bScanned = false;
	ON_SCOPE_EXIT
	{
		if ((DirectoryIndex == Root.Order.GetFirst()) ||
			(bScanned && ++Root.DirectoryScanCount >= Params.MaxDirectoryScanCount))
		{
			UE_LOG(LogDerivedDataCache, VeryVerbose, TEXT("%s: Maintenance finished scanning %s."), *CachePath, *Root.Path);
			Roots.RemoveAt(int32(RootIndex));
		}
	};

	if (!Root.bScannedRoot)
	{
		Root.ExistsLevel0 = ScanHashDirectory(Root.Path);
		Root.bScannedRoot = true;
	}

	if (!Root.ExistsLevel0[IndexLevel0])
	{
		return;
	}
	if (!Root.ScannedLevel0[IndexLevel0])
	{
		TStringBuilder<256> Path;
		Path.Appendf(TEXT("%s/%02x"), *Root.Path, IndexLevel0);
		Root.ExistsLevel1[IndexLevel0] = ScanHashDirectory(Path);
		Root.ScannedLevel0[IndexLevel0] = true;
	}

	if (!Root.ExistsLevel1[IndexLevel0][IndexLevel1])
	{
		return;
	}

	TStringBuilder<256> Path;
	Path.Appendf(TEXT("%s/%02x/%02x"), *Root.Path, IndexLevel0, IndexLevel1);
	FileManager.IterateDirectoryStat(*Path, [this](const TCHAR* const Path, const FFileStatData& StatData) -> bool
	{
		ProcessFile(Path, StatData);
		return !bExit;
	});

	bScanned = true;
}

TStaticBitArray<256> FFileSystemCacheStoreMaintainer::ScanHashDirectory(FStringBuilderBase& Path)
{
	TStaticBitArray<256> Exists;
	FileManager.IterateDirectoryStat(*Path, [this, &Exists](const TCHAR* ChildPath, const FFileStatData& StatData) -> bool
	{
		FStringView ChildView = FPathViews::GetCleanFilename(ChildPath);
		if (StatData.bIsDirectory && ChildView.Len() == 2 && Algo::AllOf(ChildView, FChar::IsHexDigit))
		{
			uint8 Byte;
			if (String::HexToBytes(ChildView, &Byte) == 1)
			{
				Exists[Byte] = true;
			}
		}
		return !bExit;
	});
	return Exists;
}

TStaticBitArray<10> FFileSystemCacheStoreMaintainer::ScanLegacyDirectory(FStringBuilderBase& Path)
{
	TStaticBitArray<10> Exists;
	const bool bDeleteFiles = CachePath.Len() < Path.Len();
	FileManager.IterateDirectoryStat(*Path, [this, &Exists, bDeleteFiles](const TCHAR* ChildPath, const FFileStatData& StatData) -> bool
	{
		FStringView ChildView = FPathViews::GetCleanFilename(ChildPath);
		if (StatData.bIsDirectory && ChildView.Len() == 1 && Algo::AllOf(ChildView, FChar::IsDigit))
		{
			Exists[FChar::ConvertCharDigitToInt(ChildView[0])] = true;
		}
		return !bExit;
	});
	return Exists;
}

void FFileSystemCacheStoreMaintainer::CreateLegacyRoot()
{
	TStringBuilder<256> Path;
	FPathViews::Append(Path, CachePath);
	TStaticBitArray<10> Exists = ScanLegacyDirectory(Path);
	if (Exists.FindFirstSetBit() != INDEX_NONE)
	{
		LegacyRoot = MakeUnique<FLegacyRoot>(Random);
		LegacyRoot->ExistsLevel0 = Exists;
	}
}

void FFileSystemCacheStoreMaintainer::ScanLegacyRoot()
{
	FLegacyRoot& Root = *LegacyRoot;
	const uint32 DirectoryIndex = Root.Order.GetNext();
	const int32 IndexLevel0 = int32(DirectoryIndex / 100) % 10;
	const int32 IndexLevel1 = int32(DirectoryIndex / 10) % 10;
	const int32 IndexLevel2 = int32(DirectoryIndex / 1) % 10;

	bool bScanned = false;
	ON_SCOPE_EXIT
	{
		if ((DirectoryIndex == Root.Order.GetFirst()) ||
			(bScanned && ++Root.DirectoryScanCount >= Params.MaxDirectoryScanCount))
		{
			LegacyRoot.Reset();
		}
	};

	if (!Root.ExistsLevel0[IndexLevel0])
	{
		return;
	}
	if (!Root.ScannedLevel0[IndexLevel0])
	{
		TStringBuilder<256> Path;
		FPathViews::Append(Path, CachePath, IndexLevel0);
		Root.ExistsLevel1[IndexLevel0] = ScanLegacyDirectory(Path);
		Root.ScannedLevel0[IndexLevel0] = true;
	}

	if (!Root.ExistsLevel1[IndexLevel0][IndexLevel1])
	{
		return;
	}
	if (!Root.ScannedLevel1[IndexLevel0][IndexLevel1])
	{
		TStringBuilder<256> Path;
		FPathViews::Append(Path, CachePath, IndexLevel0, IndexLevel1);
		Root.ExistsLevel2[IndexLevel0][IndexLevel1] = ScanLegacyDirectory(Path);
		Root.ScannedLevel1[IndexLevel0][IndexLevel1] = true;
	}

	if (!Root.ExistsLevel2[IndexLevel0][IndexLevel1][IndexLevel2])
	{
		return;
	}

	TStringBuilder<256> Path;
	FPathViews::Append(Path, CachePath, IndexLevel0, IndexLevel1, IndexLevel2);
	FileManager.IterateDirectoryStat(*Path, [this](const TCHAR* const Path, const FFileStatData& StatData) -> bool
	{
		ProcessFile(Path, StatData);
		return !bExit;
	});

	bScanned = true;
}

void FFileSystemCacheStoreMaintainer::ResetRoots()
{
	Roots.Empty();
	LegacyRoot.Reset();
}

void FFileSystemCacheStoreMaintainer::ProcessFile(const TCHAR* const Path, const FFileStatData& StatData)
{
	if (StatData.bIsDirectory)
	{
		return;
	}

	if (StatData.ModificationTime + Params.MaxFileAge < FDateTime::UtcNow())
	{
		++DeleteCount;
		DeleteSize += StatData.FileSize > 0 ? uint64(StatData.FileSize) : 0;
		if (FileManager.Delete(Path, /*bRequireExists*/ false, /*bEvenReadOnly*/ false, /*bQuiet*/ true))
		{
			UE_LOG(LogDerivedDataCache, VeryVerbose,
				TEXT("%s: Maintenance deleted file %s that was last modified at %s."),
				*CachePath, Path, *StatData.ModificationTime.ToIso8601());
		}
		else
		{
			UE_LOG(LogDerivedDataCache, Verbose,
				TEXT("%s: Maintenance failed to delete file %s that was last modified at %s."),
				*CachePath, Path, *StatData.ModificationTime.ToIso8601());
		}
	}

	if (!bExit && !bIgnoreFileScanRate && Params.MaxFileScanRate && ++ProcessCount % Params.MaxFileScanRate == 0)
	{
		const double BatchEndTime = FPlatformTime::Seconds();
		if (const double BatchWaitTime = 1.0 - (BatchEndTime - BatchStartTime); BatchWaitTime > 0.0)
		{
			WaitEvent->Wait(FTimespan::FromSeconds(BatchWaitTime), /*bIgnoreThreadIdleStats*/ true);
			BatchStartTime = FPlatformTime::Seconds();
		}
		else
		{
			BatchStartTime = BatchEndTime;
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** 
 * Cache server that uses the OS filesystem
 * The entire API should be callable from any thread (except the singleton can be assumed to be called at least once before concurrent access).
**/
class FFileSystemDerivedDataBackend : public FDerivedDataBackendInterface
{
public:
	FFileSystemDerivedDataBackend(const TCHAR* InCachePath, const TCHAR* InParams, const TCHAR* InAccessLogFileName)
		: CachePath(InCachePath)
		, SpeedClass(ESpeedClass::Unknown)
		, bDisabled(false)
		, bReadOnly(false)
		, bTouch(false)
		, bPurgeTransient(false)
		, DaysToDeleteUnusedFiles(15.0)
		, TotalEstimatedBuildTime(0)
	{
		// If we find a platform that has more stringent limits, this needs to be rethought.
		checkf(MAX_BACKEND_KEY_LENGTH + MAX_CACHE_DIR_LEN + MAX_BACKEND_NUMBERED_SUBFOLDER_LENGTH + MAX_CACHE_EXTENTION_LEN < FPlatformMisc::GetMaxPathLength(),
			TEXT("Not enough room left for cache keys in max path."));

		check(CachePath.Len());
		FPaths::NormalizeFilename(CachePath);

		// Params that override our instance defaults
		FParse::Bool(InParams, TEXT("ReadOnly="), bReadOnly);
		FParse::Bool(InParams, TEXT("Touch="), bTouch);
		FParse::Bool(InParams, TEXT("PurgeTransient="), bPurgeTransient);
		FParse::Value(InParams, TEXT("UnusedFileAge="), DaysToDeleteUnusedFiles);
		FParse::Value(InParams, TEXT("MaxRecordSizeKB="), MaxRecordSizeKB);
		FParse::Value(InParams, TEXT("MaxValueSizeKB="), MaxValueSizeKB);

		// Flush the cache if requested.
		bool bFlush = false;
		if (!bReadOnly && FParse::Bool(InParams, TEXT("Flush="), bFlush) && bFlush)
		{
			IFileManager::Get().DeleteDirectory(*(CachePath / TEXT("")), /*bRequireExists*/ false, /*bTree*/ true);
		}

		// check latency and speed. Read values should always be valid
		double ReadSpeedMBs = 0.0;
		double WriteSpeedMBs = 0.0;
		double SeekTimeMS = 0.0;

		/* Speeds faster than this are considered local*/
		const float ConsiderFastAtMS = 10;
		/* Speeds faster than this are ok. Everything else is slow. This value can be overridden in the ini file */
		float ConsiderSlowAtMS = 50;
		FParse::Value(InParams, TEXT("ConsiderSlowAt="), ConsiderSlowAtMS);

		// can skip the speed test so everything acts as local (e.g. 4.25 and earlier behavior). 
		bool SkipSpeedTest = !WITH_EDITOR || FParse::Param(FCommandLine::Get(), TEXT("ddcskipspeedtest"));
		if (SkipSpeedTest)
		{
			ReadSpeedMBs = 999;
			WriteSpeedMBs = 999;
			SeekTimeMS = 0;
			UE_LOG(LogDerivedDataCache, Log, TEXT("Skipping speed test to %s. Assuming local performance"), *CachePath);
		}

		if (!SkipSpeedTest && !RunSpeedTest(ConsiderSlowAtMS * 2, SeekTimeMS, ReadSpeedMBs, WriteSpeedMBs))
		{
			bDisabled = true;
			UE_LOG(LogDerivedDataCache, Warning, TEXT("No read or write access to %s"), *CachePath);
		}
		else
		{
			bool bReadTestPassed = ReadSpeedMBs > 0.0;
			bool bWriteTestPassed = WriteSpeedMBs > 0.0;

			// if we failed writes mark this as read only
			bReadOnly = bReadOnly || !bWriteTestPassed;

			// classify and report on these times
			if (SeekTimeMS < 1)
			{
				SpeedClass = ESpeedClass::Local;
			}
			else if (SeekTimeMS <= ConsiderFastAtMS)
			{
				SpeedClass = ESpeedClass::Fast;
			}
			else if (SeekTimeMS >= ConsiderSlowAtMS)
			{
				SpeedClass = ESpeedClass::Slow;
			}
			else
			{
				SpeedClass = ESpeedClass::Ok;
			}

			UE_LOG(LogDerivedDataCache, Display, TEXT("Performance to %s: Latency=%.02fms. RandomReadSpeed=%.02fMBs, RandomWriteSpeed=%.02fMBs. Assigned SpeedClass '%s'"), 
				*CachePath, SeekTimeMS, ReadSpeedMBs, WriteSpeedMBs, LexToString(SpeedClass));

			if (SpeedClass <= FDerivedDataBackendInterface::ESpeedClass::Slow && !bReadOnly)
			{
				if (GIsBuildMachine)
				{
					UE_LOG(LogDerivedDataCache, Display, TEXT("Access to %s appears to be slow. 'Touch' will be disabled and queries/writes will be limited."), *CachePath);
				}
				else
				{
					UE_LOG(LogDerivedDataCache, Warning, TEXT("Access to %s appears to be slow. 'Touch' will be disabled and queries/writes will be limited."), *CachePath);
				}
				bTouch = false;
				//bReadOnly = true;
			}

			if (!bReadOnly)
			{
				if (FString(FCommandLine::Get()).Contains(TEXT("Run=DerivedDataCache")))
				{
					bTouch = true; // we always touch files when running the DDC commandlet
				}

				// The command line (-ddctouch) enables touch on all filesystem backends if specified. 
				bTouch = bTouch || FParse::Param(FCommandLine::Get(), TEXT("DDCTOUCH"));

				if (bTouch)
				{
					UE_LOG(LogDerivedDataCache, Display, TEXT("Files in %s will be touched."), *CachePath);
				}

				bool bClean = false;
				bool bDeleteUnused = true;
				FParse::Bool(InParams, TEXT("Clean="), bClean);
				FParse::Bool(InParams, TEXT("DeleteUnused="), bDeleteUnused);
				bDeleteUnused = bDeleteUnused && !FParse::Param(FCommandLine::Get(), TEXT("NODDCCLEANUP"));

				if (bClean || bDeleteUnused)
				{
					FFileSystemCacheStoreMaintainerParams MaintainerParams;
					MaintainerParams.MaxFileAge = FTimespan::FromDays(DaysToDeleteUnusedFiles);
					if (bDeleteUnused)
					{
						if (!FParse::Value(InParams, TEXT("MaxFileChecksPerSec="), MaintainerParams.MaxFileScanRate))
						{
							int32 MaxFileScanRate;
							if (GConfig->GetInt(TEXT("DDCCleanup"), TEXT("MaxFileChecksPerSec"), MaxFileScanRate, GEngineIni))
							{
								MaintainerParams.MaxFileScanRate = uint32(MaxFileScanRate);
							}
						}
						FParse::Value(InParams, TEXT("FoldersToClean="), MaintainerParams.MaxDirectoryScanCount);
					}
					else
					{
						MaintainerParams.ScanFrequency = FTimespan::FromDays(500.0);
					}
					double TimeToWaitAfterInit;
					if (bClean)
					{
						MaintainerParams.TimeToWaitAfterInit = FTimespan::Zero();
					}
					else if (GConfig->GetDouble(TEXT("DDCCleanup"), TEXT("TimeToWaitAfterInit"), TimeToWaitAfterInit, GEngineIni))
					{
						MaintainerParams.TimeToWaitAfterInit = FTimespan::FromSeconds(TimeToWaitAfterInit);
					}

					Maintainer = MakeUnique<FFileSystemCacheStoreMaintainer>(MaintainerParams, CachePath);

					if (bClean)
					{
						Maintainer->BoostPriority();
						Maintainer->WaitForIdle();
					}
				}
			}
			
			if (IsUsable() && InAccessLogFileName != nullptr && *InAccessLogFileName != 0)
			{
				AccessLogWriter.Reset(new FAccessLogWriter(InAccessLogFileName, CachePath));
			}
		}
	}

	bool RunSpeedTest(double InSkipTestsIfSeeksExceedMS, double& OutSeekTimeMS, double& OutReadSpeedMBs, double& OutWriteSpeedMBs) const
	{
		SCOPED_BOOT_TIMING("RunSpeedTest");

		//  files of increasing size. Most DDC data falls within this range so we don't want to skew by reading 
		// large amounts of data. Ultimately we care most about latency anyway.
		const int FileSizes[] = { 4, 8, 16, 64, 128, 256 };
		const int NumTestFolders = 2; //(0-9)
		const int FileSizeCount = UE_ARRAY_COUNT(FileSizes);

		bool bWriteTestPassed = true;
		bool bReadTestPassed = true;
		bool bTestDataExists = true;

		double TotalSeekTime = 0;
		double TotalReadTime = 0;
		double TotalWriteTime = 0;
		int TotalDataRead = 0;
		int TotalDataWritten = 0;

		const FString AbsoluteCachePath = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*CachePath);
		if (AbsoluteCachePath.Len() > MAX_CACHE_DIR_LEN)
		{
			const FText ErrorMessage = FText::Format(NSLOCTEXT("DerivedDataCache", "PathTooLong", "Cache path {0} is longer than {1} characters...please adjust [DerivedDataBackendGraph] paths to be shorter (this leaves more room for cache keys)."), FText::FromString(AbsoluteCachePath), FText::AsNumber(MAX_CACHE_DIR_LEN));
			FMessageDialog::Open(EAppMsgType::Ok, ErrorMessage);
			UE_LOG(LogDerivedDataCache, Fatal, TEXT("%s"), *ErrorMessage.ToString());
		}

		TArray<FString> Paths;
		TArray<FString> MissingFiles;

		MissingFiles.Reserve(NumTestFolders * FileSizeCount);

		const FString TestDataPath = FPaths::Combine(CachePath, TEXT("TestData"));

		// create an upfront map of paths to data size in bytes
		// create the paths we'll use. <path>/0/TestData.dat, <path>/1/TestData.dat etc. If those files don't exist we'll
		// create them which will likely give an invalid result when measuring them now but not in the future...
		TMap<FString, int> TestFileEntries;
		for (int iSize = 0; iSize < FileSizeCount; iSize++)
		{
			// make sure we dont stat/read/write to consecuting files in folders
			for (int iFolder = 0; iFolder < NumTestFolders; iFolder++)
			{
				int FileSizeKB = FileSizes[iSize];
				FString Path = FPaths::Combine(CachePath, TEXT("TestData"), *FString::FromInt(iFolder), *FString::Printf(TEXT("TestData_%dkb.dat"), FileSizeKB));
				TestFileEntries.Add(Path, FileSizeKB * 1024);
			}
		}

		// measure latency by checking for the presence of all these files. We'll also track which don't exist..
		const double StatStartTime = FPlatformTime::Seconds();
 		for (auto& KV : TestFileEntries)
		{
			FFileStatData StatData = IFileManager::Get().GetStatData(*KV.Key);

			if (!StatData.bIsValid || StatData.FileSize != KV.Value)
			{
				MissingFiles.Add(KV.Key);
			}
		}

		// save total stat time
		TotalSeekTime = (FPlatformTime::Seconds() - StatStartTime);

		// calculate seek time here
		OutSeekTimeMS = (TotalSeekTime / TestFileEntries.Num()) * 1000;

		UE_LOG(LogDerivedDataCache, Verbose, TEXT("Stat tests to %s took %.02f seconds"), *CachePath, TotalSeekTime);

		// if seek times are very slow do a single read/write test just to confirm access
		/*if (OutSeekTimeMS >= InSkipTestsIfSeeksExceedMS)
		{
			UE_LOG(LogDerivedDataCache, Warning, TEXT("Limiting read/write speed tests due to seek times of %.02f exceeding %.02fms. Values will be inaccurate."), OutSeekTimeMS, InSkipTestsIfSeeksExceedMS);

			FString Path = TestFileEntries.begin()->Key;
			int Size = TestFileEntries.begin()->Value;

			TestFileEntries.Reset();
			TestFileEntries.Add(Path, Size);		
		}*/
	
		// create any files that were missing
		if (!bReadOnly)
		{
			TArray<uint8> Data;
			for (auto& File : MissingFiles)
			{
				const int DesiredSize = TestFileEntries[File];
				Data.SetNumUninitialized(DesiredSize);

				if (!FFileHelper::SaveArrayToFile(Data, *File, &IFileManager::Get(), FILEWRITE_Silent))
				{
					// handle the case where something else may have created the path at the same time. This is less about multiple users
					// and more about things like SCW's / UnrealPak that can spin up multiple instances at once
					if (!IFileManager::Get().FileExists(*File))
					{
						uint32 ErrorCode = FPlatformMisc::GetLastError();
						TCHAR ErrorBuffer[1024];
						FPlatformMisc::GetSystemErrorMessage(ErrorBuffer, 1024, ErrorCode);
						UE_LOG(LogDerivedDataCache, Warning, TEXT("Fail to create %s, derived data cache to this directory will be read only. WriteError: %u (%s)"), *File, ErrorCode, ErrorBuffer);
						bTestDataExists = false;
						bWriteTestPassed = false;
						break;
					}
				}
			}
		}

		// now read all sizes from random folders
		{
			const int ArraySize = UE_ARRAY_COUNT(FileSizes);
			TArray<uint8> TempData;
			TempData.Empty(FileSizes[ArraySize - 1] * 1024);

			const double ReadStartTime = FPlatformTime::Seconds();

			for (auto& KV : TestFileEntries)
			{
				const int FileSize = KV.Value;
				const FString& FilePath = KV.Key;

				if (!FFileHelper::LoadFileToArray(TempData, *FilePath, FILEREAD_Silent))
				{
					uint32 ErrorCode = FPlatformMisc::GetLastError();
					TCHAR ErrorBuffer[1024];
					FPlatformMisc::GetSystemErrorMessage(ErrorBuffer, 1024, ErrorCode);
					UE_LOG(LogDerivedDataCache, Warning, TEXT("Fail to read from %s, derived data cache will be disabled. ReadError: %u (%s)"), *FilePath, ErrorCode, ErrorBuffer);
					bReadTestPassed = false;
					break;
				}

				TotalDataRead += TempData.Num();
			}

			TotalReadTime = FPlatformTime::Seconds() - ReadStartTime;

			UE_LOG(LogDerivedDataCache, Verbose, TEXT("Read tests %s on %s and took %.02f seconds"), bReadTestPassed ? TEXT("passed") : TEXT("failed"), *CachePath, TotalReadTime);
		}
		
		// do write tests if or read tests passed and our seeks were below the cut-off 
		if (bReadTestPassed && !bReadOnly)
		{
			// do write tests but use a unique folder that is cleaned up afterwards
			FString CustomPath = FPaths::Combine(CachePath, TEXT("TestData"), *FGuid::NewGuid().ToString());

			const int ArraySize = UE_ARRAY_COUNT(FileSizes);
			TArray<uint8> TempData;
			TempData.Empty(FileSizes[ArraySize - 1] * 1024);

			const double WriteStartTime = FPlatformTime::Seconds();

			for (auto& KV : TestFileEntries)
			{
				const int FileSize = KV.Value;
				FString FilePath = KV.Key;

				TempData.SetNumUninitialized(FileSize);

				FilePath = FilePath.Replace(*CachePath, *CustomPath);

				if (!FFileHelper::SaveArrayToFile(TempData, *FilePath, &IFileManager::Get(), FILEWRITE_Silent))
				{
					uint32 ErrorCode = FPlatformMisc::GetLastError();
					TCHAR ErrorBuffer[1024];
					FPlatformMisc::GetSystemErrorMessage(ErrorBuffer, 1024, ErrorCode);
					UE_LOG(LogDerivedDataCache, Warning, TEXT("Fail to write to %s, derived data cache will be disabled. ReadError: %u (%s)"), *FilePath, ErrorCode, ErrorBuffer);
					bWriteTestPassed = false;
					break;
				}

				TotalDataWritten += TempData.Num();
			}

			TotalWriteTime = FPlatformTime::Seconds() - WriteStartTime;

			UE_LOG(LogDerivedDataCache, Verbose, TEXT("write tests %s on %s and took %.02f seconds"), bWriteTestPassed ? TEXT("passed") : TEXT("failed"), *CachePath, TotalReadTime)

			// remove the custom path but do it async as this can be slow on remote drives
			AsyncTask(ENamedThreads::AnyThread, [CustomPath]() {
				IFileManager::Get().DeleteDirectory(*CustomPath, false, true);
			});
	
			// check latency and speed. Read values should always be valid
			const double ReadSpeedMBs = (bReadTestPassed ? (TotalDataRead / TotalReadTime) : 0) / (1024 * 1024);
			const double WriteSpeedMBs = (bWriteTestPassed ? (TotalDataWritten / TotalWriteTime) : 0) / (1024 * 1024);
			const double SeekTimeMS = (TotalSeekTime / TestFileEntries.Num()) * 1000;
		}

		const double TotalTestTime = FPlatformTime::Seconds() - StatStartTime;

		UE_LOG(LogDerivedDataCache, Log, TEXT("Speed tests for %s took %.02f seconds"), *CachePath, TotalTestTime);

		// check latency and speed. Read values should always be valid
		OutReadSpeedMBs = (bReadTestPassed ? (TotalDataRead / TotalReadTime) : 0) / (1024 * 1024);
		OutWriteSpeedMBs = (bWriteTestPassed ? (TotalDataWritten / TotalWriteTime) : 0) / (1024 * 1024);

		return bWriteTestPassed || bReadTestPassed;
	}

	/** Return a type for this interface */
	virtual FString GetDisplayName() const override
	{
		return FString(TEXT("File System"));
	}


	/** Return a name for this interface */
	virtual FString GetName() const override 
	{ 
		return CachePath; 
	}

	/** return true if the cache is usable **/
	bool IsUsable() const
	{
		return !bDisabled;
	}

	/** return true if this cache is writable **/
	virtual bool IsWritable() const override
	{
		return !bReadOnly && !bDisabled;
	}

	/** Returns a class of speed for this interface **/
	virtual ESpeedClass GetSpeedClass() const override
	{
		return SpeedClass;
	}

	class FAccessLogWriter
	{
	public:
		FAccessLogWriter(const TCHAR* FileName, const FString& CachePath)
			: Archive(IFileManager::Get().CreateFileWriter(FileName, FILEWRITE_AllowRead))
			, BasePath(CachePath / TEXT(""))
		{
		}

		void Append(const TCHAR* CacheKey, FStringView Path)
		{
			FScopeLock Lock(&CriticalSection);

			bool bIsAlreadyInSet = false;
			CacheKeys.FindOrAdd(FString(CacheKey), &bIsAlreadyInSet);
			if (!bIsAlreadyInSet)
			{
				AppendPath(Path);
			}
		}

		void Append(const FIoHash& RawHash, FStringView Path)
		{
			FScopeLock Lock(&CriticalSection);

			bool bIsAlreadyInSet = false;
			ContentKeys.FindOrAdd(RawHash, &bIsAlreadyInSet);
			if (!bIsAlreadyInSet)
			{
				AppendPath(Path);
			}
		}

		void Append(const FCacheKey& CacheKey, FStringView Path)
		{
			FScopeLock Lock(&CriticalSection);

			bool bIsAlreadyInSet = false;
			RecordKeys.FindOrAdd(CacheKey, &bIsAlreadyInSet);
			if (!bIsAlreadyInSet)
			{
				AppendPath(Path);
			}
		}

	private:
		void AppendPath(FStringView Path)
		{
			if (Path.StartsWith(BasePath))
			{
				const FTCHARToUTF8 PathUtf8(Path);
				Archive->Serialize(const_cast<ANSICHAR*>(PathUtf8.Get()), PathUtf8.Length());
				Archive->Serialize(const_cast<ANSICHAR*>(LINE_TERMINATOR_ANSI), sizeof(LINE_TERMINATOR_ANSI) - 1);
			}
		}

		TUniquePtr<FArchive> Archive;
		FString BasePath;
		FCriticalSection CriticalSection;
		TSet<FString> CacheKeys;
		TSet<FIoHash> ContentKeys;
		TSet<FCacheKey> RecordKeys;
	};
	
	
	/**
	 * Synchronous test for the existence of a cache item
	 *
	 * @param	CacheKey	Alphanumeric+underscore key of this cache item
	 * @return				true if the data probably will be found, this can't be guaranteed because of concurrency in the backends, corruption, etc
	 */
	virtual bool CachedDataProbablyExists(const TCHAR* CacheKey) override
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FileSystemDDC_Exist);
		TRACE_COUNTER_INCREMENT(FileSystemDDC_Exist);
		COOK_STAT(auto Timer = UsageStats.TimeProbablyExists());
		check(IsUsable());

		// if we're a slow device just say we have the data. It's faster to try and fail than it
		// is to check and succeed.
		
		// todo(@agrant: Some types currently use ProbablyExists as a guarantee. Disabling this until those can be 
		// addressed.
		/*
		if (GetSpeedClass() <= FDerivedDataBackendInterface::ESpeedClass::Slow)
		{
			return true;
		}
		*/

		if (ShouldSimulateMiss(CacheKey))
		{
			return false;
		}

		FString Filename = BuildFilename(CacheKey);

		FFileStatData FileStat = IFileManager::Get().GetStatData(*Filename);

		if (FileStat.bIsValid)
		{
			FDateTime TimeStamp = FileStat.ModificationTime;

			// Update file timestamp to prevent it from being deleted by DDC Cleanup.
			if (bTouch || 
				 (!bReadOnly && (FDateTime::UtcNow() - TimeStamp).GetTotalDays() > (DaysToDeleteUnusedFiles / 4)))
			{
				IFileManager::Get().SetTimeStamp(*Filename, FDateTime::UtcNow());
			}

			if (AccessLogWriter.IsValid())
			{
				AccessLogWriter->Append(CacheKey, Filename);
			}

			TRACE_COUNTER_INCREMENT(FileSystemDDC_ExistHit);
			COOK_STAT(Timer.AddHit(0));
		}

		// If not using a shared cache, record a (probable) miss
		if (!FileStat.bIsValid && !GetDerivedDataCacheRef().GetUsingSharedDDC())
		{
			// store a cache miss
			FScopeLock ScopeLock(&SynchronizationObject);
			if (!DDCNotificationCacheTimes.Contains(CacheKey))
			{
				DDCNotificationCacheTimes.Add(CacheKey, FPlatformTime::Seconds());
			}
		}

		UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s CachedDataProbablyExists=%d for %s"), *GetName(), FileStat.bIsValid, CacheKey);

		return FileStat.bIsValid;
	}	

	/**
	 * Synchronous retrieve of a cache item
	 *
	 * @param	CacheKey	Alphanumeric+underscore key of this cache item
	 * @param	OutData		Buffer to receive the results, if any were found
	 * @return				true if any data was found, and in this case OutData is non-empty
	 */
	virtual bool GetCachedData(const TCHAR* CacheKey, TArray<uint8>& Data) override
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FileSystemDDC_Get);
		TRACE_COUNTER_INCREMENT(FileSystemDDC_Get);
		COOK_STAT(auto Timer = UsageStats.TimeGet());
		check(IsUsable());
		FString Filename = BuildFilename(CacheKey);
		double StartTime = FPlatformTime::Seconds();

		if (ShouldSimulateMiss(CacheKey))
		{
			return false;
		}

		if (FFileHelper::LoadFileToArray(Data,*Filename,FILEREAD_Silent))
		{
			if (SpeedClass >= FDerivedDataBackendInterface::ESpeedClass::Fast && (!bReadOnly || bTouch))
			{
				FFileStatData FileStat = IFileManager::Get().GetStatData(*Filename);
				if (FileStat.bIsValid)
				{
					FDateTime TimeStamp = FileStat.ModificationTime;
					// Update file timestamp to prevent it from being deleted by DDC Cleanup.
					if (bTouch ||
						(!bReadOnly && (FDateTime::UtcNow() - TimeStamp).GetTotalDays() > (DaysToDeleteUnusedFiles / 4)))
					{
						IFileManager::Get().SetTimeStamp(*Filename, FDateTime::UtcNow());
					}
				}
			}

			double ReadDuration = FPlatformTime::Seconds() - StartTime;
			double ReadSpeed = (Data.Num() / ReadDuration) / (1024.0 * 1024.0);

			if(!GIsBuildMachine && ReadDuration > 5.0)
			{				
				// Slower than 0.5MB/s?
				UE_CLOG(ReadSpeed < 0.5, LogDerivedDataCache, Warning, TEXT("%s is very slow (%.2fMB/s) when accessing %s, consider disabling it."), *CachePath, ReadSpeed, *Filename);
			}

			if (AccessLogWriter.IsValid())
			{
				AccessLogWriter->Append(CacheKey, Filename);
			}

			UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Cache hit on %s (%d bytes, %.02f secs, %.2fMB/s)"), *GetName(), CacheKey, Data.Num(), ReadDuration, ReadSpeed);
			TRACE_COUNTER_INCREMENT(FileSystemDDC_GetHit);
			TRACE_COUNTER_ADD(FileSystemDDC_BytesRead, int64(Data.Num()));
			COOK_STAT(Timer.AddHit(Data.Num()));
			return true;
		}

		UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Cache miss on %s"), *GetName(), CacheKey);
		Data.Empty();

		// If not using a shared cache, record a miss
		if (!GetDerivedDataCacheRef().GetUsingSharedDDC())
		{
			// store a cache miss
			FScopeLock ScopeLock(&SynchronizationObject);
			if (!DDCNotificationCacheTimes.Contains(CacheKey))
			{
				DDCNotificationCacheTimes.Add(CacheKey, FPlatformTime::Seconds());
			}
		}

		return false;
	}

	/**
	 * Would we cache this? Say yes so long as we aren't read-only.
	 */
	bool WouldCache(const TCHAR* CacheKey, TArrayView<const uint8> InData) override
	{
		return IsWritable() && !CachedDataProbablyExists(CacheKey);
	}

	/**
	 * Asynchronous, fire-and-forget placement of a cache item
	 *
	 * @param	CacheKey	Alphanumeric+underscore key of this cache item
	 * @param	OutData		Buffer containing the data to cache, can be destroyed after the call returns, immediately
	 * @param	bPutEvenIfExists	If true, then do not attempt skip the put even if CachedDataProbablyExists returns true
	 */
	virtual EPutStatus PutCachedData(const TCHAR* CacheKey, TArrayView<const uint8> Data, bool bPutEvenIfExists) override
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FileSystemDDC_Put);
		TRACE_COUNTER_INCREMENT(FileSystemDDC_Put);
		COOK_STAT(auto Timer = UsageStats.TimePut());
		check(IsUsable());

		if (IsWritable())
		{
			FString Filename = BuildFilename(CacheKey);

			if (AccessLogWriter.IsValid())
			{
				AccessLogWriter->Append(CacheKey, Filename);
			}

			// don't put anything we pretended didn't exist
			if (ShouldSimulateMiss(CacheKey))
			{
				return EPutStatus::Skipped;
			}

			EPutStatus Status = EPutStatus::NotCached;

			if (bPutEvenIfExists || !CachedDataProbablyExists(CacheKey))
			{
				TRACE_COUNTER_INCREMENT(FileSystemDDC_PutHit);
				TRACE_COUNTER_ADD(FileSystemDDC_BytesWritten, int64(Data.Num()));
				COOK_STAT(Timer.AddHit(Data.Num()));
				check(Data.Num());
				FString TempFilename(TEXT("temp.")); 
				TempFilename += FGuid::NewGuid().ToString();
				TempFilename = FPaths::GetPath(Filename) / TempFilename;
				bool bResult;
				{
					bResult = FFileHelper::SaveArrayToFile(Data, *TempFilename, &IFileManager::Get(), FILEWRITE_Silent);
				}
				if (bResult)
				{
					if (IFileManager::Get().FileSize(*TempFilename) == Data.Num())
					{
						bool DoMove = !CachedDataProbablyExists(CacheKey);
						if (bPutEvenIfExists && !DoMove)
						{
							DoMove = true;
							RemoveCachedData(CacheKey, /*bTransient=*/ false);
						}
						if (DoMove) 
						{
							if (!IFileManager::Get().Move(*Filename, *TempFilename, true, true, false, true))
							{
								UE_LOG(LogDerivedDataCache, Log, TEXT("%s: Move collision, attempt at redundant update, OK %s."), *GetName(),*Filename);
							}
							else
							{
								UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Successful cache put of %s to %s"),*GetName(), CacheKey, *Filename);
								Status = EPutStatus::Cached;
							}
						}
						else
						{
							Status = EPutStatus::Cached;
						}
					}
					else
					{
						UE_LOG(LogDerivedDataCache, Warning, TEXT("%s: Temp file is short %s!"), *GetName(), *TempFilename);
					}
				}
				else
				{
					uint32 ErrorCode = FPlatformMisc::GetLastError();
					TCHAR ErrorBuffer[1024];
					FPlatformMisc::GetSystemErrorMessage(ErrorBuffer, 1024, ErrorCode);
					UE_LOG(LogDerivedDataCache, Warning, TEXT("FFileSystemDerivedDataBackend: Could not write temp file %s! Error: %u (%s)"), *TempFilename, ErrorCode, ErrorBuffer);
				}
				// if everything worked, this is not necessary, but we will make every effort to avoid leaving junk in the cache
				if (FPaths::FileExists(TempFilename))
				{
					IFileManager::Get().Delete(*TempFilename, false, false, true);
				}
			}
			else
			{
				COOK_STAT(Timer.AddMiss(Data.Num()));
				UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s skipping put to existing file %s"), *GetName(), CacheKey);
				Status = EPutStatus::Cached;
			}

			// If not using a shared cache, update estimated build time
			if (!GetDerivedDataCacheRef().GetUsingSharedDDC())
			{
				FScopeLock ScopeLock(&SynchronizationObject);

				if (DDCNotificationCacheTimes.Contains(CacheKey))
				{
					// There isn't any way to get exact build times in the DDC code as custom asset processing and async are factors.
					// So, estimate the asset build time based on the delta between the cache miss and the put
					TotalEstimatedBuildTime += (FPlatformTime::Seconds() - DDCNotificationCacheTimes[CacheKey]);
					DDCNotificationCacheTimes.Remove(CacheKey);

					// If more than 20 seconds has been spent building assets, send out a notification
					if (TotalEstimatedBuildTime > 20.0f)
					{
						// Send out a DDC put notification if we have any subscribers
						FDerivedDataCacheInterface::FOnDDCNotification& DDCNotificationEvent = GetDerivedDataCacheRef().GetDDCNotificationEvent();

						if (DDCNotificationEvent.IsBound())
						{
							TotalEstimatedBuildTime = 0.0f;

							DECLARE_CYCLE_STAT(TEXT("FSimpleDelegateGraphTask.PutCachedData"), STAT_FSimpleDelegateGraphTask_DDCNotification, STATGROUP_TaskGraphTasks);

							FSimpleDelegateGraphTask::CreateAndDispatchWhenReady(
								FSimpleDelegateGraphTask::FDelegate::CreateLambda([DDCNotificationEvent]() {
								DDCNotificationEvent.Broadcast(FDerivedDataCacheInterface::SharedDDCPerformanceNotification);
							}),
								GET_STATID(STAT_FSimpleDelegateGraphTask_DDCNotification),
								nullptr,
								ENamedThreads::GameThread);
						}
					}

				}

			}

			return Status;
		}
		else
		{
			UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s is read only. Skipping put of %s"), *GetName(), CacheKey);
			return EPutStatus::NotCached;
		}
	}

	void RemoveCachedData(const TCHAR* CacheKey, bool bTransient) override
	{
		check(IsUsable());
		if (IsWritable() && (!bTransient || bPurgeTransient))
		{
			FString Filename = BuildFilename(CacheKey);
			if (bTransient)
			{
				UE_LOG(LogDerivedDataCache,Verbose,TEXT("Deleting transient cached data. Key=%s Filename=%s"),CacheKey,*Filename);
			}
			IFileManager::Get().Delete(*Filename, false, false, true);
		}
	}

	virtual TSharedRef<FDerivedDataCacheStatsNode> GatherUsageStats() const override
	{
		TSharedRef<FDerivedDataCacheStatsNode> Usage = MakeShared<FDerivedDataCacheStatsNode>(this, FString::Printf(TEXT("%s.%s"), TEXT("FileSystem"), *CachePath));
		Usage->Stats.Add(TEXT(""), UsageStats);

		return Usage;
	}

	bool TryToPrefetch(TConstArrayView<FString> CacheKeys) override
	{
		return CachedDataProbablyExistsBatch(CacheKeys).CountSetBits() == CacheKeys.Num();
	}

	bool ApplyDebugOptions(FBackendDebugOptions& InOptions) override
	{
		DebugOptions = InOptions;
		return true;
	}

	virtual void Put(
		TConstArrayView<FCacheRecord> Records,
		FStringView Context,
		ECachePolicy Policy,
		IRequestOwner& Owner,
		FOnCachePutComplete&& OnComplete) override
	{
		for (const FCacheRecord& Record : Records)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(FileSystemDDC_Put);
			TRACE_COUNTER_INCREMENT(FileSystemDDC_Put);
			COOK_STAT(auto Timer = UsageStats.TimePut());
			if (PutCacheRecord(Record, Context, Policy))
			{
				UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Cache put complete for %s from '%.*s'"),
					*CachePath, *WriteToString<96>(Record.GetKey()), Context.Len(), Context.GetData());
				TRACE_COUNTER_INCREMENT(FileSystemDDC_PutHit);
				TRACE_COUNTER_ADD(FileSystemDDC_BytesWritten, MeasureCompressedCacheRecord(Record));
				COOK_STAT(Timer.AddHit(MeasureRawCacheRecord(Record)));
				if (OnComplete)
				{
					OnComplete({Record.GetKey(), EStatus::Ok});
				}
			}
			else
			{
				COOK_STAT(Timer.AddMiss(MeasureRawCacheRecord(Record)));
				if (OnComplete)
				{
					OnComplete({Record.GetKey(), EStatus::Error});
				}
			}
		}
	}

	virtual void Get(
		TConstArrayView<FCacheKey> Keys,
		FStringView Context,
		FCacheRecordPolicy Policy,
		IRequestOwner& Owner,
		FOnCacheGetComplete&& OnComplete) override
	{
		for (const FCacheKey& Key : Keys)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(FileSystemDDC_Get);
			TRACE_COUNTER_INCREMENT(FileSystemDDC_Get);
			COOK_STAT(auto Timer = UsageStats.TimeGet());
			EStatus Status = EStatus::Ok;
			if (FOptionalCacheRecord Record = GetCacheRecord(Key, Context, Policy, Status))
			{
				UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Cache hit for %s from '%.*s'"),
					*CachePath, *WriteToString<96>(Key), Context.Len(), Context.GetData());
				TRACE_COUNTER_INCREMENT(FileSystemDDC_GetHit);
				TRACE_COUNTER_ADD(FileSystemDDC_BytesRead, MeasureCompressedCacheRecord(Record.Get()));
				COOK_STAT(Timer.AddHit(MeasureRawCacheRecord(Record.Get())));
				if (OnComplete)
				{
					OnComplete({MoveTemp(Record).Get(), Status});
				}
			}
			else
			{
				if (OnComplete)
				{
					OnComplete({FCacheRecordBuilder(Key).Build(), Status});
				}
			}
		}
	}

	virtual void GetChunks(
		TConstArrayView<FCacheChunkRequest> Chunks,
		FStringView Context,
		IRequestOwner& Owner,
		FOnCacheGetChunkComplete&& OnComplete) override
	{
		TArray<FCacheChunkRequest, TInlineAllocator<16>> SortedChunks(Chunks);
		SortedChunks.StableSort(TChunkLess());

		FOptionalCacheRecord Record;
		for (const FCacheChunkRequest& Chunk : SortedChunks)
		{
			constexpr ECachePolicy SkipFlag = ECachePolicy::SkipValue;
			const bool bExistsOnly = EnumHasAnyFlags(Chunk.Policy, SkipFlag);
			TRACE_CPUPROFILER_EVENT_SCOPE(FileSystemDDC_Get);
			TRACE_COUNTER_INCREMENT(FileSystemDDC_Get);
			COOK_STAT(auto Timer = bExistsOnly ? UsageStats.TimeProbablyExists() : UsageStats.TimeGet());
			if (!Record || Record.Get().GetKey() != Chunk.Key)
			{
				FCacheRecordPolicyBuilder PolicyBuilder(ECachePolicy::None);
				const ECachePolicy RecordSkipFlags = bExistsOnly ? ECachePolicy::SkipData : ECachePolicy::None;
				PolicyBuilder.AddPayloadPolicy(Chunk.Id, Chunk.Policy | RecordSkipFlags);
				Record = GetCacheRecordOnly(Chunk.Key, Context, PolicyBuilder.Build());
			}
			if (Record)
			{
				EStatus PayloadStatus = EStatus::Ok;
				FPayload Payload = Record.Get().GetPayload(Chunk.Id);
				GetCachePayload(Chunk.Key, Context, Chunk.Policy, SkipFlag, Payload, PayloadStatus);
				if (Payload)
				{
					const uint64 RawSize = FMath::Min(Payload.GetRawSize(), Chunk.RawSize);
					UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Cache hit for %s from '%.*s'"),
						*CachePath, *WriteToString<96>(Chunk.Key, '/', Chunk.Id), Context.Len(), Context.GetData());
					TRACE_COUNTER_INCREMENT(FileSystemDDC_GetHit);
					TRACE_COUNTER_ADD(FileSystemDDC_BytesRead, Payload.GetData().GetCompressedSize());
					COOK_STAT(Timer.AddHit(Payload.HasData() ? RawSize : 0));
					if (OnComplete)
					{
						FUniqueBuffer Buffer;
						if (Payload.HasData() && !bExistsOnly)
						{
							Buffer = FUniqueBuffer::Alloc(RawSize);
							Payload.GetData().DecompressToComposite().CopyTo(Buffer, Chunk.RawOffset);
						}
						OnComplete({Chunk.Key, Chunk.Id, Chunk.RawOffset,
							RawSize, Payload.GetRawHash(), Buffer.MoveToShared(), PayloadStatus});
					}
					continue;
				}
			}

			if (OnComplete)
			{
				OnComplete({Chunk.Key, Chunk.Id, Chunk.RawOffset, 0, {}, {}, EStatus::Error});
			}
		}
	}

private:
	uint64 MeasureCompressedCacheRecord(const FCacheRecord& Record) const
	{
		return Record.GetMeta().GetSize() +
			Record.GetValuePayload().GetData().GetCompressedSize() +
			Algo::TransformAccumulate(Record.GetAttachmentPayloads(),
				[](const FPayload& Payload) { return Payload.GetData().GetCompressedSize(); }, uint64(0));
	}

	uint64 MeasureRawCacheRecord(const FCacheRecord& Record) const
	{
		return Record.GetMeta().GetSize() +
			Record.GetValuePayload().GetData().GetRawSize() +
			Algo::TransformAccumulate(Record.GetAttachmentPayloads(),
				[](const FPayload& Payload) { return Payload.GetData().GetRawSize(); }, uint64(0));
	}

	bool PutCacheRecord(const FCacheRecord& Record, FStringView Context, ECachePolicy Policy)
	{
		if (!IsWritable())
		{
			UE_LOG(LogDerivedDataCache, VeryVerbose,
				TEXT("%s: Skipped put of %s from '%.*s' because this cache store is not writable"),
				*CachePath, *WriteToString<96>(Record.GetKey()), Context.Len(), Context.GetData());
			return false;
		}

		const FCacheKey& Key = Record.GetKey();

		// Skip the request if storing to the cache is disabled.
		if (!EnumHasAnyFlags(Policy, SpeedClass == ESpeedClass::Local ? ECachePolicy::StoreLocal : ECachePolicy::StoreRemote))
		{
			UE_LOG(LogDerivedDataCache, VeryVerbose, TEXT("%s: Skipped put of %s from '%.*s' due to cache policy"),
				*CachePath, *WriteToString<96>(Key), Context.Len(), Context.GetData());
			return false;
		}

		if (ShouldSimulateMiss(Key))
		{
			UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Simulated miss for put of %s from '%.*s'"),
				*CachePath, *WriteToString<96>(Key), Context.Len(), Context.GetData());
			return false;
		}

		// Check if there is an existing record package.
		bool bRecordExists;
		FCbPackage ExistingPackage;
		TStringBuilder<256> Path;
		BuildCacheRecordPath(Key, Path);
		if (EnumHasAllFlags(Policy, ECachePolicy::SkipValue | ECachePolicy::SkipAttachments))
		{
			bRecordExists = FileExists(Path);
		}
		else
		{
			FSharedBuffer Buffer = LoadFile(Path, Context);
			FCbFieldIterator It = FCbFieldIterator::MakeRange(Buffer);
			bRecordExists = ExistingPackage.TryLoad(It);
		}

		// Save the record to a package and remove attachments that will be stored externally.
		FCbPackage Package = Record.Save();
		TArray<FCompressedBuffer, TInlineAllocator<8>> ExternalContent;
		if (ExistingPackage)
		{
			// Mirror the existing internal/external attachment storage.
			TArray<FCompressedBuffer, TInlineAllocator<8>> AllContent;
			Algo::Transform(Package.GetAttachments(), AllContent, &FCbAttachment::AsCompressedBinary);
			for (FCompressedBuffer& Content : AllContent)
			{
				const FIoHash RawHash = Content.GetRawHash();
				if (!ExistingPackage.FindAttachment(RawHash))
				{
					Package.RemoveAttachment(RawHash);
					ExternalContent.Add(MoveTemp(Content));
				}
			}
		}
		else
		{
			// Remove the largest attachments from the package until it fits within the size limits.
			TArray<FCompressedBuffer, TInlineAllocator<8>> AllContent;
			Algo::Transform(Package.GetAttachments(), AllContent, &FCbAttachment::AsCompressedBinary);
			uint64 TotalSize = Algo::TransformAccumulate(AllContent, &FCompressedBuffer::GetCompressedSize, uint64(0));
			const uint64 MaxSize = (AllContent.Num() == 1 ? MaxValueSizeKB : MaxRecordSizeKB) * 1024;
			if (TotalSize > MaxSize)
			{
				Algo::StableSortBy(AllContent, &FCompressedBuffer::GetCompressedSize, TGreater<>());
				for (FCompressedBuffer& Content : AllContent)
				{
					const uint64 CompressedSize = Content.GetCompressedSize();
					Package.RemoveAttachment(Content.GetRawHash());
					ExternalContent.Add(MoveTemp(Content));
					TotalSize -= CompressedSize;
					if (TotalSize <= MaxSize)
					{
						break;
					}
				}
			}
		}

		// Save the external content to storage.
		for (FCompressedBuffer& Content : ExternalContent)
		{
			PutCacheContent(Content, Context);
		}

		// Save the record package to storage.
		if (!bRecordExists && !SaveFile(Path, Context, [&Package](FArchive& Ar) { Package.Save(Ar); }))
		{
			return false;
		}

		if (AccessLogWriter)
		{
			AccessLogWriter->Append(Key, Path);
		}

		return true;
	}

	FOptionalCacheRecord GetCacheRecordOnly(
		const FCacheKey& Key,
		const FStringView Context,
		const FCacheRecordPolicy& Policy)
	{
		if (!IsUsable())
		{
			UE_LOG(LogDerivedDataCache, VeryVerbose,
				TEXT("%s: Skipped get of %s from '%.*s' because this cache store is not available"),
				*CachePath, *WriteToString<96>(Key), Context.Len(), Context.GetData());
			return FOptionalCacheRecord();
		}

		// Skip the request if querying the cache is disabled.
		if (!EnumHasAnyFlags(Policy.GetRecordPolicy(), SpeedClass == ESpeedClass::Local ? ECachePolicy::QueryLocal : ECachePolicy::QueryRemote))
		{
			UE_LOG(LogDerivedDataCache, VeryVerbose, TEXT("%s: Skipped get of %s from '%.*s' due to cache policy"),
				*CachePath, *WriteToString<96>(Key), Context.Len(), Context.GetData());
			return FOptionalCacheRecord();
		}

		if (ShouldSimulateMiss(Key))
		{
			UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Simulated miss for get of %s from '%.*s'"),
				*CachePath, *WriteToString<96>(Key), Context.Len(), Context.GetData());
			return FOptionalCacheRecord();
		}

		// Request the record from storage.
		TStringBuilder<256> Path;
		BuildCacheRecordPath(Key, Path);
		FSharedBuffer Buffer = LoadFile(Path, Context);
		if (Buffer.IsNull())
		{
			UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Cache miss with missing record for %s from '%.*s'"),
				*CachePath, *WriteToString<96>(Key), Context.Len(), Context.GetData());
			return FOptionalCacheRecord();
		}

		// Delete the record from storage if it is invalid.
		bool bDeleteCacheObject = true;
		ON_SCOPE_EXIT
		{
			if (bDeleteCacheObject && !bReadOnly)
			{
				IFileManager::Get().Delete(*Path, /*bRequireExists*/ false, /*bEvenReadOnly*/ false, /*bQuiet*/ true);
			}
		};

		// Validate that the record can be read as a compact binary package without crashing.
		if (ValidateCompactBinaryPackage(Buffer, ECbValidateMode::Default | ECbValidateMode::Package) != ECbValidateError::None)
		{
			UE_LOG(LogDerivedDataCache, Display, TEXT("%s: Cache miss with invalid package for %s from '%.*s'"),
				*CachePath, *WriteToString<96>(Key), Context.Len(), Context.GetData());
			return FOptionalCacheRecord();
		}

		// Load the record from the package.
		FOptionalCacheRecord Record;
		{
			FCbPackage Package;
			if (FCbFieldIterator It = FCbFieldIterator::MakeRange(Buffer); !Package.TryLoad(It))
			{
				UE_LOG(LogDerivedDataCache, Display, TEXT("%s: Cache miss with package load failure for %s from '%.*s'"),
					*CachePath, *WriteToString<96>(Key), Context.Len(), Context.GetData());
				return FOptionalCacheRecord();
			}
			Record = FCacheRecord::Load(Package);
			if (Record.IsNull())
			{
				UE_LOG(LogDerivedDataCache, Display, TEXT("%s: Cache miss with record load failure for %s from '%.*s'"),
					*CachePath, *WriteToString<96>(Key), Context.Len(), Context.GetData());
				return FOptionalCacheRecord();
			}
		}

		// Disable deletion now that the record is loaded and validated.
		bDeleteCacheObject = false;

		if (AccessLogWriter)
		{
			AccessLogWriter->Append(Key, Path);
		}

		return Record.Get();
	}

	FOptionalCacheRecord GetCacheRecord(
		const FCacheKey& Key,
		const FStringView Context,
		const FCacheRecordPolicy& Policy,
		EStatus& OutStatus)
	{
		FOptionalCacheRecord Record = GetCacheRecordOnly(Key, Context, Policy);
		if (Record.IsNull())
		{
			OutStatus = EStatus::Error;
			return Record;
		}

		OutStatus = EStatus::Ok;

		FCacheRecordBuilder RecordBuilder(Key);

		if (!EnumHasAnyFlags(Policy.GetRecordPolicy(), ECachePolicy::SkipMeta))
		{
			RecordBuilder.SetMeta(FCbObject(Record.Get().GetMeta()));
		}

		if (FPayload Payload = Record.Get().GetValuePayload())
		{
			const ECachePolicy PayloadPolicy = Policy.GetPayloadPolicy(Payload.GetId());
			GetCachePayload(Key, Context, PayloadPolicy, ECachePolicy::SkipValue, Payload, OutStatus);
			if (Payload.IsNull())
			{
				return FOptionalCacheRecord();
			}
			RecordBuilder.SetValue(MoveTemp(Payload));
		}

		for (FPayload Payload : Record.Get().GetAttachmentPayloads())
		{
			const ECachePolicy PayloadPolicy = Policy.GetPayloadPolicy(Payload.GetId());
			GetCachePayload(Key, Context, PayloadPolicy, ECachePolicy::SkipAttachments, Payload, OutStatus);
			if (Payload.IsNull())
			{
				return FOptionalCacheRecord();
			}
			RecordBuilder.AddAttachment(MoveTemp(Payload));
		}

		return RecordBuilder.Build();
	}

	bool PutCacheContent(const FCompressedBuffer& Content, const FStringView Context) const
	{
		const FIoHash& RawHash = Content.GetRawHash();
		TStringBuilder<256> Path;
		BuildCacheContentPath(RawHash, Path);
		if (!FileExists(Path))
		{
			if (!SaveFile(Path, Context, [&Content](FArchive& Ar) { Ar << const_cast<FCompressedBuffer&>(Content); }))
			{
				return false;
			}
		}
		if (AccessLogWriter)
		{
			AccessLogWriter->Append(RawHash, Path);
		}
		return true;
	}

	void GetCachePayload(
		const FCacheKey& Key,
		const FStringView Context,
		const ECachePolicy Policy,
		const ECachePolicy SkipFlag,
		FPayload& InOutPayload,
		EStatus& InOutStatus) const
	{
		const FIoHash& RawHash = InOutPayload.GetRawHash();

		if (!EnumHasAnyFlags(Policy, ECachePolicy::Query) || (EnumHasAnyFlags(Policy, SkipFlag) && InOutPayload.HasData()))
		{
			InOutPayload = FPayload(InOutPayload.GetId(), RawHash, InOutPayload.GetRawSize());
			return;
		}

		if (InOutPayload.HasData())
		{
			return;
		}

		TStringBuilder<256> Path;
		BuildCacheContentPath(RawHash, Path);
		if (EnumHasAllFlags(Policy, SkipFlag))
		{
			if (FileExists(Path))
			{
				if (AccessLogWriter)
				{
					AccessLogWriter->Append(RawHash, Path);
				}
				return;
			}
		}
		else
		{
			if (FSharedBuffer CompressedData = LoadFile(Path, Context))
			{
				if (FCompressedBuffer CompressedBuffer = FCompressedBuffer::FromCompressed(MoveTemp(CompressedData));
					CompressedBuffer && CompressedBuffer.GetRawHash() == RawHash)
				{
					if (AccessLogWriter)
					{
						AccessLogWriter->Append(RawHash, Path);
					}
					InOutPayload = FPayload(InOutPayload.GetId(), MoveTemp(CompressedBuffer));
					return;
				}
				UE_LOG(LogDerivedDataCache, Display,
					TEXT("%s: Cache miss with corrupted payload %s with hash %s for %s from '%.*s'"),
					*CachePath, *WriteToString<16>(InOutPayload.GetId()), *WriteToString<48>(RawHash),
					*WriteToString<96>(Key), Context.Len(), Context.GetData());
				InOutStatus = EStatus::Error;
				if (!EnumHasAnyFlags(Policy, ECachePolicy::PartialOnError))
				{
					InOutPayload = FPayload::Null;
				}
				return;
			}
		}

		UE_LOG(LogDerivedDataCache, Verbose,
			TEXT("%s: Cache miss with missing payload %s with hash %s for %s from '%.*s'"),
			*CachePath, *WriteToString<16>(InOutPayload.GetId()), *WriteToString<48>(RawHash), *WriteToString<96>(Key),
			Context.Len(), Context.GetData());
		InOutStatus = EStatus::Error;
		if (!EnumHasAnyFlags(Policy, ECachePolicy::PartialOnError))
		{
			InOutPayload = FPayload::Null;
		}
	}

	void BuildCacheRecordPath(const FCacheKey& CacheKey, FStringBuilderBase& Path) const
	{
		Path << CachePath << TEXT('/');
		BuildPathForCacheRecord(CacheKey, Path);
	}

	void BuildCacheContentPath(const FIoHash& RawHash, FStringBuilderBase& Path) const
	{
		Path << CachePath << TEXT('/');
		BuildPathForCacheContent(RawHash, Path);
	}

	bool SaveFile(FStringBuilderBase& Path, FStringView Context, TFunctionRef<void (FArchive&)> WriteFunction) const
	{
		TStringBuilder<256> TempPath;
		TempPath << FPathViews::GetPath(Path) << TEXT("/Temp.") << FGuid::NewGuid();

		ON_SCOPE_EXIT
		{
			IFileManager::Get().Delete(*TempPath, /*bRequireExists*/ false, /*bEvenReadOnly*/ false, /*bQuiet*/ true);
		};

		int64 ExpectedSize = 0;
		if (SaveDataToFile(TempPath, MoveTemp(WriteFunction), ExpectedSize))
		{
			if (IFileManager::Get().FileSize(*TempPath) == ExpectedSize)
			{
				if (IFileManager::Get().Move(*Path, *TempPath, /*bReplace*/ false, /*bEvenIfReadOnly*/ false, /*bAttributes*/ false, /*bDoNotRetryOrError*/ true))
				{
					return true;
				}
				else
				{
					UE_LOG(LogDerivedDataCache, Log,
						TEXT("%s: Move collision when writing file %s from '%.*s'"),
						*CachePath, *Path, Context.Len(), Context.GetData());
					return true;
				}
			}
			else
			{
				UE_LOG(LogDerivedDataCache, Warning,
					TEXT("%s: Failed to write to temp file %s when saving %s from '%.*s'. ")
					TEXT("File is %" INT64_FMT " bytes when %" INT64_FMT " bytes are expected."),
					*CachePath, *TempPath, *Path, Context.Len(), Context.GetData(),
					IFileManager::Get().FileSize(*TempPath), ExpectedSize);
				return false;
			}
		}
		else
		{
			UE_LOG(LogDerivedDataCache, Warning,
				TEXT("%s: Failed to write to temp file %s when saving %s from '%.*s'. Error 0x%08x"),
				*CachePath, *TempPath, *Path, Context.Len(), Context.GetData(), FPlatformMisc::GetLastError());
			return false;
		}
	}

	bool SaveDataToFile(
		FStringBuilderBase& Path,
		TFunctionRef<void (FArchive&)> WriteFunction,
		int64& OutWriteSize) const
	{
		if (TUniquePtr<FArchive> Ar{IFileManager::Get().CreateFileWriter(*Path, FILEWRITE_Silent)})
		{
			THashingArchiveProxy<FBlake3> HashAr(*Ar);
			WriteFunction(HashAr);
			FBlake3Hash Hash = HashAr.GetHash();
			*Ar << Hash;
			OutWriteSize = Ar->Tell();
			return Ar->Close() && !Ar->IsError();
		}
		OutWriteSize = 0;
		return false;
	}

	FSharedBuffer LoadFile(FStringBuilderBase& Path, FStringView Context) const
	{
		check(IsUsable());
		const double StartTime = FPlatformTime::Seconds();

		FSharedBuffer Buffer;

		// Check for existence before reading because it may update the modification time and avoid the
		// file being deleted by a cache cleanup thread or process.
		if (!FileExists(Path))
		{
			return Buffer;
		}

		if (TUniquePtr<FArchive> Ar{IFileManager::Get().CreateFileReader(*Path, FILEREAD_Silent)})
		{
			if (const int64 TotalSize = Ar->TotalSize(); TotalSize >= sizeof(FBlake3Hash))
			{
				const int64 DataSize = TotalSize - sizeof(FBlake3Hash);
				FUniqueBuffer MutableBuffer = FUniqueBuffer::Alloc(uint64(DataSize));
				Ar->Serialize(MutableBuffer.GetData(), DataSize);
				FBlake3Hash SavedHash;
				*Ar << SavedHash;
				if (Ar->Close())
				{
					const FBlake3Hash Hash = FBlake3::HashBuffer(MutableBuffer);
					if (Hash == SavedHash)
					{
						Buffer = MutableBuffer.MoveToShared();
					}
					else
					{
						UE_LOG(LogDerivedDataCache, Display,
							TEXT("%s: File %s from '%.*s' is corrupted and has hash %s when %s is expected."),
							*CachePath, *Path, Context.Len(), Context.GetData(),
							*WriteToString<80>(Hash), *WriteToString<80>(SavedHash));
						if (!bReadOnly)
						{
							IFileManager::Get().Delete(*Path, /*bRequireExists*/ false, /*bEvenReadOnly*/ false, /*bQuiet*/ true);
						}
					}
				}
			}
			else
			{
				UE_LOG(LogDerivedDataCache, Display,
					TEXT("%s: File %s from '%.*s' is %" INT64_FMT " bytes when at least %" INT64_FMT " bytes are required."),
					*CachePath, *Path, Context.Len(), Context.GetData(), TotalSize, int64(sizeof(FBlake3Hash)));
				if (!bReadOnly)
				{
					IFileManager::Get().Delete(*Path, /*bRequireExists*/ false, /*bEvenReadOnly*/ false, /*bQuiet*/ true);
				}
			}
		}

		const double ReadDuration = FPlatformTime::Seconds() - StartTime;
		const double ReadSpeed = ReadDuration > 0.001 ? (Buffer.GetSize() / ReadDuration) / (1024.0 * 1024.0) : 0.0;

		if (!GIsBuildMachine && ReadDuration > 5.0)
		{
			// Slower than 0.5 MiB/s?
			UE_CLOG(ReadSpeed < 0.5, LogDerivedDataCache, Warning, TEXT("%s: Loading %s from '%.*s' is very slow (%.2f MiB/s); ")
				TEXT("consider disabling this cache backend"), *CachePath, *Path, Context.Len(), Context.GetData(), ReadSpeed);
		}

		if (Buffer)
		{
			UE_LOG(LogDerivedDataCache, VeryVerbose, TEXT("%s: Loaded %s from '%.*s' (%" UINT64_FMT " bytes, %.02f secs, %.2f MiB/s)"),
				*CachePath, *Path, Context.Len(), Context.GetData(), Buffer.GetSize(), ReadDuration, ReadSpeed);
		}

		return Buffer;
	}

	bool FileExists(FStringBuilderBase& Path) const
	{
		const FDateTime TimeStamp = IFileManager::Get().GetTimeStamp(*Path);
		if (TimeStamp == FDateTime::MinValue())
		{
			return false;
		}
		if (bTouch || (!bReadOnly && (FDateTime::UtcNow() - TimeStamp).GetTotalDays() > (DaysToDeleteUnusedFiles / 4)))
		{
			IFileManager::Get().SetTimeStamp(*Path, FDateTime::UtcNow());
		}
		return true;
	}

private:
	FDerivedDataCacheUsageStats UsageStats;

	/**
	 * Threadsafe method to compute the filename from the cachekey, currently just adds a path and an extension.
	 *
	 * @param	CacheKey	Alphanumeric+underscore key of this cache item
	 * @return				filename built from the cache key
	 */
	FString BuildFilename(const TCHAR* CacheKey) const
	{
		return CachePath / BuildPathForCacheKey(CacheKey);
	}

	/** Base path we are storing the cache files in. **/
	FString	CachePath;
	/** Class of this cache */
	ESpeedClass SpeedClass;
	/** If true, we failed to write to this directory and it did not contain anything so we should not be used. */
	bool		bDisabled;
	/** If true, do not attempt to write to this cache. */
	bool		bReadOnly;
	/** If true, CachedDataProbablyExists will update the file timestamps. */
	bool		bTouch;
	/** If true, allow transient data to be removed from the cache. */
	bool		bPurgeTransient;
	/** Age of file when it should be deleted from DDC cache. */
	double		DaysToDeleteUnusedFiles;

	/** Maximum total size of compressed data stored within a record package with multiple attachments. */
	uint64		MaxRecordSizeKB = 256;
	/** Maximum total size of compressed data stored within a value package, or a record package with one attachment. */
	uint64		MaxValueSizeKB = 1024;

	/** Object used for synchronization via a scoped lock						*/
	FCriticalSection SynchronizationObject;

	// DDCNotification metrics

	/** Map of cache keys to miss times for generating timing deltas */
	TMap<FString, double> DDCNotificationCacheTimes;

	/** The total estimated build time accumulated from cache miss/put deltas */
	double TotalEstimatedBuildTime;

	/** Access log to write to */
	TUniquePtr<FAccessLogWriter> AccessLogWriter;

	/** Debug Options */
	FBackendDebugOptions DebugOptions;

	/** Keys we ignored due to miss rate settings */
	FCriticalSection MissedKeysCS;
	TSet<FName> DebugMissedKeys;
	TSet<FCacheKey> DebugMissedCacheKeys;

	TUniquePtr<FFileSystemCacheStoreMaintainer> Maintainer;

	bool ShouldSimulateMiss(const TCHAR* InKey)
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

	bool ShouldSimulateMiss(const FCacheKey& Key)
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

};

FDerivedDataBackendInterface* CreateFileSystemDerivedDataBackend(const TCHAR* CacheDirectory, const TCHAR* InParams, const TCHAR* InAccessLogFileName /*= nullptr*/)
{
	FFileSystemDerivedDataBackend* FileDDB = new FFileSystemDerivedDataBackend(CacheDirectory, InParams, InAccessLogFileName);

	if (!FileDDB->IsUsable())
	{
		delete FileDDB;
		FileDDB = NULL;
	}

	return FileDDB;
}

} // UE::DerivedData::CacheStore::FileSystem

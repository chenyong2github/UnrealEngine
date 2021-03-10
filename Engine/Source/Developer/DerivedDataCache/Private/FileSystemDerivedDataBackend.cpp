// Copyright Epic Games, Inc. All Rights Reserved.


#include "CoreMinimal.h"
#include "Algo/AllOf.h"
#include "Misc/MessageDialog.h"
#include "HAL/FileManager.h"
#include "Misc/CoreMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Guid.h"
#include "Misc/ScopeLock.h"
#include "Async/TaskGraphInterfaces.h"
#include "Async/Async.h"

#include "DerivedDataCacheInterface.h"
#include "DerivedDataBackendInterface.h"
#include "DDCCleanup.h"

#include "ProfilingDebugging/CookStats.h"
#include "DerivedDataCacheUsageStats.h"

#define MAX_BACKEND_KEY_LENGTH (120)
#define MAX_BACKEND_NUMBERED_SUBFOLDER_LENGTH (9)
#if PLATFORM_LINUX	// PATH_MAX on Linux is 4096 (getconf PATH_MAX /, also see limits.h), so this value can be larger (note that it is still arbitrary).
                    // This should not affect sharing the cache between platforms as the absolute paths will be different anyway.
	#define MAX_CACHE_DIR_LEN (3119)
#else
	#define MAX_CACHE_DIR_LEN (119)
#endif // PLATFORM_LINUX
#define MAX_CACHE_EXTENTION_LEN (4)

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

/**
 * Helper function to get the value of parsed bool as the return value
 **/
bool GetParsedBool(const TCHAR* Stream, const TCHAR* Match) 
{
	bool bValue = 0;
	FParse::Bool(Stream, Match, bValue);
	return bValue;
}

/** Delete the old files in a directory **/
void DeleteOldFiles(const TCHAR* Directory, int Age)
{
	// @todo(agrant) the original implementation of this did nothing. Do we need this?
}

/** 
 * Cache server that uses the OS filesystem
 * The entire API should be callable from any thread (except the singleton can be assumed to be called at least once before concurrent access).
**/
class FFileSystemDerivedDataBackend : public FDerivedDataBackendInterface
{
public:
	/**
	 * Constructor that should only be called once by the singleton, grabs the cache path from the ini
	 * @param InCacheDirectory	directory to store the cache in
	 * @param bForceReadOnly	if true, do not attempt to write to this cache
	*/
	FFileSystemDerivedDataBackend(const TCHAR* InCacheDirectory, const TCHAR* InParams, const TCHAR* InAccessLogFileName)
		: CachePath(InCacheDirectory)
		, SpeedClass(ESpeedClass::Unknown)
		, bReadOnly(false)
		, bTouch(false)
		, bPurgeTransient(false)
		, DaysToDeleteUnusedFiles(15)
		, bDisabled(false)
		, TotalEstimatedBuildTime(0)
	{
		// If we find a platform that has more stingent limits, this needs to be rethought.
		checkf(MAX_BACKEND_KEY_LENGTH + MAX_CACHE_DIR_LEN + MAX_BACKEND_NUMBERED_SUBFOLDER_LENGTH + MAX_CACHE_EXTENTION_LEN < FPlatformMisc::GetMaxPathLength(),
			TEXT("Not enough room left for cache keys in max path."));

		check(CachePath.Len());
		FPaths::NormalizeFilename(CachePath);

		// Params that override our instance defaults
		bReadOnly = GetParsedBool(InParams, TEXT("ReadOnly="));
		bTouch = GetParsedBool(InParams, TEXT("Touch="));
		bPurgeTransient = GetParsedBool(InParams, TEXT("PurgeTransient="));
		FParse::Value(InParams, TEXT("UnusedFileAge="), DaysToDeleteUnusedFiles);

		// Params that are used when setting up our path
		const bool bClean = GetParsedBool(InParams, TEXT("Clean="));
		const bool bFlush = GetParsedBool(InParams, TEXT("Flush="));
	

		// These are used to determine if we kick off a worker to cleanup our cache
		bool bDeleteUnused = true; // On by default
		int32 MaxFoldersToClean = -1;
		int32 MaxFileChecksPerSec = -1;

		FParse::Bool(InParams, TEXT("DeleteUnused="), bDeleteUnused);

		if (bDeleteUnused)
		{
			FParse::Value(InParams, TEXT("FoldersToClean="), MaxFoldersToClean);
			FParse::Value(InParams, TEXT("MaxFileChecksPerSec="), MaxFileChecksPerSec);
		}

		if (bFlush)
		{
			IFileManager::Get().DeleteDirectory(*(CachePath / TEXT("")), false, true);
		}
		else if (bClean)
		{
			DeleteOldFiles(InCacheDirectory, DaysToDeleteUnusedFiles);
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
				UE_LOG(LogDerivedDataCache, Warning, TEXT("Access to %s appears to be slow. 'Touch' will be disabled and queries/writes will be limited."), *CachePath);
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

				if (bDeleteUnused && !FParse::Param(FCommandLine::Get(), TEXT("NODDCCLEANUP")) && FDDCCleanup::Get())
				{
					FDDCCleanup::Get()->AddFilesystem(CachePath, DaysToDeleteUnusedFiles, MaxFoldersToClean, MaxFileChecksPerSec);
				}
			}
			
			if (IsUsable() && InAccessLogFileName != nullptr && *InAccessLogFileName != 0)
			{
				AccessLogWriter.Reset(new FAccessLogWriter(InAccessLogFileName));
			}
		}
	}

	bool RunSpeedTest(double InSkipTestsIfSeeksExceedMS, double& OutSeekTimeMS, double& OutReadSpeedMBs, double& OutWriteSpeedMBs) const
	{
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
		int TotalStatChecks = 0;
		FDateTime CurrentTime = FDateTime::Now();
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
		FAccessLogWriter(const TCHAR* FileName)
			: Archive(IFileManager::Get().CreateFileWriter(FileName, FILEWRITE_AllowRead))
		{
		}

		void Append(const TCHAR* CacheKey)
		{
			FScopeLock Lock(&CriticalSection);

			FString CacheKeyStr(CacheKey);
			if (!CacheKeys.Contains(CacheKeyStr))
			{
				CacheKeys.Add(MoveTemp(CacheKeyStr));

				auto FileName = StringCast<ANSICHAR>(*BuildPathForCacheKey(CacheKey));
				Archive->Serialize(const_cast<ANSICHAR*>(FileName.Get()), FileName.Length());
				Archive->Serialize(const_cast<ANSICHAR*>(LINE_TERMINATOR_ANSI), sizeof(LINE_TERMINATOR_ANSI) - 1);
			}
		}

	private:
		TUniquePtr<FArchive> Archive;
		FCriticalSection CriticalSection;
		TSet<FString> CacheKeys;
	};
	
	
	/**
	 * Synchronous test for the existence of a cache item
	 *
	 * @param	CacheKey	Alphanumeric+underscore key of this cache item
	 * @return				true if the data probably will be found, this can't be guaranteed because of concurrency in the backends, corruption, etc
	 */
	virtual bool CachedDataProbablyExists(const TCHAR* CacheKey) override
	{
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
				 (!bReadOnly && (FDateTime::UtcNow() - TimeStamp).GetDays() > (DaysToDeleteUnusedFiles / 4)))
			{
				IFileManager::Get().SetTimeStamp(*Filename, FDateTime::UtcNow());
			}

			if (AccessLogWriter.IsValid())
			{
				AccessLogWriter->Append(CacheKey);
			}

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
		COOK_STAT(auto Timer = UsageStats.TimeGet());
		check(IsUsable());
		FString Filename = BuildFilename(CacheKey);
		double StartTime = FPlatformTime::Seconds();

		if (ShouldSimulateMiss(CacheKey))
		{
			FScopeLock Lock(&MissedKeysCS);
			UE_LOG(LogDerivedDataCache, Verbose, TEXT("Simulating miss in %s for %s"), *GetName(), CacheKey);
			DebugMissedKeys.Add(FName(CacheKey));
			return false;
		}

		if (FFileHelper::LoadFileToArray(Data,*Filename,FILEREAD_Silent))
		{
			double ReadDuration = FPlatformTime::Seconds() - StartTime;
			double ReadSpeed = (Data.Num() / ReadDuration) / (1024.0 * 1024.0);

			if(!GIsBuildMachine && ReadDuration > 5.0)
			{				
				// Slower than 0.5MB/s?
				UE_CLOG(ReadSpeed < 0.5, LogDerivedDataCache, Warning, TEXT("%s is very slow (%.2fMB/s) when accessing %s, consider disabling it."), *CachePath, ReadSpeed, *Filename);
			}

			if (AccessLogWriter.IsValid())
			{
				AccessLogWriter->Append(CacheKey);
			}

			UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Cache hit on %s (%d bytes, %.02f secs, %.2fMB/s)"), *GetName(), CacheKey, Data.Num(), ReadDuration, ReadSpeed);
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
	virtual void PutCachedData(const TCHAR* CacheKey, TArrayView<const uint8> Data, bool bPutEvenIfExists) override
	{
		COOK_STAT(auto Timer = UsageStats.TimePut());
		check(IsUsable());

		if (IsWritable())
		{
			if (AccessLogWriter.IsValid())
			{
				AccessLogWriter->Append(CacheKey);
			}

			// don't put anything we pretended didn't exist
			if (ShouldSimulateMiss(CacheKey))
			{
				return;
			}

			if (bPutEvenIfExists || !CachedDataProbablyExists(CacheKey))
			{
				COOK_STAT(Timer.AddHit(Data.Num()));
				check(Data.Num());
				FString Filename = BuildFilename(CacheKey);
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
							}
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
		}
		else
		{
			UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s is read only. Skipping put of %s"), *GetName(), CacheKey);
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

	bool TryToPrefetch(const TCHAR* CacheKey) override
	{
		return false;
	}

	bool ApplyDebugOptions(FBackendDebugOptions& InOptions) override
	{
		DebugOptions = InOptions;
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
	FString BuildFilename(const TCHAR* CacheKey)
	{
		return CachePath / BuildPathForCacheKey(CacheKey);
	}

	/** Base path we are storing the cache files in. **/
	FString	CachePath;
	/** Class of this cache */
	ESpeedClass SpeedClass;
	/** If true, do not attempt to write to this cache **/
	bool		bReadOnly;
	
	/** If true, CachedDataProbablyExists will update the file timestamps. */
	bool		bTouch;
	/** If true, allow transient data to be removed from the cache. */
	bool		bPurgeTransient;
	/** Age of file when it should be deleted from DDC cache. */
	int32		DaysToDeleteUnusedFiles;

	/** If true, we failed to write to this directory and it did not contain anything so we should not be used **/
	bool		bDisabled;

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

	bool DidSimulateMiss(const TCHAR* InKey)
	{
		if (DebugOptions.RandomMissRate == 0 || DebugOptions.SimulateMissTypes.Num() == 0)
		{
			return false;
		}
		FScopeLock Lock(&MissedKeysCS);
		return DebugMissedKeys.Contains(FName(InKey));
	}

	bool ShouldSimulateMiss(const TCHAR* InKey)
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

};

FDerivedDataBackendInterface* CreateFileSystemDerivedDataBackend(const TCHAR* CacheDirectory, const TCHAR* InParams, const TCHAR* InAccessLogFileName /*= nullptr*/)
{
	FFileSystemDerivedDataBackend* FileDDB = new FFileSystemDerivedDataBackend( CacheDirectory, InParams, InAccessLogFileName);

	if (!FileDDB->IsUsable())
	{
		delete FileDDB;
		FileDDB = NULL;
	}

	return FileDDB;
}

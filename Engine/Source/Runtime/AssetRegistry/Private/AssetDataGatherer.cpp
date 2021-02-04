// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetDataGatherer.h"
#include "HAL/PlatformProcess.h"
#include "HAL/RunnableThread.h"
#include "Misc/AsciiSet.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "AssetRegistryArchive.h"
#include "AssetRegistryPrivate.h"
#include "PackageReader.h"
#include "Async/ParallelFor.h"
#include "Algo/AnyOf.h"

namespace AssetDataGathererConstants
{
	static const int32 MaxFilesToDiscoverBeforeFlush = 2500;
	static const int32 MaxFilesToGatherBeforeFlush = 250;
	static const int32 MinSecondsToElapseBeforeCacheWrite = 60;
}

namespace
{
	class FLambdaDirectoryStatVisitor : public IPlatformFile::FDirectoryStatVisitor
	{
	public:
		typedef TFunctionRef<bool(const TCHAR*, const FFileStatData&)> FLambdaRef;
		FLambdaRef Callback;
		explicit FLambdaDirectoryStatVisitor(FLambdaRef InCallback)
			: Callback(MoveTemp(InCallback))
		{
		}
		virtual bool Visit(const TCHAR* FilenameOrDirectory, const FFileStatData& StatData) override
		{
			return Callback(FilenameOrDirectory, StatData);
		}
	};

	static constexpr FAsciiSet InvalidLongPackageCharacters(INVALID_LONGPACKAGE_CHARACTERS);

	static bool ConvertToValidLongPackageName(const FString& Filename, /* out */ FString& LongPackageName)
	{
		// Make sure the path does not contain invalid characters. These packages will not be successfully loaded or read later.
		return FPackageName::TryConvertFilenameToLongPackageName(Filename, LongPackageName) &&
			FAsciiSet::HasNone(*LongPackageName, InvalidLongPackageCharacters);
	}
}

namespace AssetDataDiscoveryUtil
{
	bool PassesScanFilters(const TArray<FString>& InBlacklistFilters, const FString& InPath)
	{
		for (const FString& Filter : InBlacklistFilters)
		{
			if (InPath.StartsWith(Filter))
			{
				return false;
			}
		}
		return true;
	}
}

FAssetDataDiscovery::FAssetDataDiscovery(const TArray<FString>& InPaths, const TArray<FString>& InBlacklistScanFilters, bool bInIsSynchronous)
	: bIsSynchronous(bInIsSynchronous)
	, bIsDiscoveringFiles(true)
	, BlacklistScanFilters(InBlacklistScanFilters)
	, StopTaskCounter(0)
	, Thread(nullptr)
{
	DirectoriesToSearch.Reserve(InPaths.Num());
	for (const FString& Path : InPaths)
	{
		// Convert the package path to a filename with no extension (directory)
		DirectoriesToSearch.Add(FPackageName::LongPackageNameToFilename(Path / TEXT("")));
	}

	if (!bIsSynchronous && !FPlatformProcess::SupportsMultithreading())
	{
		bIsSynchronous = true;
		UE_LOG(LogAssetRegistry, Warning, TEXT("Requested asyncronous asset data discovery, but threading support is disabled. Performing a synchronous discovery instead!"));
	}

	if (bIsSynchronous)
	{
		Run();
	}
	else
	{
		Thread = FRunnableThread::Create(this, TEXT("FAssetDataDiscovery"), 0, TPri_BelowNormal);
		checkf(Thread, TEXT("Failed to create asset data discovery thread"));
	}
}

FAssetDataDiscovery::~FAssetDataDiscovery()
{
}

bool FAssetDataDiscovery::Init()
{
	return true;
}

uint32 FAssetDataDiscovery::Run()
{
	// Commenting out the code below as it causes cook-on-the-side to wait indefinitely in FAssetRegistryGenerator::Initialize
	//if (!bIsSynchronous)
	//{
	//	// If we're running asynchronous, don't allow these tasks to start until the engine is "running"
	//	// as we may still be manipulating global lists during start-up configuration
	//	while (!GIsRunning && StopTaskCounter.GetValue() == 0)
	//	{
	//		FPlatformProcess::SleepNoStats(0.1f);
	//	}
	//}

	double DiscoverStartTime = FPlatformTime::Seconds();
	int32 NumDiscoveredFiles = 0;

	FString LocalFilenamePathToPrioritize;

	TSet<FString> LocalDiscoveredPathsSet;
	TArray<FString> LocalDiscoveredDirectories;

	TArray<FDiscoveredPackageFile> LocalPriorityFilesToSearch;
	TArray<FDiscoveredPackageFile> LocalNonPriorityFilesToSearch;

	TArray<FString> LocalBlacklistScanFilters;

	// This set contains the folders that we should hide by default unless they contain assets
	TSet<FString> PathsToHideIfEmpty;
	PathsToHideIfEmpty.Add(TEXT("/Game/Collections"));

	auto FlushLocalResultsIfRequired = [&]()
	{
		if (LocalPriorityFilesToSearch.Num() > 0 || LocalNonPriorityFilesToSearch.Num() > 0 || LocalDiscoveredPathsSet.Num() > 0)
		{
			TArray<FString> LocalDiscoveredPathsArray = LocalDiscoveredPathsSet.Array();

			{
				FScopeLock CritSectionLock(&WorkerThreadCriticalSection);

				// Work around for TArray::Append(const TArray&) not growing expontentially.
				// This causes O(N^2) allocations when continuously appending small arrays,
				// at least on platforms with allocators that use QuantizeSize().
				const auto ReserveAndAppend = [](auto& To, auto&& From)
				{
					To.Reserve(FMath::RoundUpToPowerOfTwo(To.Num() + From.Num()));
					To.Append(MoveTemp(From));
				};

				// Place all the discovered files into the files to search list
				ReserveAndAppend(DiscoveredPaths, MoveTemp(LocalDiscoveredPathsArray));
				ReserveAndAppend(PriorityDiscoveredFiles, MoveTemp(LocalPriorityFilesToSearch));
				ReserveAndAppend(NonPriorityDiscoveredFiles, MoveTemp(LocalNonPriorityFilesToSearch));
			}
		}

		LocalDiscoveredPathsSet.Reset();

		LocalPriorityFilesToSearch.Reset();
		LocalNonPriorityFilesToSearch.Reset();
	};

	auto IsPriorityFile = [&](const FString& InPackageFilename) -> bool
	{
		return !bIsSynchronous && !LocalFilenamePathToPrioritize.IsEmpty() && InPackageFilename.StartsWith(LocalFilenamePathToPrioritize);
	};

	auto OnIterateDirectoryItem = [&](const TCHAR* InPackageFilename, const FFileStatData& InPackageStatData) -> bool
	{
		if (StopTaskCounter.GetValue() != 0)
		{
			// Requested to stop - break out of the directory iteration
			return false;
		}

		const FString PackageFilenameStr = InPackageFilename;
		FString PackagePath;
		bool bIsLongPackagePath = ConvertToValidLongPackageName(PackageFilenameStr, PackagePath);
		if (AssetDataDiscoveryUtil::PassesScanFilters(LocalBlacklistScanFilters, PackagePath))
		{
			if (InPackageStatData.bIsDirectory)
			{
				LocalDiscoveredDirectories.Add(PackageFilenameStr / TEXT(""));
				if (bIsLongPackagePath && !PathsToHideIfEmpty.Contains(PackagePath))
				{
					LocalDiscoveredPathsSet.Add(PackagePath);
				}
			}
			else if (FPackageName::IsPackageFilename(PackageFilenameStr))
			{
				if (bIsLongPackagePath)
				{
					if (IsPriorityFile(PackageFilenameStr))
					{
						LocalPriorityFilesToSearch.Add(FDiscoveredPackageFile(PackageFilenameStr, InPackageStatData.ModificationTime));
					}
					else
					{
						LocalNonPriorityFilesToSearch.Add(FDiscoveredPackageFile(PackageFilenameStr, InPackageStatData.ModificationTime));
					}

					LocalDiscoveredPathsSet.Add(FPackageName::GetLongPackagePath(PackagePath));

					++NumDiscoveredFiles;

					// Flush the data if we've processed enough
					if (!bIsSynchronous && (LocalPriorityFilesToSearch.Num() + LocalNonPriorityFilesToSearch.Num()) >= AssetDataGathererConstants::MaxFilesToDiscoverBeforeFlush)
					{
						FlushLocalResultsIfRequired();
					}
				}
			}
		}
		return true;
	};

	bool bIsIdle = true;

	while (StopTaskCounter.GetValue() == 0)
	{
		FString LocalDirectoryToSearch;
		{
			FScopeLock CritSectionLock(&WorkerThreadCriticalSection);

			if (DirectoriesToSearch.Num() > 0)
			{
				bIsDiscoveringFiles = true;

				LocalFilenamePathToPrioritize = FilenamePathToPrioritize;

				// Pop off the first path to search
				LocalDirectoryToSearch = DirectoriesToSearch[0];
				DirectoriesToSearch.RemoveAt(0, 1, false);
			}

			if (BlacklistScanFilters.Num() > 0)
			{
				LocalBlacklistScanFilters = MoveTemp(BlacklistScanFilters);
			}
		}

		if (LocalDirectoryToSearch.Len() > 0)
		{
			if (bIsIdle)
			{
				bIsIdle = false;

				// About to start work - reset these
				DiscoverStartTime = FPlatformTime::Seconds();
				NumDiscoveredFiles = 0;
			}

			// Iterate the current search directory
			FLambdaDirectoryStatVisitor Visitor(OnIterateDirectoryItem);
			IFileManager::Get().IterateDirectoryStat(*LocalDirectoryToSearch, Visitor);

			{
				FScopeLock CritSectionLock(&WorkerThreadCriticalSection);

				// Push back any newly discovered sub-directories
				if (LocalDiscoveredDirectories.Num() > 0)
				{
					// Use LocalDiscoveredDirectories as scratch space, then move it back out - this puts the directories we just 
					// discovered at the start of the list for the next iteration, which can help with disk locality
					LocalDiscoveredDirectories.Append(MoveTemp(DirectoriesToSearch));
					DirectoriesToSearch = MoveTemp(LocalDiscoveredDirectories);
				}
				LocalDiscoveredDirectories.Reset();

				if (!bIsSynchronous)
				{
					FlushLocalResultsIfRequired();
					SortPathsByPriority(1);
				}
			}
		}
		else
		{
			if (!bIsIdle)
			{
				bIsIdle = true;

				{
					FScopeLock CritSectionLock(&WorkerThreadCriticalSection);
					bIsDiscoveringFiles = false;
				}

				UE_LOG(LogAssetRegistry, Verbose, TEXT("Discovery took %0.6f seconds and found %d files to process"), FPlatformTime::Seconds() - DiscoverStartTime, NumDiscoveredFiles);
			}

			// Ran out of things to do... if we have any pending results, flush those now
			FlushLocalResultsIfRequired();

			if (bIsSynchronous)
			{
				// This is synchronous. Since our work is done, we should safely exit
				Stop();
			}
			else
			{
				// No work to do. Sleep for a little and try again later.
				FPlatformProcess::Sleep(0.1);
			}
		}
	}

	return 0;
}

void FAssetDataDiscovery::Stop()
{
	StopTaskCounter.Increment();
}

void FAssetDataDiscovery::Exit()
{
}

void FAssetDataDiscovery::EnsureCompletion()
{
	{
		FScopeLock CritSectionLock(&WorkerThreadCriticalSection);
		DirectoriesToSearch.Empty();
	}

	Stop();

	if (Thread != nullptr)
	{
		Thread->WaitForCompletion();
		delete Thread;
		Thread = nullptr;
	}
}

bool FAssetDataDiscovery::GetAndTrimSearchResults(TArray<FString>& OutDiscoveredPaths, TArray<FDiscoveredPackageFile>& OutDiscoveredFiles, int32& OutNumPathsToSearch)
{
	FScopeLock CritSectionLock(&WorkerThreadCriticalSection);

	OutDiscoveredPaths.Append(MoveTemp(DiscoveredPaths));
	DiscoveredPaths.Reset();

	if (PriorityDiscoveredFiles.Num() > 0)
	{
		// Use PriorityDiscoveredFiles as scratch space, then move it back out - this puts the priority files at the start of the final list
		PriorityDiscoveredFiles.Append(MoveTemp(OutDiscoveredFiles));
		PriorityDiscoveredFiles.Append(MoveTemp(NonPriorityDiscoveredFiles));
		OutDiscoveredFiles = MoveTemp(PriorityDiscoveredFiles);
	}
	else
	{
		OutDiscoveredFiles.Append(MoveTemp(NonPriorityDiscoveredFiles));
	}
	PriorityDiscoveredFiles.Reset();
	NonPriorityDiscoveredFiles.Reset();

	OutNumPathsToSearch = DirectoriesToSearch.Num();

	return bIsDiscoveringFiles;
}

void FAssetDataDiscovery::AddPathToSearch(const FString& Path)
{
	FScopeLock CritSectionLock(&WorkerThreadCriticalSection);

	// Convert the package path to a filename with no extension (directory)
	DirectoriesToSearch.Add(FPackageName::LongPackageNameToFilename(Path / TEXT("")));
}

void FAssetDataDiscovery::PrioritizeSearchPath(const FString& PathToPrioritize)
{
	FString LocalFilenamePathToPrioritize;
	if (FPackageName::TryConvertLongPackageNameToFilename(PathToPrioritize / TEXT(""), LocalFilenamePathToPrioritize))
	{
		FScopeLock CritSectionLock(&WorkerThreadCriticalSection);

		FilenamePathToPrioritize = LocalFilenamePathToPrioritize;
		SortPathsByPriority(INDEX_NONE);
	}
}

void FAssetDataDiscovery::SetBlacklistScanFilters(const TArray<FString>& InBlacklistScanFilters)
{
	FScopeLock CritSectionLock(&WorkerThreadCriticalSection);
	BlacklistScanFilters = InBlacklistScanFilters;
}

void FAssetDataDiscovery::SortPathsByPriority(const int32 MaxNumToSort)
{
	FScopeLock CritSectionLock(&WorkerThreadCriticalSection);

	// Critical section. This code needs to be as fast as possible since it is in a critical section!
	// Swap all priority files to the top of the list
	if (FilenamePathToPrioritize.Len() > 0)
	{
		int32 LowestNonPriorityPathIdx = 0;
		int32 NumSorted = 0;
		const int32 NumToSort = (MaxNumToSort == INDEX_NONE) ? DirectoriesToSearch.Num() : FMath::Min(DirectoriesToSearch.Num(), MaxNumToSort);
		for (int32 DirIdx = 0; DirIdx < DirectoriesToSearch.Num(); ++DirIdx)
		{
			if (DirectoriesToSearch[DirIdx].StartsWith(FilenamePathToPrioritize))
			{
				DirectoriesToSearch.Swap(DirIdx, LowestNonPriorityPathIdx);
				LowestNonPriorityPathIdx++;

				if (++NumSorted >= NumToSort)
				{
					break;
				}
			}
		}
	}
}


FAssetDataGatherer::FAssetDataGatherer(const TArray<FString>& InPaths, const TArray<FString>& InSpecificFiles, const TArray<FString>& InBlacklistScanFilters, bool bInIsSynchronous, EAssetDataCacheMode AssetDataCacheMode)
	: StopTaskCounter( 0 )
	, bIsSynchronous( bInIsSynchronous )
	, bIsDiscoveringFiles( false )
	, SearchStartTime( 0 )
	, BlacklistScanFilters(InBlacklistScanFilters)
	, NumPathsToSearchAtLastSyncPoint( InPaths.Num() )
	, bLoadAndSaveCache( false )
	, bFinishedInitialDiscovery( false )
	, Thread(nullptr)
{
	bGatherDependsData = (GIsEditor && !FParse::Param( FCommandLine::Get(), TEXT("NoDependsGathering") )) || FParse::Param(FCommandLine::Get(),TEXT("ForceDependsGathering"));

	if (FParse::Param(FCommandLine::Get(), TEXT("NoAssetRegistryCache")) || FParse::Param(FCommandLine::Get(), TEXT("multiprocess")))
	{
		bLoadAndSaveCache = false;
	}
	else if (AssetDataCacheMode != EAssetDataCacheMode::NoCache)
	{
		if (AssetDataCacheMode == EAssetDataCacheMode::UseMonolithicCache)
		{
			bLoadAndSaveCache = true;
			CacheFilename = FPaths::ProjectIntermediateDir() / (bGatherDependsData ? TEXT("CachedAssetRegistry.bin") : TEXT("CachedAssetRegistryNoDeps.bin"));
		}
		else if (InPaths.Num() > 0)
		{
			// Sort the paths to try and build a consistent hash for this input
			TArray<FString> SortedPaths = InPaths;
			SortedPaths.StableSort();

			// todo: handle hash collisions?
			uint32 CacheHash = GetTypeHash(SortedPaths[0]);
			for (int32 PathIndex = 1; PathIndex < SortedPaths.Num(); ++PathIndex)
			{
				CacheHash = HashCombine(CacheHash, GetTypeHash(SortedPaths[PathIndex]));
			}

			bLoadAndSaveCache = true;
			CacheFilename = FPaths::ProjectIntermediateDir() / TEXT("AssetRegistryCache") / FString::Printf(TEXT("%08x%s.bin"), CacheHash, bGatherDependsData ? TEXT("") : TEXT("NoDeps"));
		}
	}

	// Add any specific files before doing search
	AddFilesToSearch(InSpecificFiles);
	if (!bIsSynchronous && !FPlatformProcess::SupportsMultithreading())
	{
		bIsSynchronous = true;
		UE_LOG(LogAssetRegistry, Warning, TEXT("Requested asyncronous asset data gather, but threading support is disabled. Performing a synchronous gather instead!"));
	}

	if (bIsSynchronous)
	{
		// Run the package file discovery synchronously
		FAssetDataDiscovery PackageFileDiscovery(InPaths, BlacklistScanFilters, bIsSynchronous);
		PackageFileDiscovery.GetAndTrimSearchResults(DiscoveredPaths, FilesToSearch, NumPathsToSearchAtLastSyncPoint);

		Run();
	}
	else
	{
		BackgroundPackageFileDiscovery = MakeShared<FAssetDataDiscovery>(InPaths, BlacklistScanFilters, bIsSynchronous);
		Thread = FRunnableThread::Create(this, TEXT("FAssetDataGatherer"), 0, TPri_BelowNormal);
		checkf(Thread, TEXT("Failed to create asset data gatherer thread"));
	}
}

FAssetDataGatherer::~FAssetDataGatherer()
{
	NewCachedAssetDataMap.Empty();
	DiskCachedAssetDataMap.Empty();

	for ( auto CacheIt = NewCachedAssetData.CreateConstIterator(); CacheIt; ++CacheIt )
	{
		delete (*CacheIt);
	}
	NewCachedAssetData.Empty();
}

bool FAssetDataGatherer::Init()
{
	return true;
}

uint32 FAssetDataGatherer::Run()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FAssetDataGatherer::Run)
	
	static constexpr uint32 RemovedCacheSerializationVersionMagic = 0xAF014867;

	if ( bLoadAndSaveCache )
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ReadAssetCacheFile)

		// load the cached data
		TUniquePtr<FArchive> FileAr(IFileManager::Get().CreateFileReader(*CacheFilename, FILEREAD_Silent));
		if (FileAr && !FileAr->IsError() && FileAr->TotalSize() > 2 * sizeof(uint32))
		{
			uint32 MagicNumber = 0;
			*FileAr << MagicNumber;

			if (!FileAr->IsError() && MagicNumber == RemovedCacheSerializationVersionMagic)
			{
				FAssetRegistryVersion::Type RegistryVersion;
				if (FAssetRegistryVersion::SerializeVersion(*FileAr, RegistryVersion) && RegistryVersion == FAssetRegistryVersion::LatestVersion)
				{
					FAssetRegistryReader RegistryReader(*FileAr);
					if (!RegistryReader.IsError())
					{
						SerializeCache(RegistryReader);
					}	
				}
			}
		}
	}

	TArray<FDiscoveredPackageFile> LocalFilesToSearch;
	TArray<FAssetData*> LocalAssetResults;
	TArray<FPackageDependencyData> LocalDependencyResults;
	TArray<FString> LocalCookedPackageNamesWithoutAssetDataResults;

	const double InitialScanStartTime = FPlatformTime::Seconds();
	double LastCacheWriteTime = InitialScanStartTime;
	int32 NumCachedFiles = 0;
	int32 NumUncachedFiles = 0;

	auto WriteAssetCacheFile = [&]()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(WriteAssetCacheFile);

		// Save to a temp file first, then move to the destination to avoid corruption
		FString TempFilename(CacheFilename + TEXT(".tmp"));

		TUniquePtr<FArchive> FileAr(IFileManager::Get().CreateFileWriter(*TempFilename, 0));
		if (FileAr)
		{
			int32 MagicNumber = RemovedCacheSerializationVersionMagic;
			*FileAr << MagicNumber;

			FAssetRegistryVersion::Type RegistryVersion = FAssetRegistryVersion::LatestVersion;
			FAssetRegistryVersion::SerializeVersion(*FileAr, RegistryVersion);

#if ALLOW_NAME_BATCH_SAVING
			{
				// We might be able to reduce load time by using AssetRegistry::SerializationOptions
				// to save certain common tags as FName.
				FAssetRegistryWriter Ar(FAssetRegistryWriterOptions(), *FileAr);
				SerializeCache(Ar);
			}
#else		
		checkf(false, TEXT("Cannot save asset registry cache in this configuration"));
#endif

			// Close file handle before moving temp file to target 
			FileAr.Reset();
			IFileManager::Get().Move(*CacheFilename, *TempFilename);
		}
		else
		{
			UE_LOG(LogAssetRegistry, Error, TEXT("Failed to open file for write %s"), *TempFilename);
		}
	};

	while ( true )
	{
		bool LocalIsDiscoveringFiles = false;

		{
			FScopeLock CritSectionLock(&WorkerThreadCriticalSection);

			AssetResults.Append(MoveTemp(LocalAssetResults));
			DependencyResults.Append(MoveTemp(LocalDependencyResults));
			CookedPackageNamesWithoutAssetDataResults.Append(MoveTemp(LocalCookedPackageNamesWithoutAssetDataResults));
			LocalAssetResults.Reset();
			LocalDependencyResults.Reset();
			LocalCookedPackageNamesWithoutAssetDataResults.Reset();

			if (StopTaskCounter.GetValue() != 0)
			{
				break;
			}

			// Grab any new package files from the background directory scan
			if (BackgroundPackageFileDiscovery.IsValid())
			{
				bIsDiscoveringFiles = BackgroundPackageFileDiscovery->GetAndTrimSearchResults(DiscoveredPaths, FilesToSearch, NumPathsToSearchAtLastSyncPoint);
				LocalIsDiscoveringFiles = bIsDiscoveringFiles;
			}


			if (FilesToSearch.Num() > 0)
			{
				if (SearchStartTime == 0)
				{
					SearchStartTime = FPlatformTime::Seconds();
				}

				const int32 NumFilesToProcess = FMath::Min<int32>(AssetDataGathererConstants::MaxFilesToGatherBeforeFlush, FilesToSearch.Num());
				LocalFilesToSearch.Append(FilesToSearch.GetData(), NumFilesToProcess);
				FilesToSearch.RemoveAt(0, NumFilesToProcess, false);
			}
			else if (SearchStartTime != 0 && !LocalIsDiscoveringFiles)
			{
				SearchTimes.Add(FPlatformTime::Seconds() - SearchStartTime);
				SearchStartTime = 0;
			}
		}

		TArray<FDiscoveredPackageFile> LocalFilesToRetry;

		if (LocalFilesToSearch.Num() > 0)
		{
			struct FReadContext
			{
				FName PackageName;
				FName Extension;
				const FDiscoveredPackageFile& AssetFileData;
				TArray<FAssetData*> AssetDataFromFile;
				FPackageDependencyData DependencyData;
				TArray<FString> CookedPackageNamesWithoutAssetData;
				bool bCanAttemptAssetRetry = false;
				bool bResult = false;

				FReadContext(FName InPackageName, FName InExtension, const FDiscoveredPackageFile& InAssetFileData)
					: PackageName(InPackageName)
					, Extension(InExtension)
					, AssetFileData(InAssetFileData)
				{
				}
			};

			TArray<FReadContext> ReadContexts;
			FString LongPackageNameString;
			for (const FDiscoveredPackageFile& AssetFileData : LocalFilesToSearch)
			{
				if (StopTaskCounter.GetValue() != 0)
				{
					// We have been asked to stop, so don't read any more files
					break;
				}

				LongPackageNameString.Reset();
				if (!FPackageName::TryConvertFilenameToLongPackageName(AssetFileData.PackageFilename, LongPackageNameString))
				{
					// Conversion is expected to fail when the path has recently been unmounted, fail silent instead of fatal crash
					continue;
				}

				const FName PackageName = *LongPackageNameString;
				const FName Extension = FName(*FPaths::GetExtension(AssetFileData.PackageFilename));

				bool bLoadedFromCache = false;
				if (bLoadAndSaveCache)
				{
					FDiskCachedAssetData* DiskCachedAssetData = DiskCachedAssetDataMap.Find(PackageName);
					if (DiskCachedAssetData)
					{
						const FDateTime& CachedTimestamp = DiskCachedAssetData->Timestamp;
						if (AssetFileData.PackageTimestamp != CachedTimestamp)
						{
							DiskCachedAssetData = nullptr;
						}
						else if ((DiskCachedAssetData->DependencyData.PackageName != PackageName && DiskCachedAssetData->DependencyData.PackageName != NAME_None) ||
								DiskCachedAssetData->Extension != Extension)
						{
							UE_LOG(LogAssetRegistry, Display, TEXT("Cached dependency data for package '%s' is invalid. Discarding cached data."), *PackageName.ToString());
							DiskCachedAssetData = nullptr;
						}
					}

					if (DiskCachedAssetData)
					{
						bLoadedFromCache = true;

						++NumCachedFiles;

						LocalAssetResults.Reserve(LocalAssetResults.Num() + DiskCachedAssetData->AssetDataList.Num());
						for (const FAssetData& AssetData : DiskCachedAssetData->AssetDataList)
						{
							LocalAssetResults.Add(new FAssetData(AssetData));
						}

						if (bGatherDependsData)
						{
							LocalDependencyResults.Add(DiskCachedAssetData->DependencyData);
						}

						AddToCache(PackageName, DiskCachedAssetData);
					}
				}

				if (!bLoadedFromCache)
				{
					ReadContexts.Emplace(PackageName, Extension, AssetFileData);
				}
			}

			ParallelFor(ReadContexts.Num(),
				[this, &ReadContexts](int32 Index)
				{
					FReadContext& ReadContext = ReadContexts[Index];
					ReadContext.bResult = ReadAssetFile(ReadContext.AssetFileData.PackageFilename, ReadContext.AssetDataFromFile, ReadContext.DependencyData, ReadContext.CookedPackageNamesWithoutAssetData, ReadContext.bCanAttemptAssetRetry);
				},
				EParallelForFlags::Unbalanced | EParallelForFlags::BackgroundPriority
			);

			for (FReadContext& ReadContext : ReadContexts)
			{
				if (ReadContext.bResult)
				{
					++NumUncachedFiles;

					LocalCookedPackageNamesWithoutAssetDataResults.Append(MoveTemp(ReadContext.CookedPackageNamesWithoutAssetData));

					// Don't store info on cooked packages
					bool bCachePackage = bLoadAndSaveCache && LocalCookedPackageNamesWithoutAssetDataResults.Num() == 0;
					if (bCachePackage)
					{
						for (const FAssetData* AssetData : ReadContext.AssetDataFromFile)
						{
							if (!!(AssetData->PackageFlags & PKG_FilterEditorOnly))
							{
								bCachePackage = false;
								break;
							}
						}
					}

					if (bCachePackage)
					{
						// Update the cache
						FDiskCachedAssetData* NewData = new FDiskCachedAssetData(ReadContext.AssetFileData.PackageTimestamp, ReadContext.Extension);
						NewData->AssetDataList.Reserve(ReadContext.AssetDataFromFile.Num());
						for (const FAssetData* BackgroundAssetData : ReadContext.AssetDataFromFile)
						{
							NewData->AssetDataList.Add(*BackgroundAssetData);
						}

						// MoveTemp only used if we don't need DependencyData anymore
						if (bGatherDependsData)
						{
							NewData->DependencyData = ReadContext.DependencyData;
						}
						else
						{
							NewData->DependencyData = MoveTemp(ReadContext.DependencyData);
						}

						NewCachedAssetData.Add(NewData);
						AddToCache(ReadContext.PackageName, NewData);
					}

					LocalAssetResults.Append(MoveTemp(ReadContext.AssetDataFromFile));
					if (bGatherDependsData)
					{
						LocalDependencyResults.Add(MoveTemp(ReadContext.DependencyData));
					}
				}
				else if (ReadContext.bCanAttemptAssetRetry)
				{
					LocalFilesToRetry.Add(ReadContext.AssetFileData);
				}
			}

			LocalFilesToSearch.Reset();
			LocalFilesToSearch.Append(LocalFilesToRetry);
			LocalFilesToRetry.Reset();

			if (bLoadAndSaveCache)
			{
				// Only write intermediate state cache file if we have spent a good amount of time working on it
				if (FPlatformTime::Seconds() - LastCacheWriteTime >= AssetDataGathererConstants::MinSecondsToElapseBeforeCacheWrite)
				{
					WriteAssetCacheFile();
					LastCacheWriteTime = FPlatformTime::Seconds();
				}
			}
		}
		else
		{
			if (bIsSynchronous)
			{
				// This is synchronous. Since our work is done, we should safely exit
				Stop();
			}
			else
			{
				if (!LocalIsDiscoveringFiles && !bFinishedInitialDiscovery)
				{
					bFinishedInitialDiscovery = true;

					UE_LOG(LogAssetRegistry, Verbose, TEXT("Initial scan took %0.6f seconds (found %d cached assets, and loaded %d)"), FPlatformTime::Seconds() - InitialScanStartTime, NumCachedFiles, NumUncachedFiles);

					// If we are caching discovered assets and this is the first time we had no work to do, save off the cache now in case the user terminates unexpectedly
					if (bLoadAndSaveCache)
					{
						WriteAssetCacheFile();
					}
				}

				// No work to do. Sleep for a little and try again later.
				FPlatformProcess::Sleep(0.1);
			}
		}
	}

	check(LocalAssetResults.Num() == 0); // All LocalAssetResults needed to be copied to AssetResults to avoid leaking memory

	if ( bLoadAndSaveCache )
	{
		WriteAssetCacheFile();
	}

	return 0;
}

void FAssetDataGatherer::AddToCache(FName PackageName, FDiskCachedAssetData* DiskCachedAssetData)
{
	FDiskCachedAssetData*& ValueInMap = NewCachedAssetDataMap.FindOrAdd(PackageName, DiskCachedAssetData);
	if (ValueInMap != DiskCachedAssetData)
	{
		// An updated DiskCachedAssetData for the same package; replace the existing DiskCachedAssetData with the new one.
		// Note that memory management of the DiskCachedAssetData is handled in a separate structure; we do not need to delete the old value here.
		if (DiskCachedAssetData->Extension != ValueInMap->Extension)
		{
			// Two files with the same package name but different extensions, e.g. basename.umap and basename.uasset
			// This is invalid - some systems in the engine (Cooker's FPackageNameCache) assume that package : filename is 1 : 1 - so issue a warning
			// Because it is invalid, we don't fully support it here (our map is keyed only by packagename), and will remove from cache all but the last filename we find with the same packagename
			// TODO: Turn this into a warning once all sample projects have fixed it
			UE_LOG(LogAssetRegistry, Display, TEXT("Multiple files exist with the same package name %s but different extensions (%s and %s). ")
				TEXT("This is invalid and will cause errors; merge or rename or delete one of the files."),
				*PackageName.ToString(), *ValueInMap->Extension.ToString(), *DiskCachedAssetData->Extension.ToString());
		}
		ValueInMap = DiskCachedAssetData;
	}
}

void FAssetDataGatherer::Stop()
{
	if (BackgroundPackageFileDiscovery.IsValid())
	{
		BackgroundPackageFileDiscovery->Stop();
	}

	StopTaskCounter.Increment();
}

void FAssetDataGatherer::Exit()
{   
}

void FAssetDataGatherer::EnsureCompletion()
{
	if (BackgroundPackageFileDiscovery.IsValid())
	{
		BackgroundPackageFileDiscovery->EnsureCompletion();
	}

	{
		FScopeLock CritSectionLock(&WorkerThreadCriticalSection);
		FilesToSearch.Empty();
	}

	Stop();

	if (Thread != nullptr)
	{
		Thread->WaitForCompletion();
		delete Thread;
		Thread = nullptr;
	}
}

bool FAssetDataGatherer::GetAndTrimSearchResults(TBackgroundGatherResults<FAssetData*>& OutAssetResults, TBackgroundGatherResults<FString>& OutPathResults, TBackgroundGatherResults<FPackageDependencyData>& OutDependencyResults, TBackgroundGatherResults<FString>& OutCookedPackageNamesWithoutAssetDataResults, TArray<double>& OutSearchTimes, int32& OutNumFilesToSearch, int32& OutNumPathsToSearch, bool& OutIsDiscoveringFiles)
{
	FScopeLock CritSectionLock(&WorkerThreadCriticalSection);

	OutAssetResults.Append(MoveTemp(AssetResults));
	AssetResults.Reset();

	OutPathResults.Append(MoveTemp(DiscoveredPaths));
	DiscoveredPaths.Reset();

	OutDependencyResults.Append(MoveTemp(DependencyResults));
	DependencyResults.Reset();

	OutCookedPackageNamesWithoutAssetDataResults.Append(MoveTemp(CookedPackageNamesWithoutAssetDataResults));
	CookedPackageNamesWithoutAssetDataResults.Reset();

	OutSearchTimes.Append(MoveTemp(SearchTimes));
	SearchTimes.Reset();

	OutNumFilesToSearch = FilesToSearch.Num();
	OutNumPathsToSearch = NumPathsToSearchAtLastSyncPoint;
	OutIsDiscoveringFiles = bIsDiscoveringFiles;

	return (SearchStartTime > 0 || bIsDiscoveringFiles);
}

void FAssetDataGatherer::AddPathToSearch(const FString& Path)
{
	if (BackgroundPackageFileDiscovery.IsValid())
	{
		BackgroundPackageFileDiscovery->AddPathToSearch(Path);
	}
}

void FAssetDataGatherer::AddFilesToSearch(const TArray<FString>& Files)
{
	TArray<FString> FilesToAdd;
	FilesToAdd.Reserve(Files.Num());
	FString LongPackageName;
	for (const FString& Filename : Files)
	{
		LongPackageName.Reset();
		if (ConvertToValidLongPackageName(Filename, LongPackageName)
			&& AssetDataDiscoveryUtil::PassesScanFilters(BlacklistScanFilters, LongPackageName))
		{
			// Add the path to this asset into the list of discovered paths
			FilesToAdd.Add(Filename);
		}
	}

	if (FilesToAdd.Num() > 0)
	{
		FScopeLock CritSectionLock(&WorkerThreadCriticalSection);
		FilesToSearch.Append(MoveTemp(FilesToAdd));
	}
}

void FAssetDataGatherer::PrioritizeSearchPath(const FString& PathToPrioritize)
{
	if (BackgroundPackageFileDiscovery.IsValid())
	{
		BackgroundPackageFileDiscovery->PrioritizeSearchPath(PathToPrioritize);
	}

	FString LocalFilenamePathToPrioritize;
	if (FPackageName::TryConvertLongPackageNameToFilename(PathToPrioritize / TEXT(""), LocalFilenamePathToPrioritize))
	{
		FScopeLock CritSectionLock(&WorkerThreadCriticalSection);

		FilenamePathToPrioritize = LocalFilenamePathToPrioritize;
		SortPathsByPriority(INDEX_NONE);
	}
}

void FAssetDataGatherer::SetBlacklistScanFilters(const TArray<FString>& InBlacklistScanFilters)
{
	if (BackgroundPackageFileDiscovery.IsValid())
	{
		BackgroundPackageFileDiscovery->SetBlacklistScanFilters(InBlacklistScanFilters);
	}
}

void FAssetDataGatherer::SortPathsByPriority(const int32 MaxNumToSort)
{
	FScopeLock CritSectionLock(&WorkerThreadCriticalSection);

	// Critical section. This code needs to be as fast as possible since it is in a critical section!
	// Swap all priority files to the top of the list
	if (FilenamePathToPrioritize.Len() > 0)
	{
		int32 LowestNonPriorityFileIdx = 0;
		int32 NumSorted = 0;
		const int32 NumToSort = (MaxNumToSort == INDEX_NONE) ? FilesToSearch.Num() : FMath::Min(FilesToSearch.Num(), MaxNumToSort);
		for (int32 FilenameIdx = 0; FilenameIdx < FilesToSearch.Num(); ++FilenameIdx)
		{
			if (FilesToSearch[FilenameIdx].PackageFilename.StartsWith(FilenamePathToPrioritize))
			{
				FilesToSearch.Swap(FilenameIdx, LowestNonPriorityFileIdx);
				LowestNonPriorityFileIdx++;

				if (++NumSorted >= NumToSort)
				{
					break;
				}
			}
		}
	}
}

bool FAssetDataGatherer::ReadAssetFile(const FString& AssetFilename, TArray<FAssetData*>& AssetDataList, FPackageDependencyData& DependencyData, TArray<FString>& CookedPackageNamesWithoutAssetData, bool& OutCanRetry) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FAssetDataGatherer::ReadAssetFile)
	OutCanRetry = false;
	AssetDataList.Reset();

	FPackageReader PackageReader;

	FPackageReader::EOpenPackageResult OpenPackageResult;
	if ( !PackageReader.OpenPackageFile(AssetFilename, &OpenPackageResult) )
	{
		// If we're missing a custom version, we might be able to load this package later once the module containing that version is loaded...
		//   -	We can only attempt a retry in editors (not commandlets) that haven't yet finished initializing (!GIsRunning), as we 
		//		have no guarantee that a commandlet or an initialized editor is going to load any more modules/plugins
		//   -	Likewise, we can only attempt a retry for asynchronous scans, as during a synchronous scan we won't be loading any 
		//		modules/plugins so it would last forever
		const bool bAllowRetry = GIsEditor && !IsRunningCommandlet() && !GIsRunning && !bIsSynchronous;
		OutCanRetry = bAllowRetry && OpenPackageResult == FPackageReader::EOpenPackageResult::CustomVersionMissing;
		return false;
	}

	if ( PackageReader.ReadAssetRegistryDataIfCookedPackage(AssetDataList, CookedPackageNamesWithoutAssetData) )
	{
		// Cooked data is special. No further data is found in these packages
		return true;
	}

	if ( !PackageReader.ReadAssetRegistryData(AssetDataList) )
	{
		if ( !PackageReader.ReadAssetDataFromThumbnailCache(AssetDataList) )
		{
			// It's ok to keep reading even if the asset registry data doesn't exist yet
			//return false;
		}
	}

	if ( bGatherDependsData )
	{
		if ( !PackageReader.ReadDependencyData(DependencyData) )
		{
			return false;
		}

		// DEPRECATION_TODO: Remove this fixup-on-load once we bump EUnrealEngineObjectUE4Version VER_UE4_ASSETREGISTRY_DEPENDENCYFLAGS and therefore all projects will resave each ObjectRedirector
		// UObjectRedirectors were originally incorrectly marked as having editor-only imports, since UObjectRedirector is an editor-only class. But UObjectRedirectors are followed during cooking
		// and so their imports should be considered used-in-game. Mark all dependencies in the package as used in game if the package has a UObjectRedirector object
		FName RedirectorClassName = UObjectRedirector::StaticClass()->GetFName();
		if (Algo::AnyOf(AssetDataList, [RedirectorClassName](FAssetData* AssetData) { return AssetData->AssetClass == RedirectorClassName; }))
		{
			TBitArray<>& ImportUsedInGame = DependencyData.ImportUsedInGame;
			for (int32 ImportNum = ImportUsedInGame.Num(), Index = 0; Index < ImportNum; ++Index)
			{
				ImportUsedInGame[Index] = true;
			}
		}
		// END DEPRECATION_TODO
	}

	return true;
}

template<class Archive>
void FAssetDataGatherer::SerializeCache(Archive&& Ar)
{
	double SerializeStartTime = FPlatformTime::Seconds();

	// serialize number of objects
	int32 LocalNumAssets = NewCachedAssetDataMap.Num();
	Ar << LocalNumAssets;

	if (Ar.IsSaving())
	{
		// save out by walking the TMap
		for (TPair<FName, FDiskCachedAssetData*>& Pair : NewCachedAssetDataMap)
		{
			Ar << Pair.Key;
			Pair.Value->SerializeForCache(Ar);
		}
	}
	else
	{
		// allocate one single block for all asset data structs (to reduce tens of thousands of heap allocations)
		if (Ar.IsError() || LocalNumAssets < 0)
		{
			Ar.SetError();
		}
		else
		{
			FSoftObjectPathSerializationScope SerializationScope(NAME_None, NAME_None, ESoftObjectPathCollectType::NeverCollect, ESoftObjectPathSerializeType::AlwaysSerialize);

			const int32 MinAssetEntrySize = sizeof(int32);
			int32 MaxReservation = (Ar.TotalSize() - Ar.Tell()) / MinAssetEntrySize;
			DiskCachedAssetDataMap.Empty(FMath::Min(LocalNumAssets, MaxReservation));

			for (int32 AssetIndex = 0; AssetIndex < LocalNumAssets; ++AssetIndex)
			{
				// Load the name first to add the entry to the tmap below
				FName PackageName;
				Ar << PackageName;
				if (Ar.IsError())
				{
					// There was an error reading the cache. Bail out.
					break;
				}

				// Add to the cached map
				FDiskCachedAssetData& CachedAssetData = DiskCachedAssetDataMap.Add(PackageName);

				// Now load the data
				CachedAssetData.SerializeForCache(Ar);

				if (Ar.IsError())
				{
					// There was an error reading the cache. Bail out.
					break;
				}
			}
		}

		// If there was an error loading the cache, abandon all data loaded from it so we can build a clean one.
		if (Ar.IsError())
		{
			UE_LOG(LogAssetRegistry, Error, TEXT("There was an error loading the asset registry cache. Generating a new one."));
			DiskCachedAssetDataMap.Empty();
		}
	}

	UE_LOG(LogAssetRegistry, Verbose, TEXT("Asset data gatherer serialized in %0.6f seconds"), FPlatformTime::Seconds() - SerializeStartTime);
}

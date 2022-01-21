// Copyright Epic Games, Inc. All Rights Reserved.

#include "VirtualizationManager.h"

#include "HAL/PlatformTime.h"
#include "IVirtualizationBackend.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreDelegates.h"
#include "Misc/PackageName.h"
#include "Misc/PackagePath.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "ProfilingDebugging/CookStats.h"

#include "VirtualizationFilterSettings.h"

namespace UE::Virtualization
{
UE_REGISTER_VIRTUALIZATION_SYSTEM(UE::Virtualization::FVirtualizationManager, Default);

/** Utility struct, similar to FScopeLock but allows the lock to be enabled/disabled more easily */
struct FConditionalScopeLock
{
	UE_NONCOPYABLE(FConditionalScopeLock);

	FConditionalScopeLock(FCriticalSection* InSyncObject, bool bShouldLock)
	{
		checkf(InSyncObject != nullptr, TEXT("InSyncObject must point to a valid FCriticalSection"));

		if (bShouldLock)
		{		
			SyncObject = InSyncObject;
			SyncObject->Lock();
		}
		else
		{
			SyncObject = nullptr;
		}
	}

	/** Destructor that performs a release on the synchronization object. */
	~FConditionalScopeLock()
	{
		if (SyncObject != nullptr)
		{
			SyncObject->Unlock();
		}
	}

private:
	FCriticalSection* SyncObject;
};

/* Utility function for building up a lookup table of all available IBackendFactory interfaces*/
FVirtualizationManager::FRegistedFactories FindBackendFactories()
{
	FVirtualizationManager::FRegistedFactories BackendFactories;

	TArray<IVirtualizationBackendFactory*> FactoriesArray = IModularFeatures::Get().GetModularFeatureImplementations<IVirtualizationBackendFactory>(FName("VirtualizationBackendFactory"));
	for (IVirtualizationBackendFactory* FactoryInterface : FactoriesArray)
	{
		checkf(FactoryInterface != nullptr, TEXT("A nullptr was added to the modular features for 'VirtualizationBackendFactory'"));

		const FName FactoryName = FactoryInterface->GetName();

		if (!BackendFactories.Contains(FactoryName))
		{
			BackendFactories.Add(FactoryName, FactoryInterface);
		}
		else
		{
			UE_LOG(LogVirtualization, Error, TEXT("Duplicate IBackendFactory found! Name '%s'"), *FactoryName.ToString());
		}
	}

	return BackendFactories;
}

/* Utility function for finding entries in a given string*/
TArray<FString> ParseEntries(const FString& Data)
{
	TArray<FString> Entries;

	const TCHAR* DataPtr = *Data;

	const TCHAR* EntryLabel = TEXT("Entry=");
	const int32 EntryLabelLength = FCString::Strlen(EntryLabel);

	FString ConfigEntryName;
	while (FParse::Value(DataPtr, EntryLabel, ConfigEntryName))
	{
		Entries.Add(ConfigEntryName);

		// Skip head so we can look for any additional entries (note that we might not skip past the existing
		// entry has we have no idea how much whitespace was ignored by FParse, but it will be enough)
		DataPtr += EntryLabelLength + ConfigEntryName.Len();
	}

	return Entries;
}

/**
 * Profiling data allowing us to track how payloads are being push/pulled during the lifespan of the process. Note that as all backends are
 * created at the same time, we don't need to add locked when accessing the maps. In addition FCookStats is thread safe when adding hits/misses
 * so we don't have to worry about that either.
 * We keep the FCookStats here rather than as a member of IVirtualizationBackend to try and avoid the backends needing to be aware of the data that
 * we are gathering at all. This way all profiling code is kept to this cpp.
 */
namespace Profiling
{
#if ENABLE_COOK_STATS
	TMap<FString, FCookStats::CallStats> CacheStats;
	TMap<FString, FCookStats::CallStats> PushStats;
	TMap<FString, FCookStats::CallStats> PullStats;

	void CreateStats(const IVirtualizationBackend& Backend)
	{
		CacheStats.Add(Backend.GetDebugName());
		PushStats.Add(Backend.GetDebugName());
		PullStats.Add(Backend.GetDebugName());
	}

	FCookStats::CallStats& GetCacheStats(const IVirtualizationBackend& Backend)
	{
		return *CacheStats.Find(Backend.GetDebugName());
	}
	
	FCookStats::CallStats& GetPushStats(const IVirtualizationBackend& Backend)
	{
		return *PushStats.Find(Backend.GetDebugName());
	}

	FCookStats::CallStats& GetPullStats(const IVirtualizationBackend& Backend)
	{
		return *PullStats.Find(Backend.GetDebugName());
	}

	/** Returns true if we have gathered any profiling data at all */
	bool HasProfilingData()
	{
		auto HasAccumulatedData = [](const TMap<FString, FCookStats::CallStats>& Stats)->bool
		{
			for (const auto& Iterator : Stats)
			{
				if (Iterator.Value.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Counter) > 0)
				{
					return true;
				}

				if (Iterator.Value.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Miss, FCookStats::CallStats::EStatType::Counter) > 0)
				{
					return true;
				}
			}

			return false;
		};

		return HasAccumulatedData(CacheStats) || HasAccumulatedData(PushStats) || HasAccumulatedData(PullStats);
	}

	void LogStats()
	{
		if (!HasProfilingData())
		{
			return; // Early out if we have no data
		}

		UE_LOG(LogVirtualization, Log, TEXT(""));
		UE_LOG(LogVirtualization, Log, TEXT("Virtualization ProfileData"));
		UE_LOG(LogVirtualization, Log, TEXT("======================================================================================="));

		if (CacheStats.Num() > 0)
		{
			UE_LOG(LogVirtualization, Log, TEXT("%-40s|%17s|%12s|%14s|"), TEXT("Caching Data"), TEXT("TotalSize (MB)"), TEXT("TotalTime(s)"), TEXT("DataRate(MB/S)"));
			UE_LOG(LogVirtualization, Log, TEXT("----------------------------------------|-----------------|------------|--------------|"));

			for (const auto& Iterator : CacheStats)
			{
				const double Time = Iterator.Value.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Cycles) * FPlatformTime::GetSecondsPerCycle();
				const int64 DataSizeMB = Iterator.Value.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Bytes) / (1024 * 1024);
				const double MBps = Time != 0.0 ? (DataSizeMB / Time) : 0.0;

				UE_LOG(LogVirtualization, Log, TEXT("%-40.40s|%17" UINT64_FMT "|%12.3f|%14.3f|"),
					*Iterator.Key,
					DataSizeMB,
					Time,
					MBps);
			}

			UE_LOG(LogVirtualization, Log, TEXT("======================================================================================="));
		}

		if (PushStats.Num() > 0)
		{
			UE_LOG(LogVirtualization, Log, TEXT("%-40s|%17s|%12s|%14s|"), TEXT("Pushing Data"), TEXT("TotalSize (MB)"), TEXT("TotalTime(s)"), TEXT("DataRate(MB/S)"));
			UE_LOG(LogVirtualization, Log, TEXT("----------------------------------------|-----------------|------------|--------------|"));

			for (const auto& Iterator : PushStats)
			{
				const double Time = Iterator.Value.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Cycles) * FPlatformTime::GetSecondsPerCycle();
				const int64 DataSizeMB = Iterator.Value.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Bytes) / (1024 * 1024);
				const double MBps = Time != 0.0 ? (DataSizeMB / Time) : 0.0;

				UE_LOG(LogVirtualization, Log, TEXT("%-40.40s|%17" UINT64_FMT "|%12.3f|%14.3f|"),
					*Iterator.Key,
					DataSizeMB,
					Time,
					MBps);
			}

			UE_LOG(LogVirtualization, Log, TEXT("======================================================================================="));
		}

		if (PullStats.Num() > 0)
		{
			UE_LOG(LogVirtualization, Log, TEXT("%-40s|%17s|%12s|%14s|"), TEXT("Pulling Data"), TEXT("TotalSize (MB)"), TEXT("TotalTime(s)"), TEXT("DataRate(MB/S)"));
			UE_LOG(LogVirtualization, Log, TEXT("----------------------------------------|-----------------|------------|--------------|"));

			for (const auto& Iterator : PullStats)
			{
				const double Time = Iterator.Value.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Cycles) * FPlatformTime::GetSecondsPerCycle();
				const int64 DataSizeMB = Iterator.Value.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Bytes) / (1024 * 1024);
				const double MBps = Time != 0.0 ? (DataSizeMB / Time) : 0.0;

				UE_LOG(LogVirtualization, Log, TEXT("%-40.40s|%17" UINT64_FMT "|%12.3f|%14.3f|"),
					*Iterator.Key,
					DataSizeMB,
					Time,
					MBps);
			}

			UE_LOG(LogVirtualization, Log, TEXT("======================================================================================="));
		}
	}
#endif // ENABLE_COOK_STATS
} //namespace Profiling

FVirtualizationManager::FVirtualizationManager()
	: bEnablePayloadPushing(true)
	, bEnableCacheAfterPull(true)
	, MinPayloadLength(0)
	, BackendGraphName(TEXT("ContentVirtualizationBackendGraph_None"))
	, bForceSingleThreaded(false)
	, bValidateAfterPushOperation(false)
{
	UE_LOG(LogVirtualization, Log, TEXT("Virtualization manager created"));

	// Allows us to log the profiling data on process exit. 
	// TODO: We should just be able to call the logging in the destructor, but 
	// we need to fix the startup/shutdown ordering of Mirage first.
	COOK_STAT(FCoreDelegates::OnExit.AddStatic(Profiling::LogStats));

	FConfigFile PlatformEngineIni;
	if (FConfigCacheIni::LoadLocalIniFile(PlatformEngineIni, TEXT("Engine"), true))
	{
		ApplySettingsFromConfigFiles(PlatformEngineIni);
		ApplyDebugSettingsFromConfigFiles(PlatformEngineIni);
	}
	else
	{
		UE_LOG(LogVirtualization, Error, TEXT("Failed to load config file settings for content virtualization"));
	}

	ApplySettingsFromCmdline();
	ApplyDebugSettingsFromFromCmdline();

	MountBackends();
}

FVirtualizationManager::~FVirtualizationManager()
{
	UE_LOG(LogVirtualization, Log, TEXT("Destroying backends"));
	
	LocalCachableBackends.Empty();
	PersistentStorageBackends.Empty();
	PullEnabledBackends.Empty();

	AllBackends.Empty(); // This will delete all backends and beyond this point all references to them are invalid

	UE_LOG(LogVirtualization, Log, TEXT("Virtualization manager destroyed"));
}

bool FVirtualizationManager::IsEnabled() const
{
	return !AllBackends.IsEmpty();
}

bool FVirtualizationManager::PushData(const FPayloadId& Id, const FCompressedBuffer& Payload, EStorageType StorageType, const FString& Context)
{
	FPushRequest Request(Id, Payload, Context);
	return FVirtualizationManager::PushData(MakeArrayView(&Request, 1), StorageType);
}

bool FVirtualizationManager::PushData(TArrayView<FPushRequest> Requests, EStorageType StorageType)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FVirtualizationManager::PushData);

	TArray<FPushRequest> ValidatedRequests;
	ValidatedRequests.Reserve(Requests.Num());

	TArray<int32> OriginalToValidatedRequest; // Builds a mapping between Requests and ValidatedRequests for later
	OriginalToValidatedRequest.SetNum(Requests.Num());

	// Create a new list of FPushRequest, excluding the requests that should not be processed for what ever reason.
	for (int32 Index = 0; Index < Requests.Num(); ++Index)
	{
		OriginalToValidatedRequest[Index] = INDEX_NONE;

		FPushRequest& Request = Requests[Index];
		if (!Request.Identifier.IsValid() || Request.Payload.GetCompressedSize() == 0)
		{
			Request.Status = FPushRequest::EStatus::Invalid;
			continue;
		}

		if ((int64)Request.Payload.GetCompressedSize() < MinPayloadLength)
		{
			UE_LOG(	LogVirtualization, Verbose, TEXT("Attempting to push a virtualized payload (id: %s) that is smaller (%" UINT64_FMT ") than the MinPayloadLength (%" INT64_FMT ")"),
					*Request.Identifier.ToString(), 
					Request.Payload.GetCompressedSize(),
					MinPayloadLength);

			Request.Status = FPushRequest::EStatus::BelowMinSize;
			continue;
		}

		OriginalToValidatedRequest[Index] = ValidatedRequests.Num();
		ValidatedRequests.Add(Request);
	}
	
	// Filtering by package name is currently disabled
#if 0
	if (!ShouldVirtualizePackage(PackageContext))
	{
		UE_LOG(LogVirtualization, Verbose, TEXT("Payload '%s' for package '%s' will not be virtualized due to filtering"),
			*Id.ToString(), *PackageContext.GetDebugName());
		return false;
	}
#endif

	FConditionalScopeLock _(&ForceSingleThreadedCS, bForceSingleThreaded);

	// Early out if there are no backends or if the pushing of payloads has been disabled
	if (!IsEnabled() || bEnablePayloadPushing == false)
	{
		return false;
	}

	// TODO: Note that all push operations are currently synchronous, probably 
	// should change to async at some point, although this makes handling failed
	// pushed much more difficult.

	int32 ErrorCount = 0;
	bool bWasPayloadPushed = false;
	FBackendArray& Backends = StorageType == EStorageType::Local ? LocalCachableBackends : PersistentStorageBackends;

	for (IVirtualizationBackend* Backend : Backends)
	{
		const bool bResult = TryPushDataToBackend(*Backend, ValidatedRequests);

		UE_CLOG(bResult == true, LogVirtualization, Verbose, TEXT("[%s] Pushed '%d' payload(s)"), *Backend->GetDebugName(), ValidatedRequests.Num());
		UE_CLOG(bResult == false, LogVirtualization, Error, TEXT("[%s] Failed to push '%d' payload(s)"), *Backend->GetDebugName(), ValidatedRequests.Num());

		if (!bResult)
		{
			ErrorCount++;
		}
		
		// Debugging operation where we immediately try to pull the payload after each push (when possible) and assert 
		// that the pulled payload is the same as the original
		if (bValidateAfterPushOperation && bResult == true && Backend->SupportsPullOperations())
		{
			for (FPushRequest& Request : ValidatedRequests)
			{
				FCompressedBuffer ValidationPayload = PullDataFromBackend(*Backend, Request.Identifier);
				checkf(	Request.Payload.GetRawHash() == ValidationPayload.GetRawHash(),
						TEXT("[%s] Failed to pull payload '%s' after it was pushed to backend"),
						*Backend->GetDebugName(),
						*Request.Identifier.ToString());
			}
		}
	}

	UE_CLOG(ErrorCount == Backends.Num(), LogVirtualization, Error, TEXT("Failed to push '%d' payload(s) to any backend'"), ValidatedRequests.Num());

	// Now we need to update the statuses of the original list of requests with those from our validated list
	for (int32 Index = 0; Index < Requests.Num(); ++Index)
	{
		const int32 MappingIndex = OriginalToValidatedRequest[Index];
		if (MappingIndex != INDEX_NONE)
		{
			Requests[Index].Status = ValidatedRequests[MappingIndex].Status;
		}
	}

	// For local storage we consider the push to have failed only if ALL backends gave an error, if at least one backend succeeded then the operation succeeded.
	// For persistent storage we require that all backends succeeded, so any errors will fail the push operation.
	return StorageType == EStorageType::Local ? ErrorCount < Backends.Num() : ErrorCount == 0;
}

FCompressedBuffer FVirtualizationManager::PullData(const FPayloadId& Id)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FVirtualizationManager::PullData);

	if (!Id.IsValid())
	{
		// TODO: See below, should errors here be fatal?
		UE_LOG(LogVirtualization, Error, TEXT("Attempting to pull a virtualized payload with an invalid FPayloadId"));
		return FCompressedBuffer();
	}

	if (PullEnabledBackends.IsEmpty())
	{
		// TODO: See below, should errors here be fatal?
		UE_LOG(LogVirtualization, Error, TEXT("Payload '%s' failed to be pulled as there are no backends mounted!'"), *Id.ToString());
		return FCompressedBuffer();
	}

	FConditionalScopeLock _(&ForceSingleThreadedCS, bForceSingleThreaded);

	GetNotificationEvent().Broadcast(IVirtualizationSystem::PullBegunNotification, Id);

	for (IVirtualizationBackend* Backend : PullEnabledBackends)
	{
		FCompressedBuffer Payload = PullDataFromBackend(*Backend, Id);

		if (Payload)
		{
			if (bEnableCacheAfterPull)
			{
				CachePayload(Id, Payload, Backend);
			}

			GetNotificationEvent().Broadcast(IVirtualizationSystem::PullEndedNotification, Id);

			return Payload;
		}
	}

	// Pull ended but failed..
	GetNotificationEvent().Broadcast(IVirtualizationSystem::PullEndedNotification, Id);

	// Broadcast the pull failed event to any listeners
	GetNotificationEvent().Broadcast(IVirtualizationSystem::PullFailedNotification, Id);

	// TODO: Maybe this should be a fatal error? If we keep it as an error we need to make sure any calling
	// code handles it properly.
	// Could be worth extending ::PullData to return error codes instead so we can make a better distinction 
	// between the payload not being found in any of the backends and one or more of the backends failing.
	UE_LOG(LogVirtualization, Error, TEXT("Payload '%s' failed to be pulled from any backend'"), *Id.ToString());

	return FCompressedBuffer();
}

bool FVirtualizationManager::DoPayloadsExist(TArrayView<const FPayloadId> Ids, EStorageType StorageType, TArray<FPayloadStatus>& OutStatuses)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FVirtualizationManager::DoPayloadsExist);

	OutStatuses.SetNum(Ids.Num()); // Make sure we set the number out statuses before we potentially early out

	if (Ids.IsEmpty())
	{
		return true;
	}

	for (int32 Index = 0; Index < Ids.Num(); ++Index)
	{
		OutStatuses[Index] = Ids[Index].IsValid() ? FPayloadStatus::NotFound : FPayloadStatus::Invalid;
	}

	FBackendArray& Backends = StorageType == EStorageType::Local ? LocalCachableBackends : PersistentStorageBackends;

	TArray<int8> HitCount;
	TArray<bool> Results;

	HitCount.SetNum(Ids.Num());
	Results.SetNum(Ids.Num());

	{
		FConditionalScopeLock _(&ForceSingleThreadedCS, bForceSingleThreaded);

		for (IVirtualizationBackend* Backend : Backends)
		{
			if (!Backend->DoPayloadsExist(Ids, Results))
			{
				// If a backend entirely failed we should early out and report the problem
				OutStatuses.Reset();
				return false;
			}

			for (int32 Index = 0; Index < Ids.Num(); ++Index)
			{
				if (Ids[Index].IsValid() && Results[Index])
				{
					HitCount[Index]++;
				}
			}
		}
	}

	// Now we total up the hit count for each payload to see if it was found in none, all or some of the backends
	for (int32 Index = 0; Index < Ids.Num(); ++Index)
	{
		if (Ids[Index].IsValid())
		{
			if (HitCount[Index] == 0)
			{
				OutStatuses[Index] = FPayloadStatus::NotFound;
			}
			else if (HitCount[Index] == Backends.Num())
			{
				OutStatuses[Index] = FPayloadStatus::FoundAll;
			}
			else
			{
				OutStatuses[Index] = FPayloadStatus::Partial;;
			}
		}
	}

	return true;
}

FPayloadActivityInfo FVirtualizationManager::GetAccumualtedPayloadActivityInfo() const
{
	FPayloadActivityInfo Info;

#if ENABLE_COOK_STATS
	for (const auto& Iterator : Profiling::CacheStats)
	{
		Info.Cache.PayloadCount += Iterator.Value.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Counter);
		Info.Cache.TotalBytes += Iterator.Value.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Bytes);
		Info.Cache.CyclesSpent += Iterator.Value.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Cycles);
	}

	for (const auto& Iterator : Profiling::PushStats)
	{
		Info.Push.PayloadCount += Iterator.Value.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Counter);
		Info.Push.TotalBytes += Iterator.Value.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Bytes);
		Info.Push.CyclesSpent += Iterator.Value.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Cycles);
	}

	for (const auto& Iterator : Profiling::PullStats)
	{
		Info.Pull.PayloadCount += Iterator.Value.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Counter);
		Info.Pull.TotalBytes += Iterator.Value.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Bytes);
		Info.Pull.CyclesSpent += Iterator.Value.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Cycles);
	}
#endif // ENABLE_COOK_STATS

	return Info;
}

void FVirtualizationManager::GetPayloadActivityInfo( GetPayloadActivityInfoFuncRef GetPayloadFunc ) const
{
	FPayloadActivityInfo Info;

#if ENABLE_COOK_STATS

	for (const auto& Backend : AllBackends)
	{
		const FCookStats::CallStats& CacheStats = Profiling::GetCacheStats(*Backend);

		Info.Cache.PayloadCount = CacheStats.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Counter);
		Info.Cache.TotalBytes = CacheStats.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Bytes);
		Info.Cache.CyclesSpent = CacheStats.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Cycles);

		const FCookStats::CallStats& PushStats = Profiling::GetPushStats(*Backend);

		Info.Push.PayloadCount = PushStats.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Counter);
		Info.Push.TotalBytes = PushStats.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Bytes);
		Info.Push.CyclesSpent = PushStats.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Cycles);

		const FCookStats::CallStats& PullStats = Profiling::GetPullStats(*Backend);

		Info.Pull.PayloadCount = PullStats.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Counter);
		Info.Pull.TotalBytes = PullStats.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Bytes);
		Info.Pull.CyclesSpent = PullStats.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Cycles);

		GetPayloadFunc(Backend->GetDebugName(), Backend->GetConfigName(), Info);

	}
#endif // ENABLE_COOK_STATS

	
}

void FVirtualizationManager::ApplySettingsFromConfigFiles(const FConfigFile& PlatformEngineIni)
{
	UE_LOG(LogVirtualization, Log, TEXT("Loading virtualization manager settings from config files..."));
	
	bool bEnablePayloadPushingFromIni = false;
	if (PlatformEngineIni.GetBool(TEXT("Core.ContentVirtualization"), TEXT("EnablePushToBackend"), bEnablePayloadPushingFromIni))
	{
		bEnablePayloadPushing = bEnablePayloadPushingFromIni;
		UE_LOG(LogVirtualization, Log, TEXT("\tEnablePushToBackend : %s"), bEnablePayloadPushing ? TEXT("true") : TEXT("false") );
	}
	else
	{
		UE_LOG(LogVirtualization, Error, TEXT("Failed to load [Core.ContentVirtualization].EnablePushToBackend from config file!"));
	}

	bool bEnableCacheAfterPullFromIni = false;
	if (PlatformEngineIni.GetBool(TEXT("Core.ContentVirtualization"), TEXT("EnableCacheAfterPull"), bEnableCacheAfterPullFromIni))
	{
		bEnableCacheAfterPull = bEnableCacheAfterPullFromIni;
		UE_LOG(LogVirtualization, Log, TEXT("\tCachePulledPayloads : %s"), bEnableCacheAfterPull ? TEXT("true") : TEXT("false"));
	}
	else
	{
		UE_LOG(LogVirtualization, Error, TEXT("Failed to load [Core.ContentVirtualization].EnableCacheAfterPull from config file!"));
	}

	int64 MinPayloadLengthFromIni = 0;
	if (PlatformEngineIni.GetInt64(TEXT("Core.ContentVirtualization"), TEXT("MinPayloadLength"), MinPayloadLengthFromIni))
	{
		MinPayloadLength = MinPayloadLengthFromIni;
		UE_LOG(LogVirtualization, Log, TEXT("\tMinPayloadLength : %" INT64_FMT), MinPayloadLength );
	}
	else
	{
		UE_LOG(LogVirtualization, Error, TEXT("Failed to load [Core.ContentVirtualization].MinPayloadLength from config file!"));;
	}

	FString BackendGraphNameFromIni;
	if (PlatformEngineIni.GetString(TEXT("Core.ContentVirtualization"), TEXT("BackendGraph"), BackendGraphNameFromIni))
	{
		BackendGraphName = BackendGraphNameFromIni;
		UE_LOG(LogVirtualization, Log, TEXT("\tBackendGraphName : %s"), *BackendGraphName );
	}
	else
	{
		UE_LOG(LogVirtualization, Error, TEXT("Failed to load [Core.ContentVirtualization].BackendGraph from config file!"));;
	}

	bool bFilterEngineContentFromIni = true;
	if (PlatformEngineIni.GetBool(TEXT("Core.ContentVirtualization"), TEXT("FilterEngineContent"), bFilterEngineContentFromIni))
	{
		bFilterEngineContent = bFilterEngineContentFromIni;
		UE_LOG(LogVirtualization, Log, TEXT("\tFilterEngineContent : %s"), bFilterEngineContent ? TEXT("true") : TEXT("false"));
	}
	else
	{
		UE_LOG(LogVirtualization, Error, TEXT("Failed to load [Core.ContentVirtualization].FilterEngineContent from config file!"));
	}
	
	bool bFilterEnginePluginContentFromIni = true;
	if (PlatformEngineIni.GetBool(TEXT("Core.ContentVirtualization"), TEXT("FilterEnginePluginContent"), bFilterEnginePluginContentFromIni))
	{
		bFilterEnginePluginContent = bFilterEnginePluginContentFromIni;
		UE_LOG(LogVirtualization, Log, TEXT("\tFilterEnginePluginContent : %s"), bFilterEnginePluginContent ? TEXT("true") : TEXT("false"));
	}
	else
	{
		UE_LOG(LogVirtualization, Error, TEXT("Failed to load [Core.ContentVirtualization].FilterEnginePluginContent from config file!"));
	}	
}

void FVirtualizationManager::ApplySettingsFromCmdline()
{
	FString CmdlineGraphName;
	if (FParse::Value(FCommandLine::Get(), TEXT("-BackendGraph="), CmdlineGraphName))
	{
		UE_LOG(LogVirtualization, Log, TEXT("Backend graph overriden from the cmdline: '%s'"), *CmdlineGraphName);
		BackendGraphName = CmdlineGraphName;
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("VirtualizationForceSingleThreaded")))
	{
		bForceSingleThreaded = true;
		UE_LOG(LogVirtualization, Log, TEXT("ForceSingleThreaded overriden from the cmdline: true"));	
	}
}

void FVirtualizationManager::ApplyDebugSettingsFromConfigFiles(const FConfigFile& PlatformEngineIni)
{
	UE_LOG(LogVirtualization, Log, TEXT("Loading virtualization manager debugging settings from config files..."));

	// Note that the debug settings are optional and could be left out of the config files entirely
	bool bForceSingleThreadedFromIni = false;
	if (PlatformEngineIni.GetBool(TEXT("Core.ContentVirtualizationDebugOptions"), TEXT("ForceSingleThreaded"), bForceSingleThreadedFromIni))
	{
		bForceSingleThreaded = bForceSingleThreadedFromIni;
		UE_LOG(LogVirtualization, Log, TEXT("\tForceSingleThreaded : %s"), bForceSingleThreaded ? TEXT("true") : TEXT("false"));
	}

	bool bValidateAfterPushOperationFromIni = false;
	if (PlatformEngineIni.GetBool(TEXT("Core.ContentVirtualizationDebugOptions"), TEXT("ValidateAfterPushOperation"), bValidateAfterPushOperationFromIni))
	{
		bValidateAfterPushOperation = bValidateAfterPushOperationFromIni;
		UE_LOG(LogVirtualization, Log, TEXT("ValidateAfterPushOperation : %s"), bValidateAfterPushOperation ? TEXT("true") : TEXT("false"));
	}

	// Some debug options will cause intentional breaks or slow downs for testing purposes, if these are enabled then we should give warning/errors 
	// so it is clear in the log that future failures are being caused by the given dev option.
	UE_CLOG(bForceSingleThreaded, LogVirtualization, Warning, TEXT("ForceSingleThreaded is enabled, virtualization will run in single threaded mode and may be slower!"));
	UE_CLOG(bValidateAfterPushOperation, LogVirtualization, Error, TEXT("ValidateAfterPushOperation is enabled, each push will be followed by a pull to validate it!"));
}

void FVirtualizationManager::ApplyDebugSettingsFromFromCmdline()
{
	FString MissOptions;
	if (FParse::Value(FCommandLine::Get(), TEXT("-VA-MissBackends="), MissOptions))
	{
		MissOptions.ParseIntoArray(BackendsToDisablePulls, TEXT("+"), true);

		UE_LOG(LogVirtualization, Warning, TEXT("Cmdline has disabled payload pulling for the following backends:"));
		for (const FString& Backend : BackendsToDisablePulls)
		{
			UE_LOG(LogVirtualization, Warning, TEXT("\t%s"), *Backend);
		}
	}
}

void FVirtualizationManager::MountBackends()
{
	UE_LOG(LogVirtualization, Log, TEXT("Mounting virtualization backends..."));

	const FRegistedFactories FactoryLookupTable = FindBackendFactories();
	UE_LOG(LogVirtualization, Verbose, TEXT("Found %d backend factories"), FactoryLookupTable.Num());

	const TCHAR* GraphName = *BackendGraphName;

	if(!GConfig->DoesSectionExist(GraphName, GEngineIni))
	{
		UE_LOG(LogVirtualization, Fatal, TEXT("Unable to find the backend graph: '%s' [ini=%s]."), GraphName, *GEngineIni);
	}

	UE_LOG(LogVirtualization, Log, TEXT("Using backend graph: '%s'"), GraphName);

	// It is important to parse the local storage hierarchy first so those backends will show up before the
	// persistent storage backends in 'PullEnabledBackends'.
	ParseHierarchy(GraphName, TEXT("LocalStorageHierarchy"), FactoryLookupTable, LocalCachableBackends);
	ParseHierarchy(GraphName, TEXT("PersistentStorageHierarchy"), FactoryLookupTable, PersistentStorageBackends);
}

void FVirtualizationManager::ParseHierarchy(const TCHAR* GraphName, const TCHAR* HierarchyKey, const FRegistedFactories& FactoryLookupTable, FBackendArray& PushArray)
{
	FString HierarchyData;
	if (!GConfig->GetString(GraphName, HierarchyKey, HierarchyData, GEngineIni))
	{
		UE_LOG(LogVirtualization, Fatal, TEXT("Unable to find the '%s' entry for the content virtualization backend graph '%s' [ini=%s]."), HierarchyKey, GraphName, *GEngineIni);
	}

	if (HierarchyData.IsEmpty())
	{
		UE_LOG(LogVirtualization, Fatal, TEXT("The '%s' entry for backend graph '%s' is empty [ini=%s]."), HierarchyKey, GraphName, *GEngineIni);
	}

	TArray<FString> Entries = ParseEntries(HierarchyData);

	UE_LOG(LogVirtualization, Log, TEXT("The backend graph hierarchy '%s' has %d entries"), HierarchyKey, Entries.Num());

	for (const FString& Entry : Entries)
	{
		CreateBackend(GraphName, Entry, FactoryLookupTable, PushArray);
	}
}

bool FVirtualizationManager::CreateBackend(const TCHAR* GraphName, const FString& ConfigEntryName, const FRegistedFactories& FactoryLookupTable, FBackendArray& PushArray)
{
	// All failures in this method are considered fatal, however it still returns true/false in case we decide
	// to be more forgiving in the future.
	UE_LOG(LogVirtualization, Log, TEXT("Attempting to create back end entry '%s'"), *ConfigEntryName);

	FString BackendData;
	if (!GConfig->GetString(GraphName, *ConfigEntryName, BackendData, GEngineIni))
	{
		UE_LOG(LogVirtualization, Fatal, TEXT("Unable to find the entry '%s' in the content virtualization backend graph '%s' [ini=%s]."), *ConfigEntryName, GraphName, *GEngineIni);
		return false;
	}

	FString BackendType;
	if (FParse::Value(*BackendData, TEXT("Type="), BackendType) && !BackendType.IsEmpty())
	{
		// Put the rest of the ini file entry into a string to pass to the backend.
		FString Cmdine = BackendData.RightChop(BackendData.Find(BackendType) + BackendType.Len());
		Cmdine.RemoveFromEnd(TEXT(")"));

		UE::Virtualization::IVirtualizationBackendFactory* const* FactoryPtr = FactoryLookupTable.Find(FName(BackendType));
		if (FactoryPtr != nullptr && *FactoryPtr != nullptr)
		{
			IVirtualizationBackendFactory* Factory = *FactoryPtr;
			TUniquePtr<IVirtualizationBackend> Backend = Factory->CreateInstance(ConfigEntryName);

			if (Backend == nullptr)
			{
				UE_LOG(LogVirtualization, Fatal, TEXT("IVirtualizationBackendFactory '%s' failed to create an instance!"), *Factory->GetName().ToString());
				return false;

			}

			if (BackendsToDisablePulls.Find(Backend->GetConfigName()) != INDEX_NONE  || BackendsToDisablePulls.Find(TEXT("All")) != INDEX_NONE)
			{
				Backend->DisablePullOperationSupport();
			}
			
			if (Backend->Initialize(Cmdine))
			{
				AddBackend(MoveTemp(Backend), PushArray);
			}
			else
			{
				UE_LOG(LogVirtualization, Fatal, TEXT("Backend '%s' reported errors when initializing"), *ConfigEntryName);
				return false;
			}
		}
		else
		{
			UE_LOG(LogVirtualization, Fatal, TEXT("No backend factory found that can create the type '%s'"), *BackendType);
			return false;
		}
	}
	else
	{
		UE_LOG(LogVirtualization, Fatal, TEXT("No 'Type=' entry found for '%s' in the config file"), *ConfigEntryName);
		return false;
	}

	return true;
}

void FVirtualizationManager::AddBackend(TUniquePtr<IVirtualizationBackend> Backend, FBackendArray& PushArray)
{
	checkf(!AllBackends.Contains(Backend), TEXT("Adding the same virtualization backend (%s) multiple times!"), *Backend->GetDebugName());

	// Move ownership of the backend to AllBackends
	AllBackends.Add(MoveTemp(Backend));

	// Get a reference pointer to use in the other backend arrays
	IVirtualizationBackend* BackendRef = AllBackends.Last().Get();

	if (BackendRef->SupportsPullOperations())
	{
		PullEnabledBackends.Add(BackendRef);
	}

	if (BackendRef->SupportsPushOperations())
	{
		PushArray.Add(BackendRef);
	}

	COOK_STAT(Profiling::CreateStats(*BackendRef));

	UE_LOG(LogVirtualization, Log, TEXT("Mounted backend: %s"), *BackendRef->GetDebugName());
}

void FVirtualizationManager::CachePayload(const FPayloadId& Id, const FCompressedBuffer& Payload, const IVirtualizationBackend* BackendSource)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FVirtualizationManager::CachePayload);

	// We start caching at the first (assumed to be fastest) local cache backend. 
	for (IVirtualizationBackend* BackendToCache : LocalCachableBackends)
	{
		if (BackendToCache == BackendSource)
		{
			return; // No point going past BackendSource
		}

		bool bResult = TryCacheDataToBackend(*BackendToCache, Id, Payload);
		UE_CLOG(	!bResult, LogVirtualization, Warning,
					TEXT("Failed to cache payload '%s' to backend '%s'"),
					*Id.ToString(),
					*BackendToCache->GetDebugName());

		// Debugging operation where we immediately try to pull the payload after each push (when possible) and assert 
		// that the pulled payload is the same as the original
		if (bValidateAfterPushOperation && bResult && BackendToCache->SupportsPullOperations())
		{
			FCompressedBuffer PulledPayload = PullDataFromBackend(*BackendToCache, Id);
			checkf(	Payload.GetRawHash() == PulledPayload.GetRawHash(), 
					TEXT("[%s] Failed to pull payload '%s' after it was cached to backend"),
					*BackendToCache->GetDebugName(), 
					*Id.ToString());
		}
	}
}

bool FVirtualizationManager::TryCacheDataToBackend(IVirtualizationBackend& Backend, const FPayloadId& Id, const FCompressedBuffer& Payload)
{
	COOK_STAT(FCookStats::FScopedStatsCounter Timer(Profiling::GetCacheStats(Backend)));
	const EPushResult Result = Backend.PushData(Id, Payload, FString());

	if (Result == EPushResult::Success)
	{
		COOK_STAT(Timer.AddHit(Payload.GetCompressedSize()));
	}

	return Result != EPushResult::Failed;
}

bool FVirtualizationManager::TryPushDataToBackend(IVirtualizationBackend& Backend, TArrayView<FPushRequest> Requests)
{
	COOK_STAT(FCookStats::FScopedStatsCounter Timer(Profiling::GetPushStats(Backend)));
	const bool bResult = Backend.PushData(Requests);

	if (bResult)
	{
		for (FPushRequest& Request : Requests)
		{
			// TODO: Don't add a hit if the payload was already uploaded
			if (Request.Status == FPushRequest::EStatus::Success)
			{
				COOK_STAT(Timer.AddHit(Request.Payload.GetCompressedSize()));
			}
		}
	}
	
	return bResult;
}

FCompressedBuffer FVirtualizationManager::PullDataFromBackend(IVirtualizationBackend& Backend, const FPayloadId& Id)
{
	COOK_STAT(FCookStats::FScopedStatsCounter Timer(Profiling::GetPullStats(Backend)));
	FCompressedBuffer Payload = Backend.PullData(Id);
	
	if (!Payload.IsNull())
	{
		COOK_STAT(Timer.AddHit(Payload.GetCompressedSize()));
	}
	
	return Payload;
}

bool FVirtualizationManager::ShouldVirtualizePackage(const FPackagePath& PackagePath) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FVirtualizationManager::ShouldVirtualizePackage);

	// We require a valid mounted path for filtering
	if (!PackagePath.IsMountedPath())
	{
		return true;
	}

	TStringBuilder<256> PackageName;
	PackagePath.AppendPackageName(PackageName);

	TStringBuilder<64> MountPointName;
	TStringBuilder<256> MountPointPath;
	TStringBuilder<256> RelativePath;

	if (!FPackageName::TryGetMountPointForPath(PackageName, MountPointName, MountPointPath, RelativePath))
	{
		return true;
	}

	if (bFilterEngineContent)
	{
		// Do not virtualize engine content
		if (MountPointName.ToView() == TEXT("/Engine/"))
		{
			return false;
		}
	}

	if (bFilterEnginePluginContent)
	{
		// Do not virtualize engine plugin content
		if (FPaths::IsUnderDirectory(MountPointPath.ToString(), FPaths::EnginePluginsDir()))
		{
			return false;
		}
	}

	const UVirtualizationFilterSettings* Settings = GetDefault<UVirtualizationFilterSettings>();
	if (Settings != nullptr)
	{
		const FStringView PackageNameView = PackageName.ToView();

		for (const FString& Exclusion : Settings->ExcludePackagePaths)
		{
			if (Exclusion.EndsWith(TEXT("/")))
			{
				// Directory path, exclude everything under it
				if (PackageNameView.StartsWith(Exclusion))
				{
					return false;
				}
			}
			else
			{
				// Path to an asset, exclude if it matches exactly
				if (PackageNameView == Exclusion)
				{
					return false;
				}
			}			
		}
	}
	
	return true;
}

} // namespace UE::Virtualization

// Copyright Epic Games, Inc. All Rights Reserved.

#include "VirtualizationManager.h"

#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "IVirtualizationBackend.h"
#include "Logging/MessageLog.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/PackageName.h"
#include "Misc/PackagePath.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "PackageRehydrationProcess.h"
#include "Misc/ScopedSlowTask.h"
#include "PackageVirtualizationProcess.h"
#include "ProfilingDebugging/CookStats.h"
#include "VirtualizationFilterSettings.h"

#define LOCTEXT_NAMESPACE "Virtualization"

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

bool LexTryParseString(EPackageFilterMode& OutValue, FStringView Buffer)
{
	if (Buffer == TEXT("OptOut"))
	{
		OutValue = EPackageFilterMode::OptOut;
		return true;
	}
	else if (Buffer == TEXT("OptIn"))
	{
		OutValue = EPackageFilterMode::OptIn;
		return true;
	}

	return false;
}

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

		UE_LOG(LogVirtualization, Display, TEXT(""));
		UE_LOG(LogVirtualization, Display, TEXT("Virtualization ProfileData"));
		UE_LOG(LogVirtualization, Display, TEXT("======================================================================================="));

		if (CacheStats.Num() > 0)
		{
			UE_LOG(LogVirtualization, Display, TEXT("%-40s|%17s|%12s|%14s|"), TEXT("Caching Data"), TEXT("TotalSize (MB)"), TEXT("TotalTime(s)"), TEXT("DataRate(MB/S)"));
			UE_LOG(LogVirtualization, Display, TEXT("----------------------------------------|-----------------|------------|--------------|"));

			for (const auto& Iterator : CacheStats)
			{
				const double Time = Iterator.Value.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Cycles) * FPlatformTime::GetSecondsPerCycle();
				const double DataSizeMB = Iterator.Value.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Bytes) / (1024.0f * 1024.0f);
				const double MBps = Time != 0.0 ? (DataSizeMB / Time) : 0.0;

				UE_LOG(LogVirtualization, Display, TEXT("%-40.40s|%17.1f|%12.3f|%14.3f|"),
					*Iterator.Key,
					DataSizeMB,
					Time,
					MBps);
			}

			UE_LOG(LogVirtualization, Display, TEXT("======================================================================================="));
		}

		if (PushStats.Num() > 0)
		{
			UE_LOG(LogVirtualization, Display, TEXT("%-40s|%17s|%12s|%14s|"), TEXT("Pushing Data"), TEXT("TotalSize (MB)"), TEXT("TotalTime(s)"), TEXT("DataRate(MB/S)"));
			UE_LOG(LogVirtualization, Display, TEXT("----------------------------------------|-----------------|------------|--------------|"));

			for (const auto& Iterator : PushStats)
			{
				const double Time = Iterator.Value.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Cycles) * FPlatformTime::GetSecondsPerCycle();
				const double DataSizeMB = Iterator.Value.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Bytes) / (1024.0f * 1024.0f);
				const double MBps = Time != 0.0 ? (DataSizeMB / Time) : 0.0;

				UE_LOG(LogVirtualization, Display, TEXT("%-40.40s|%17.1f|%12.3f|%14.3f|"),
					*Iterator.Key,
					DataSizeMB,
					Time,
					MBps);
			}

			UE_LOG(LogVirtualization, Display, TEXT("======================================================================================="));
		}

		if (PullStats.Num() > 0)
		{
			UE_LOG(LogVirtualization, Display, TEXT("%-40s|%17s|%12s|%14s|"), TEXT("Pulling Data"), TEXT("TotalSize (MB)"), TEXT("TotalTime(s)"), TEXT("DataRate(MB/S)"));
			UE_LOG(LogVirtualization, Display, TEXT("----------------------------------------|-----------------|------------|--------------|"));

			for (const auto& Iterator : PullStats)
			{
				const double Time = Iterator.Value.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Cycles) * FPlatformTime::GetSecondsPerCycle();
				const double DataSizeMB = Iterator.Value.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Bytes) / (1024.0f * 1024.0f);
				const double MBps = Time != 0.0 ? (DataSizeMB / Time) : 0.0;

				UE_LOG(LogVirtualization, Display, TEXT("%-40.40s|%17.1f|%12.3f|%14.3f|"),
					*Iterator.Key,
					DataSizeMB,
					Time,
					MBps);
			}

			UE_LOG(LogVirtualization, Display, TEXT("======================================================================================="));
		}
	}
#endif // ENABLE_COOK_STATS
} //namespace Profiling

FVirtualizationManager::FVirtualizationManager()
	: bEnablePayloadVirtualization(true)
	, bEnableCacheAfterPull(true)
	, MinPayloadLength(0)
	, BackendGraphName(TEXT("ContentVirtualizationBackendGraph_None"))
	, VirtualizationProcessTag(TEXT("#virtualized"))
	, FilteringMode(EPackageFilterMode::OptOut)
	, bFilterEngineContent(true)
	, bFilterEnginePluginContent(true)
	, bFilterMapContent(true)
	, bAllowSubmitIfVirtualizationFailed(false)
{
}

FVirtualizationManager::~FVirtualizationManager()
{
	for (IConsoleObject* ConsoleObject : DebugValues.ConsoleObjects)
	{
		IConsoleManager::Get().UnregisterConsoleObject(ConsoleObject);
	}

	UE_LOG(LogVirtualization, Log, TEXT("Destroying backends"));

	LocalCachableBackends.Empty();
	PersistentStorageBackends.Empty();
	PullEnabledBackends.Empty();

	AllBackends.Empty(); // This will delete all backends and beyond this point all references to them are invalid

	UE_LOG(LogVirtualization, Log, TEXT("Virtualization manager destroyed"));
}

#if ENABLE_FILTERING_HACK
void FVirtualizationManager::FilterRequests(TArray<FPushRequest>& Requests) const
{
	// The same filtering code as in FVirtualizationManager::PushData, since this is a hack
	// I do not want to make any changes to real code paths so it was safer to just duplicate
	// the checks that we need.
	for (FPushRequest& Request : Requests)
	{
		if (Request.GetIdentifier().IsZero() || Request.GetPayloadSize() == 0)
		{
			Request.SetStatus(FPushRequest::EStatus::Invalid);
			continue;
		}

		if ((int64)Request.GetPayloadSize() < MinPayloadLength)
		{
			Request.SetStatus(FPushRequest::EStatus::BelowMinSize);
			continue;
		}

		if (!ShouldVirtualize(Request.GetContext()))
		{
			Request.SetStatus(FPushRequest::EStatus::ExcludedByPackagPath);
			continue;
		}

		Request.SetStatus(FPushRequest::EStatus::Success);
	}
}
#endif //ENABLE_FILTERING_HACK

bool FVirtualizationManager::Initialize(const FInitParams& InitParams)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FVirtualizationManager::Initialize);

	// TODO: Ideally we'd break this down further, or at least have a FScopedSlowTask for each
	// backend initialization but the slow task system will only update the UI every 0.2 seconds
	// so if we have too many small tasks we might show misleading data to the user, so it is 
	// better for us to have a single scope here at the top level and rely on UnrealInsights for
	// detailed profiling unless we do something to how FScopedSlowTask updates the UI.
	FScopedSlowTask SlowTask(1.0f, LOCTEXT("VAInitialize", "Initializing virtualized asset system..."));
	SlowTask.EnterProgressFrame(1.0f);

	ProjectName = InitParams.ProjectName;

	ApplySettingsFromConfigFiles(InitParams.ConfigFile);
	ApplyDebugSettingsFromFromCmdline();

	// Do this after all of the command line settings have been processed and any 
	// requested debug value changes already set.
	RegisterConsoleCommands();

	MountBackends(InitParams.ConfigFile);

	return true;
}

bool FVirtualizationManager::IsEnabled() const
{
	return !AllBackends.IsEmpty();
}

bool FVirtualizationManager::IsPushingEnabled(EStorageType StorageType) const
{
	if (!bEnablePayloadVirtualization)
	{
		return false;
	}

	switch (StorageType)
	{
		case EStorageType::Local:
			return !LocalCachableBackends.IsEmpty();
			break;

		case EStorageType::Persistent:
			return !PersistentStorageBackends.IsEmpty();
			break;

		default:
			checkNoEntry();
			return false;
			break;
	}
}

EPayloadFilterReason FVirtualizationManager::FilterPayload(const UObject* Owner) const
{
	UE::Virtualization::EPayloadFilterReason PayloadFilter = UE::Virtualization::EPayloadFilterReason::None;
	if (!ShouldVirtualizeAsset(Owner))
	{
		PayloadFilter |= UE::Virtualization::EPayloadFilterReason::Asset;
	}

	// TODO: If we keep this feature long term then we might want to work this out in SavePackage.cpp and pass the info
	// via FLinkerSave rather than the following code.
	if (bFilterMapContent)
	{
		if (const UObject* Outer = Owner->GetOutermostObject())
		{
			if (const UClass* OuterClass = Outer->GetClass())
			{
				const FName OuterClassName = OuterClass->GetFName();
				if (OuterClassName == FName("Level") ||
					OuterClassName == FName("World") ||
					OuterClassName == FName("MapBuildDataRegistry"))
				{
					PayloadFilter |= UE::Virtualization::EPayloadFilterReason::MapContent;
				}
			}
		}
	}

	return PayloadFilter;
}

bool FVirtualizationManager::AllowSubmitIfVirtualizationFailed() const
{
	return bAllowSubmitIfVirtualizationFailed;
}

bool FVirtualizationManager::PushData(const FIoHash& Id, const FCompressedBuffer& Payload, EStorageType StorageType, const FString& Context)
{
	FPushRequest Request(Id, Payload, Context);
	return FVirtualizationManager::PushData(MakeArrayView(&Request, 1), StorageType);
}

bool FVirtualizationManager::PushData(TArrayView<FPushRequest> Requests, EStorageType StorageType)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FVirtualizationManager::PushData);

	if (Requests.IsEmpty())
	{
		return true;
	}

	TArray<FPushRequest> ValidatedRequests;
	ValidatedRequests.Reserve(Requests.Num());

	TArray<int32> OriginalToValidatedRequest; // Builds a mapping between Requests and ValidatedRequests for later
	OriginalToValidatedRequest.SetNum(Requests.Num());

	// Create a new list of FPushRequest, excluding the requests that should not be processed for what ever reason.
	for (int32 Index = 0; Index < Requests.Num(); ++Index)
	{
		OriginalToValidatedRequest[Index] = INDEX_NONE;

		FPushRequest& Request = Requests[Index];
		if (Request.GetIdentifier().IsZero() || Request.GetPayloadSize() == 0)
		{
			Request.SetStatus(FPushRequest::EStatus::Invalid);
			continue;
		}

		if ((int64)Request.GetPayloadSize() < MinPayloadLength)
		{
			UE_LOG(	LogVirtualization, Verbose, TEXT("Pushing payload (id: %s) with context ('%s') was prevented as it is smaller (%" UINT64_FMT ") than the MinPayloadLength (%" INT64_FMT ")"),
					*LexToString(Request.GetIdentifier()),
					*Request.GetContext(),
					Request.GetPayloadSize(),
					MinPayloadLength);

			Request.SetStatus(FPushRequest::EStatus::BelowMinSize);
			continue;
		}

		if (!ShouldVirtualize(Request.GetContext()))
		{
			UE_LOG(	LogVirtualization, Verbose, TEXT("Pushing payload (id: %s) with context ('%s') was prevented by filtering"),
					*LexToString(Request.GetIdentifier()), 
					*Request.GetContext());
			
			Request.SetStatus(FPushRequest::EStatus::ExcludedByPackagPath);
			continue;
		}

		OriginalToValidatedRequest[Index] = ValidatedRequests.Num();
		ValidatedRequests.Add(Request);
	}

	// Early out if none of the requests require pushing after validation
	if (ValidatedRequests.IsEmpty())
	{
		return true;
	}

	// Early out if there are no backends
	if (!IsEnabled() || bEnablePayloadVirtualization == false)
	{
		return false;
	}

	FConditionalScopeLock _(&DebugValues.ForceSingleThreadedCS, DebugValues.bSingleThreaded);

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
		
		// Debug operation to validate that the payload we just pushed can be retrieved from storage
		if (DebugValues.bValidateAfterPush && bResult == true && Backend->IsOperationSupported(IVirtualizationBackend::EOperations::Pull))
		{
			for (FPushRequest& Request : ValidatedRequests)
			{
				FCompressedBuffer ValidationPayload = PullDataFromBackend(*Backend, Request.GetIdentifier());
				checkf(	Request.GetIdentifier() == ValidationPayload.GetRawHash(),
						TEXT("[%s] Failed to pull payload '%s' after it was pushed to backend"),
						*Backend->GetDebugName(),
						*LexToString(Request.GetIdentifier()));
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
			Requests[Index].SetStatus(ValidatedRequests[MappingIndex].GetStatus());
		}
	}

	// For local storage we consider the push to have failed only if ALL backends gave an error, if at least one backend succeeded then the operation succeeded.
	// For persistent storage we require that all backends succeeded, so any errors will fail the push operation.
	return StorageType == EStorageType::Local ? ErrorCount < Backends.Num() : ErrorCount == 0;
}

FCompressedBuffer FVirtualizationManager::PullData(const FIoHash& Id)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FVirtualizationManager::PullData);

	if (Id.IsZero())
	{
		// TODO: See below, should errors here be fatal?
		UE_LOG(LogVirtualization, Error, TEXT("Attempting to pull a virtualized payload with an invalid FIoHash"));
		return FCompressedBuffer();
	}

	if (PullEnabledBackends.IsEmpty())
	{
		// TODO: See below, should errors here be fatal?
		UE_LOG(LogVirtualization, Error, TEXT("Payload '%s' failed to be pulled as there are no backends mounted!'"), *LexToString(Id));
		return FCompressedBuffer();
	}

	FConditionalScopeLock _(&DebugValues.ForceSingleThreadedCS, DebugValues.bSingleThreaded);

	GetNotificationEvent().Broadcast(IVirtualizationSystem::PullBegunNotification, Id);

	FCompressedBuffer Payload = PullDataFromAllBackends(Id);
	
	GetNotificationEvent().Broadcast(IVirtualizationSystem::PullEndedNotification, Id);

	if (!Payload.IsNull())
	{
		return Payload;
	}
	else
	{
		// Broadcast the pull failed event to any listeners
		GetNotificationEvent().Broadcast(IVirtualizationSystem::PullFailedNotification, Id);

		// TODO: Maybe this should be a fatal error? If we keep it as an error we need to make sure any calling
		// code handles it properly.
		// Could be worth extending ::PullData to return error codes instead so we can make a better distinction 
		// between the payload not being found in any of the backends and one or more of the backends failing.
		UE_LOG(LogVirtualization, Error, TEXT("Payload '%s' failed to be pulled from any backend'"), *LexToString(Id));

		return FCompressedBuffer();
	}
}

EQueryResult FVirtualizationManager::QueryPayloadStatuses(TArrayView<const FIoHash> Ids, EStorageType StorageType, TArray<EPayloadStatus>& OutStatuses)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FVirtualizationManager::DoPayloadsExist);

	OutStatuses.SetNum(Ids.Num()); // Make sure we set the number out statuses before we potentially early out

	if (Ids.IsEmpty())
	{
		return EQueryResult::Success;
	}

	for (int32 Index = 0; Index < Ids.Num(); ++Index)
	{
		OutStatuses[Index] = Ids[Index].IsZero() ? EPayloadStatus::Invalid : EPayloadStatus::NotFound;
	}

	FBackendArray& Backends = StorageType == EStorageType::Local ? LocalCachableBackends : PersistentStorageBackends;

	TArray<int8> HitCount;
	TArray<bool> Results;

	HitCount.SetNum(Ids.Num());
	Results.SetNum(Ids.Num());

	{
		FConditionalScopeLock _(&DebugValues.ForceSingleThreadedCS, DebugValues.bSingleThreaded);

		for (IVirtualizationBackend* Backend : Backends)
		{
			if (!Backend->DoPayloadsExist(Ids, Results))
			{
				// If a backend entirely failed we should early out and report the problem
				OutStatuses.Reset();
				return EQueryResult::Failure_Unknown;
			}

			for (int32 Index = 0; Index < Ids.Num(); ++Index)
			{
				if (!Ids[Index].IsZero() && Results[Index])
				{
					HitCount[Index]++;
				}
			}
		}
	}

	// Now we total up the hit count for each payload to see if it was found in none, all or some of the backends
	for (int32 Index = 0; Index < Ids.Num(); ++Index)
	{
		if (!Ids[Index].IsZero())
		{
			if (HitCount[Index] == 0)
			{
				OutStatuses[Index] = EPayloadStatus::NotFound;
			}
			else if (HitCount[Index] == Backends.Num())
			{
				OutStatuses[Index] = EPayloadStatus::FoundAll;
			}
			else
			{
				OutStatuses[Index] = EPayloadStatus::FoundPartial;
			}
		}
	}

	return EQueryResult::Success;
}

EVirtualizationResult FVirtualizationManager::TryVirtualizePackages(const TArray<FString>& FilesToVirtualize, TArray<FText>& OutDescriptionTags, TArray<FText>& OutErrors)
{
	OutDescriptionTags.Reset();
	OutErrors.Reset();

	UE::Virtualization::VirtualizePackages(FilesToVirtualize, OutErrors);

	// If we had no new errors add the validation tag to indicate that the packages are safe for submission. 
	// TODO: Currently this is a simple tag to make it easier for us to track which assets were submitted via the
	// virtualization process in a test project. This should be expanded when we add proper p4 server triggers.
	if (OutErrors.IsEmpty() && !VirtualizationProcessTag.IsEmpty())
	{
		FText Tag = FText::FromString(VirtualizationProcessTag);
		OutDescriptionTags.Add(Tag);
	}

	return OutErrors.IsEmpty() ? EVirtualizationResult::Success : EVirtualizationResult::Failed;
}

ERehydrationResult FVirtualizationManager::TryRehydratePackages(const TArray<FString>& Packages, TArray<FText>& OutErrors)
{
	OutErrors.Reset();

	UE::Virtualization::RehydratePackages(Packages, OutErrors);

	return OutErrors.IsEmpty() ? ERehydrationResult::Success : ERehydrationResult::Failed;
}

void FVirtualizationManager::DumpStats() const
{
#if ENABLE_COOK_STATS
	Profiling::LogStats();
#endif // ENABLE_COOK_STATS
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

void FVirtualizationManager::ApplySettingsFromConfigFiles(const FConfigFile& ConfigFile)
{
	UE_LOG(LogVirtualization, Display, TEXT("Loading virtualization manager settings from config files..."));

	const TCHAR* LegacyConfigSection = TEXT("Core.ContentVirtualization");
	const TCHAR* ConfigSection = TEXT("Core.VirtualizationModule");

	// Note that all options are doubled up as we are moving the options for this module from "Core.ContentVirtualization"
	// to it's own specific "Core.VirtualizationModule" section. This duplication can be removed before we ship 5.1
	
	{
		// This value was moved from Core.ContentVirtualization to Core.VirtualizationModule then renamed from
		// 'EnablePushToBackend' to 'EnablePayloadVirtualization' so there are a few paths we need to cover here.
		// This can also be cleaned up for 5.1 shipping.
		bool bLoadedFromFile = false;
		bool bEnablePayloadVirtualizationFromIni = false;
		
		if (ConfigFile.GetBool(LegacyConfigSection, TEXT("EnablePushToBackend"), bEnablePayloadVirtualizationFromIni))
		{
			UE_LOG(LogVirtualization, Warning, TEXT("\tFound legacy ini file setting [Core.ContentVirtualization].EnablePushToBackend, rename to [Core.VirtualizationModule].EnablePayloadVirtualization"));
			bLoadedFromFile = true;
		}
		else if (ConfigFile.GetBool(ConfigSection, TEXT("EnablePushToBackend"), bEnablePayloadVirtualizationFromIni))
		{
			UE_LOG(LogVirtualization, Warning, TEXT("\tFound legacy ini file setting [Core.VirtualizationModule].EnablePushToBackend, rename to [Core.VirtualizationModule].EnablePayloadVirtualization"));
			bLoadedFromFile = true;
		}
		else if (ConfigFile.GetBool(ConfigSection, TEXT("EnablePayloadVirtualization"), bEnablePayloadVirtualizationFromIni))
		{
			bLoadedFromFile = true;
		}

		if (bLoadedFromFile)
		{
			bEnablePayloadVirtualization = bEnablePayloadVirtualizationFromIni;
			UE_LOG(LogVirtualization, Display, TEXT("\tEnablePayloadVirtualization : %s"), bEnablePayloadVirtualization ? TEXT("true") : TEXT("false"));
		}
		else
		{
			UE_LOG(LogVirtualization, Error, TEXT("Failed to load [Core.VirtualizationModule].EnablePayloadVirtualization from config file!"));
		}
	}

	bool bEnableCacheAfterPullFromIni = false;
	if (ConfigFile.GetBool(LegacyConfigSection, TEXT("EnableCacheAfterPull"), bEnableCacheAfterPullFromIni) ||
		ConfigFile.GetBool(ConfigSection, TEXT("EnableCacheAfterPull"), bEnableCacheAfterPullFromIni))
	{
		bEnableCacheAfterPull = bEnableCacheAfterPullFromIni;
		UE_LOG(LogVirtualization, Display, TEXT("\tCachePulledPayloads : %s"), bEnableCacheAfterPull ? TEXT("true") : TEXT("false"));
	}
	else
	{
		UE_LOG(LogVirtualization, Error, TEXT("Failed to load [Core.VirtualizationModule].EnableCacheAfterPull from config file!"));
	}

	int64 MinPayloadLengthFromIni = 0;
	if (ConfigFile.GetInt64(LegacyConfigSection, TEXT("MinPayloadLength"), MinPayloadLengthFromIni) ||
		ConfigFile.GetInt64(ConfigSection, TEXT("MinPayloadLength"), MinPayloadLengthFromIni))
	{
		MinPayloadLength = MinPayloadLengthFromIni;
		UE_LOG(LogVirtualization, Display, TEXT("\tMinPayloadLength : %" INT64_FMT), MinPayloadLength );
	}
	else
	{
		UE_LOG(LogVirtualization, Error, TEXT("Failed to load [Core.VirtualizationModule].MinPayloadLength from config file!"));
	}

	FString BackendGraphNameFromIni;
	if (ConfigFile.GetString(LegacyConfigSection, TEXT("BackendGraph"), BackendGraphNameFromIni) ||
		ConfigFile.GetString(ConfigSection, TEXT("BackendGraph"), BackendGraphNameFromIni))
	{
		BackendGraphName = BackendGraphNameFromIni;
		UE_LOG(LogVirtualization, Display, TEXT("\tBackendGraphName : %s"), *BackendGraphName );
	}
	else
	{
		UE_LOG(LogVirtualization, Error, TEXT("Failed to load [Core.VirtualizationModule].BackendGraph from config file!"));
	}

	FString VirtualizationProcessTagFromIni;
	if (ConfigFile.GetString(ConfigSection, TEXT("VirtualizationProcessTag"), VirtualizationProcessTagFromIni))
	{
		VirtualizationProcessTag = VirtualizationProcessTagFromIni;
		UE_LOG(LogVirtualization, Display, TEXT("\tVirtualizationProcessTag : %s"), *VirtualizationProcessTag);
	}
	else
	{
		UE_LOG(LogVirtualization, Error, TEXT("Failed to load [Core.VirtualizationModule].VirtualizationProcessTag from config file!"));
	}

	FString FilterModeFromIni;
	if (ConfigFile.GetString(LegacyConfigSection, TEXT("FilterMode"), FilterModeFromIni) ||
		ConfigFile.GetString(ConfigSection, TEXT("FilterMode"), FilterModeFromIni))
	{
		if(LexTryParseString(FilteringMode, FilterModeFromIni))
		{
			UE_LOG(LogVirtualization, Display, TEXT("\tFilterMode : %s"), *FilterModeFromIni);
		}
		else
		{
			UE_LOG(LogVirtualization, Error, TEXT("[Core.VirtualizationModule].FilterMode was an invalid value! Allowed: 'OptIn'|'OptOut' Found '%s'"), *FilterModeFromIni);
		}
	}
	else
	{
		UE_LOG(LogVirtualization, Error, TEXT("Failed to load [Core.VirtualizationModule]FilterMode from config file!"));
	}

	bool bFilterEngineContentFromIni = true;
	if (ConfigFile.GetBool(LegacyConfigSection, TEXT("FilterEngineContent"), bFilterEngineContentFromIni) ||
		ConfigFile.GetBool(ConfigSection, TEXT("FilterEngineContent"), bFilterEngineContentFromIni))
	{
		bFilterEngineContent = bFilterEngineContentFromIni;
		UE_LOG(LogVirtualization, Display, TEXT("\tFilterEngineContent : %s"), bFilterEngineContent ? TEXT("true") : TEXT("false"));
	}
	else
	{
		UE_LOG(LogVirtualization, Error, TEXT("Failed to load [Core.VirtualizationModule].FilterEngineContent from config file!"));
	}
	
	bool bFilterEnginePluginContentFromIni = true;
	if (ConfigFile.GetBool(LegacyConfigSection, TEXT("FilterEnginePluginContent"), bFilterEnginePluginContentFromIni) ||
		ConfigFile.GetBool(ConfigSection, TEXT("FilterEnginePluginContent"), bFilterEnginePluginContentFromIni))
	{
		bFilterEnginePluginContent = bFilterEnginePluginContentFromIni;
		UE_LOG(LogVirtualization, Display, TEXT("\tFilterEnginePluginContent : %s"), bFilterEnginePluginContent ? TEXT("true") : TEXT("false"));
	}
	else
	{
		UE_LOG(LogVirtualization, Error, TEXT("Failed to load [Core.VirtualizationModule].FilterEnginePluginContent from config file!"));
	}

	// Optional
	bool bFilterMapContentFromIni = false;
	if (ConfigFile.GetBool(ConfigSection, TEXT("FilterMapContent"), bFilterMapContentFromIni))
	{
		bFilterMapContent = bFilterMapContentFromIni;
		UE_LOG(LogVirtualization, Display, TEXT("\tFilterMapContent : %s"), bFilterMapContent ? TEXT("true") : TEXT("false"));
	}
	else
	{
		UE_LOG(LogVirtualization, Error, TEXT("Failed to load [Core.VirtualizationModule].FilterMapContent from config file!"));
	}

	// Optional
	TArray<FString> DisabledAssetTypesFromIni;
	if (ConfigFile.GetArray(LegacyConfigSection, TEXT("DisabledAsset"), DisabledAssetTypesFromIni) > 0 ||
		ConfigFile.GetArray(ConfigSection, TEXT("DisabledAsset"), DisabledAssetTypesFromIni) > 0)
	{
		UE_LOG(LogVirtualization, Display, TEXT("\tVirtualization is disabled for payloads of the following assets:"));
		DisabledAssetTypes.Reserve(DisabledAssetTypesFromIni.Num());
		for(const FString& AssetType : DisabledAssetTypesFromIni)
		{ 
			UE_LOG(LogVirtualization, Display, TEXT("\t\t%s"), *AssetType);
			DisabledAssetTypes.Add(FName(AssetType));
		}	
	}

	// Optional
	bool bAllowSubmitIfVirtualizationFailedFromIni = true;
	if (ConfigFile.GetBool(ConfigSection, TEXT("AllowSubmitIfVirtualizationFailed"), bAllowSubmitIfVirtualizationFailedFromIni))
	{
		bAllowSubmitIfVirtualizationFailed = bAllowSubmitIfVirtualizationFailedFromIni;
		UE_LOG(LogVirtualization, Display, TEXT("\tAllowSubmitIfVirtualizationFailed : %s"), bAllowSubmitIfVirtualizationFailed ? TEXT("true") : TEXT("false"));
	}
	else
	{
		UE_LOG(LogVirtualization, Error, TEXT("Failed to load [Core.VirtualizationModule].AllowSubmitIfVirtualizationFailed from config file!"));
	}

	// Check for any legacy settings and print them out (easier to do this in one block rather than one and time)
	if (const FConfigSection* LegacySection = ConfigFile.Find(LegacyConfigSection))
	{
		if (LegacySection->Num() > 1)
		{
			UE_LOG(LogVirtualization, Warning, TEXT("\tFound %d legacy ini file settings under [Core.ContentVirtualization] that should be moved to [Core.VirtualizationModule]"), LegacySection->Num() - 1);
			for (auto Iterator : *LegacySection)
			{
				FString Name = Iterator.Key.ToString();
				if (Name != TEXT("SystemName"))
				{
					UE_LOG(LogVirtualization, Warning, TEXT("\t\t%s"), *Iterator.Key.ToString());
				}
			}
		}
	}
}

void FVirtualizationManager::ApplyDebugSettingsFromFromCmdline()
{
	if (FParse::Param(FCommandLine::Get(), TEXT("VA-SingleThreaded")))
	{
		DebugValues.bSingleThreaded = true;
		UE_LOG(LogVirtualization, Warning, TEXT("Cmdline has set the virtualization system to run single threaded"));
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("VA-ValidatePushes")))
	{
		DebugValues.bValidateAfterPush = true;
		UE_LOG(LogVirtualization, Warning, TEXT("Cmdline has set the virtualization system to pull each payload after pushing to either local or persistent storage"));
	}

	FString CmdlineGraphName;
	if (FParse::Value(FCommandLine::Get(), TEXT("-VA-BackendGraph="), CmdlineGraphName))
	{
		UE_LOG(LogVirtualization, Display, TEXT("Backend graph overriden from the cmdline: '%s'"), *CmdlineGraphName);
		BackendGraphName = CmdlineGraphName;
	}

	FString MissOptions;
	if (FParse::Value(FCommandLine::Get(), TEXT("-VA-MissBackends="), MissOptions))
	{
		MissOptions.ParseIntoArray(DebugValues.MissBackends, TEXT("+"), true);

		UE_LOG(LogVirtualization, Warning, TEXT("Cmdline has disabled payload pulling for the following backends:"));
		for (const FString& Backend : DebugValues.MissBackends)
		{
			UE_LOG(LogVirtualization, Warning, TEXT("\t%s"), *Backend);
		}
	}

	DebugValues.MissChance = 0.0f;
	if (FParse::Value(FCommandLine::Get(), TEXT("-VA-MissChance="), DebugValues.MissChance))
	{
		DebugValues.MissChance = FMath::Clamp(DebugValues.MissChance, 0.0f, 100.0f);

		UE_LOG(LogVirtualization, Warning, TEXT("Cmdline has set a %.1f%% chance of a payload pull failing"), DebugValues.MissChance);
	}
}

void FVirtualizationManager::RegisterConsoleCommands()
{
	DebugValues.ConsoleObjects.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("VA.MissBackends"),
		TEXT("A debug commnad which can be used to disable payload pulling on one or more backends"),
		FConsoleCommandWithArgsAndOutputDeviceDelegate::CreateRaw(this, &FVirtualizationManager::OnUpdateDebugMissBackendsFromConsole)
	));

	DebugValues.ConsoleObjects.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("VA.MissChance"),
		TEXT("A debug command which can be used to set the chance that a payload pull will fail"),
		FConsoleCommandWithArgsAndOutputDeviceDelegate::CreateRaw(this, &FVirtualizationManager::OnUpdateDebugMissChanceFromConsole)
	));

	DebugValues.ConsoleObjects.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("VA.MissCount"),
		TEXT("A debug command which can be used to cause the next X number of payload pulls to fail"),
		FConsoleCommandWithArgsAndOutputDeviceDelegate::CreateRaw(this, &FVirtualizationManager::OnUpdateDebugMissCountFromConsole)
	));

	DebugValues.ConsoleObjects.Add(IConsoleManager::Get().RegisterConsoleVariableRef(
		TEXT("VA.SingleThreaded"),
		DebugValues.bSingleThreaded,
		TEXT("When set the asset virtualization system will only access backends in a single threaded manner")
	));

	DebugValues.ConsoleObjects.Add(IConsoleManager::Get().RegisterConsoleVariableRef(
		TEXT("VA.ValidatePushes"),
		DebugValues.bValidateAfterPush,
		TEXT("When set the asset virtualization system will pull each payload after pushing to either local or persistent storage")
	));
}

void FVirtualizationManager::OnUpdateDebugMissBackendsFromConsole(const TArray<FString>& Args, FOutputDevice& OutputDevice)
{
	if (Args.IsEmpty())
	{
		OutputDevice.Log(TEXT("VA.MissBackends command help"));
		OutputDevice.Log(TEXT("This command allows you to disable the pulling of payloads by specific backends"));
		OutputDevice.Log(TEXT(""));
		OutputDevice.Log(TEXT("Commands:"));
		OutputDevice.Log(TEXT("VA.MissBackends reset            - Empties the list of backends, everything will function normally"));
		OutputDevice.Log(TEXT("VA.MissBackends list             - Prints the list of backends affected"));
		OutputDevice.Log(TEXT("VA.MissBackends set Name0 Name1  - List each backend that you want to fail to pull payloads"));
		OutputDevice.Log(TEXT("VA.MissBackends set All          - All backends will fail to pull payloads"));
		OutputDevice.Log(TEXT(""));
		OutputDevice.Log(TEXT("Valid backend names:"));

		for (const TUniquePtr<IVirtualizationBackend>& Backend : AllBackends)
		{
			OutputDevice.Logf(TEXT("\t%s"), *Backend->GetConfigName());
		}
	}
	else if (Args.Num() == 1)
	{
		if (Args[0] == TEXT("reset"))
		{
			DebugValues.MissBackends.Empty();
			UpdateBackendDebugState();
		}
		else if (Args[0] == TEXT("list"))
		{
			if (!DebugValues.MissBackends.IsEmpty())
			{
				OutputDevice.Log(TEXT("Disabled backends:"));
				for (const FString& Backend : DebugValues.MissBackends)
				{
					OutputDevice.Logf(TEXT("\t%s"), *Backend);
				}
			}
			else
			{
				OutputDevice.Log(TEXT("No backends are disabled"));
			}
		}
		else
		{
			OutputDevice.Log(ELogVerbosity::Error, TEXT("Invalid args for the VA.MissBackends command!"));
		}
	}
	else if (Args[0] == TEXT("set"))
	{	
		DebugValues.MissBackends.Empty(Args.Num() - 1);

		for (int32 Index = 1; Index < Args.Num(); ++Index)
		{
			DebugValues.MissBackends.Add(Args[Index]);
		}

		UpdateBackendDebugState();
	}
	else
	{
		OutputDevice.Log(ELogVerbosity::Error, TEXT("Invalid args for the VA.MissBackends command!"));
	}
}

void FVirtualizationManager::OnUpdateDebugMissChanceFromConsole(const TArray<FString>& Args, FOutputDevice& OutputDevice)
{
	if (Args.IsEmpty())
	{
		OutputDevice.Log(TEXT("VA.MissChance command help"));
		OutputDevice.Log(TEXT("This command allows you to set the chance (in percent) that a payload pull request will just fail"));
		OutputDevice.Log(TEXT(""));
		OutputDevice.Log(TEXT("Commands:"));
		OutputDevice.Log(TEXT("VA.MissChance show     - prints the current miss percent chance"));
		OutputDevice.Log(TEXT("VA.MissChance set Num - Sets the miss percent chance to the given value"));
	}
	else if (Args.Num() == 1 && Args[0] == TEXT("show"))
	{
		OutputDevice.Logf(TEXT("Current debug miss chance: %.1f%%"), DebugValues.MissChance);
	}
	else if (Args.Num() == 2 && Args[0] == TEXT("set"))
	{
		if (::LexTryParseString(DebugValues.MissChance, *Args[1]))
		{
			DebugValues.MissChance = FMath::Clamp(DebugValues.MissChance, 0.0f, 100.0f);
			OutputDevice.Logf(TEXT("Current debug miss chance set to %.1f%%"), DebugValues.MissChance);
		}
		else
		{
			DebugValues.MissChance = 0.0f;
			OutputDevice.Log(ELogVerbosity::Error, TEXT("Invalid value, current debug miss chance reset to 0.0%"));
		}
	}
	else
	{
		OutputDevice.Log(ELogVerbosity::Error, TEXT("Invalid args for the VA.MissChance command!"));
	}
}

void FVirtualizationManager::OnUpdateDebugMissCountFromConsole(const TArray<FString>& Args, FOutputDevice& OutputDevice)
{
	if (Args.IsEmpty())
	{
		OutputDevice.Log(TEXT("VA.MissCount command help"));
		OutputDevice.Log(TEXT("This command allows you to set the next X number of payload pulls to fail"));
		OutputDevice.Log(TEXT(""));
		OutputDevice.Log(TEXT("Commands:"));
		OutputDevice.Log(TEXT("VA.MissCount show     - prints the current number of future payload pulls that will fail"));
		OutputDevice.Log(TEXT("VA.MissChance set Num - Sets the number of future payload pulls to fail"));
	}
	else if (Args.Num() == 1 && Args[0] == TEXT("show"))
	{
		// DebugMissCount could end up negative if many threads are pulling at once, so clamp to 0 as the min value
		const int32 Value = FMath::Max(DebugValues.MissCount.load(std::memory_order_relaxed), 0);
		OutputDevice.Logf(TEXT("The next '%d' payload pulls will fail"), Value);
	}
	else if (Args.Num() == 2 && Args[0] == TEXT("set"))
	{
		int32 ValueToSet = 0;
		if (::LexTryParseString(ValueToSet, *Args[1]))
		{
			DebugValues.MissCount.store(ValueToSet, std::memory_order_relaxed);
			OutputDevice.Logf(TEXT("The next '%d' payload pulls have been set to fail"), ValueToSet);
		}
		else
		{
			DebugValues.MissCount.store(0, std::memory_order_relaxed);
			OutputDevice.Log(ELogVerbosity::Error, TEXT("Invalid value, the number of future payload pulls to fail has been set to zero"));
		}
	}
	else
	{
		OutputDevice.Log(ELogVerbosity::Error, TEXT("Invalid args for the VA.MissCount command!"));
	}
}

void FVirtualizationManager::UpdateBackendDebugState()
{
	for (TUniquePtr<IVirtualizationBackend>& Backend : AllBackends)
	{
		const bool bDisable = ShouldDebugDisablePulling(Backend->GetConfigName());
		Backend->SetOperationDebugState(IVirtualizationBackend::EOperations::Pull, bDisable);
	}
}

bool FVirtualizationManager::ShouldDebugDisablePulling(FStringView BackendConfigName) const
{
	if (DebugValues.MissBackends.IsEmpty())
	{
		return false;
	}

	if (DebugValues.MissBackends[0] == TEXT("All"))
	{
		return true;
	}

	for (const FString& Name : DebugValues.MissBackends)
	{
		if (Name == BackendConfigName)
		{
			return true;
		}
	}

	return false;
}

bool FVirtualizationManager::ShouldDebugFailPulling()
{
	// We don't want to decrement on every function call to avoid DebugMissCount
	// underflowing, so we only try to decrement if the count is positive.
	// It doesn't really matter if the value ends up a little bit negative.
	if (DebugValues.MissCount.load(std::memory_order_relaxed) > 0)
	{
		if (DebugValues.MissCount.fetch_sub(1, std::memory_order_relaxed) > 0)
		{
			return true;
		}
	}

	if (DebugValues.MissChance == 0.0f)
	{
		return false;
	}
	else
	{
		// Could consider adding a lock here, although FRandomStream
		// is thread safe, many threads hitting it could cause a few
		// threads to get the same results.
		// Since this is a debug function and the percent is only a
		// rough guide, adding a lock is considered overkill. This
		// should only be done if in the future we decide that we want
		// more accuracy.
		static FRandomStream RandomStream(NAME_None);

		const float RandValue = RandomStream.FRand() * 100.0f;
		return RandValue <= DebugValues.MissChance;
	}
}

void FVirtualizationManager::MountBackends(const FConfigFile& ConfigFile)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FVirtualizationManager::MountBackends);

	const FRegistedFactories FactoryLookupTable = FindBackendFactories();
	UE_LOG(LogVirtualization, Verbose, TEXT("Found %d backend factories"), FactoryLookupTable.Num());

	const TCHAR* GraphName = *BackendGraphName;

	if(!ConfigFile.DoesSectionExist(GraphName))
	{
		UE_LOG(LogVirtualization, Fatal, TEXT("Unable to find the backend graph: '%s' [ini=%s]."), GraphName, *GEngineIni);
	}

	UE_LOG(LogVirtualization, Display, TEXT("Mounting virtualization backend graph: '%s'"), GraphName);

	// It is important to parse the local storage hierarchy first so those backends will show up before the
	// persistent storage backends in 'PullEnabledBackends'.
	ParseHierarchy(ConfigFile, GraphName, TEXT("LocalStorageHierarchy"), FactoryLookupTable, LocalCachableBackends);
	ParseHierarchy(ConfigFile, GraphName, TEXT("PersistentStorageHierarchy"), FactoryLookupTable, PersistentStorageBackends);

	// Apply and disabled backends from the command line
	UpdateBackendDebugState();
}

void FVirtualizationManager::ParseHierarchy(const FConfigFile& ConfigFile, const TCHAR* GraphName, const TCHAR* HierarchyKey, const FRegistedFactories& FactoryLookupTable, FBackendArray& PushArray)
{
	FString HierarchyData;
	if (ConfigFile.GetValue(GraphName, HierarchyKey, HierarchyData))
	{
		if (HierarchyData.IsEmpty())
		{
			UE_LOG(LogVirtualization, Fatal, TEXT("The '%s' entry for backend graph '%s' is empty [ini=%s]."), HierarchyKey, GraphName, *GEngineIni);
		}

		const TArray<FString> Entries = ParseEntries(HierarchyData);

		UE_LOG(LogVirtualization, Display, TEXT("'%s' has %d backend(s)"), HierarchyKey, Entries.Num());

		for (const FString& Entry : Entries)
		{
			CreateBackend(ConfigFile, GraphName, Entry, FactoryLookupTable, PushArray);
		}	
	}
	else
	{
		UE_LOG(LogVirtualization, Display, TEXT("No entries for '%s' in the content virtualization backend graph '%s' [ini=%s]."), HierarchyKey, GraphName, *GEngineIni);
	}
}

bool FVirtualizationManager::CreateBackend(const FConfigFile& ConfigFile, const TCHAR* GraphName, const FString& ConfigEntryName, const FRegistedFactories& FactoryLookupTable, FBackendArray& PushArray)
{
	// All failures in this method are considered fatal, however it still returns true/false in case we decide
	// to be more forgiving in the future.
	UE_LOG(LogVirtualization, Display, TEXT("Mounting backend entry '%s'"), *ConfigEntryName);

	FString BackendData;
	if (!ConfigFile.GetValue(GraphName, *ConfigEntryName, BackendData))
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
			TUniquePtr<IVirtualizationBackend> Backend = Factory->CreateInstance(ProjectName, ConfigEntryName);

			if (Backend == nullptr)
			{
				UE_LOG(LogVirtualization, Fatal, TEXT("IVirtualizationBackendFactory '%s' failed to create an instance!"), *Factory->GetName().ToString());
				return false;

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

	if (BackendRef->IsOperationSupported(IVirtualizationBackend::EOperations::Pull))
	{
		PullEnabledBackends.Add(BackendRef);
	}

	if (BackendRef->IsOperationSupported(IVirtualizationBackend::EOperations::Push))
	{
		PushArray.Add(BackendRef);
	}

	COOK_STAT(Profiling::CreateStats(*BackendRef));
}

void FVirtualizationManager::CachePayload(const FIoHash& Id, const FCompressedBuffer& Payload, const IVirtualizationBackend* BackendSource)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FVirtualizationManager::CachePayload);

	// We start caching at the first (assumed to be fastest) local cache backend. 
	for (IVirtualizationBackend* BackendToCache : LocalCachableBackends)
	{
		if (BackendToCache == BackendSource)
		{
			return; // No point going past BackendSource
		}

		const bool bResult = TryCacheDataToBackend(*BackendToCache, Id, Payload);
		UE_CLOG(	!bResult, LogVirtualization, Warning,
					TEXT("Failed to cache payload '%s' to backend '%s'"),
					*LexToString(Id),
					*BackendToCache->GetDebugName());

		// Debug operation to validate that the payload we just cached can be retrieved from storage
		if (DebugValues.bValidateAfterPush && bResult && BackendToCache->IsOperationSupported(IVirtualizationBackend::EOperations::Pull))
		{
			FCompressedBuffer PulledPayload = PullDataFromBackend(*BackendToCache, Id);
			checkf(	Payload.GetRawHash() == PulledPayload.GetRawHash(), 
					TEXT("[%s] Failed to pull payload '%s' after it was cached to backend"),
					*BackendToCache->GetDebugName(), 
					*LexToString(Id));
		}
	}
}

bool FVirtualizationManager::TryCacheDataToBackend(IVirtualizationBackend& Backend, const FIoHash& Id, const FCompressedBuffer& Payload)
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
	COOK_STAT(FCookStats::CallStats & Stats = Profiling::GetPushStats(Backend));
	COOK_STAT(FCookStats::FScopedStatsCounter Timer(Stats));
	COOK_STAT(Timer.TrackCyclesOnly());

	const bool bPushResult = Backend.PushData(Requests);

#if ENABLE_COOK_STATS
	if (bPushResult)
	{
		Timer.AddHit(0);

		const bool bIsInGameThread = IsInGameThread();

		for (const FPushRequest& Request : Requests)
		{
			// TODO: Don't add a hit if the payload was already uploaded
			if (Request.GetStatus() == FPushRequest::EStatus::Success)
			{
				Stats.Accumulate(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Counter, 1l, bIsInGameThread);	
				Stats.Accumulate(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Bytes, Request.GetPayloadSize(), bIsInGameThread);
			}
		}
	}
#endif // ENABLE_COOK_STATS
	
	return bPushResult;
}

FCompressedBuffer FVirtualizationManager::PullDataFromAllBackends(const FIoHash& Id)
{
	if (ShouldDebugFailPulling())
	{
		UE_LOG(LogVirtualization, Verbose, TEXT("Debug miss chance (%.1f%%) invoked when pulling payload '%s'"), DebugValues.MissChance, *LexToString(Id));
		return FCompressedBuffer();
	}

	for (IVirtualizationBackend* Backend : PullEnabledBackends)
	{
		// Skip if pulling has been disabled on this backend for debug purposes
		if (Backend->IsOperationDebugDisabled(IVirtualizationBackend::EOperations::Pull))
		{
			UE_LOG(LogVirtualization, Verbose, TEXT("Pulling from backend '%s' is debug disabled for payload '%s'"), *Backend->GetDebugName(), *LexToString(Id));
			continue;
		}

		FCompressedBuffer Payload = PullDataFromBackend(*Backend, Id);

		if (Payload)
		{
			if (bEnableCacheAfterPull)
			{
				CachePayload(Id, Payload, Backend);
			}

			UE_LOG(LogVirtualization, VeryVerbose, TEXT("[%s] pulled payload '%s'"), *Backend->GetDebugName(), *LexToString(Id));

			return Payload;
		}
	}

	return FCompressedBuffer();
}

FCompressedBuffer FVirtualizationManager::PullDataFromBackend(IVirtualizationBackend& Backend, const FIoHash& Id)
{
	COOK_STAT(FCookStats::FScopedStatsCounter Timer(Profiling::GetPullStats(Backend)));
	FCompressedBuffer Payload = Backend.PullData(Id);
	
	if (!Payload.IsNull())
	{
		COOK_STAT(Timer.AddHit(Payload.GetCompressedSize()));
	}
	
	return Payload;
}


bool FVirtualizationManager::ShouldVirtualizeAsset(const UObject* OwnerObject) const
{
	if (OwnerObject == nullptr)
	{
		return true;
	}

	const UClass* OwnerClass = OwnerObject->GetClass();
	if (OwnerClass == nullptr)
	{
		// TODO: Not actually sure if the class being nullptr is reasonable or if we should warn/error here?
		return true;
	}

	const FName ClassName = OwnerClass->GetFName();
	return DisabledAssetTypes.Find(ClassName) == nullptr;
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
		auto DoesMatch = [](const TArray<FString>& Paths, const FStringView& PackagePath) -> bool
		{
			for (const FString& PathToMatch : Paths)
			{
				if (PathToMatch.EndsWith(TEXT("/")))
				{
					// Directory path, exclude everything under it
					if (PackagePath.StartsWith(PathToMatch))
					{
						return true;
					}
				}
				else
				{
					// Path to an asset, exclude if it matches exactly
					if (PackagePath == PathToMatch)
					{
						return true;
					}
				}
			}

			return false;
		};

		const FStringView PackageNameView = PackageName.ToView();

		if (DoesMatch(Settings->ExcludePackagePaths, PackageNameView))
		{
			return false;
		}

		if (DoesMatch(Settings->IncludePackagePaths, PackageNameView))
		{
			return true;
		}
	}
	
	// The package is not in any of the include/exclude paths so we use the default behavior
	return ShouldVirtualizeAsDefault();
}

bool FVirtualizationManager::ShouldVirtualize(const FString& Context) const
{
	// First see if we can convert the context from a raw string to a valid package path.
	// If we can extract a package path then we should use the package filtering code
	// path instead.
	FPackagePath PackagePath;
	if (FPackagePath::TryFromPackageName(Context, PackagePath))
	{
		return ShouldVirtualizePackage(PackagePath);
	}

	if (FPackagePath::TryFromMountedName(Context, PackagePath))
	{
		return ShouldVirtualizePackage(PackagePath);
	}

	// The package is not in any of the include/exclude paths so we use the default behavior
	return ShouldVirtualizeAsDefault();
}

bool FVirtualizationManager::ShouldVirtualizeAsDefault() const
{
	switch (FilteringMode)
	{
		case EPackageFilterMode::OptOut:
			return true;
		case EPackageFilterMode::OptIn:
			return false;
		default:
			checkNoEntry();
			return false;
	}
}

} // namespace UE::Virtualization

#undef LOCTEXT_NAMESPACE
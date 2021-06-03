// Copyright Epic Games, Inc. All Rights Reserved.

#include "Virtualization/VirtualizationManager.h"

#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Virtualization/IVirtualizationBackend.h"

#include "ProfilingDebugging/CookStats.h"
#include "Misc/CoreDelegates.h"

namespace UE::Virtualization
{

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
TMap<FName, IVirtualizationBackendFactory*> FindBackendFactories()
{
	TMap<FName, IVirtualizationBackendFactory*> BackendFactories;

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
 * so we don't hasve to worry about that either.
 * We keep the FCookStats here rather than as a member of IVirtualizationBackend to try and avoid the backends needing to be aware of the data that
 * we are gathering at all. This way all profiling code is kept to this cpp.
 */
namespace Profiling
{
#if ENABLE_COOK_STATS
	TMap<FString, FCookStats::CallStats> PushStats;
	TMap<FString, FCookStats::CallStats> PullStats;

	void CreateStats(const IVirtualizationBackend& Backend)
	{
		PushStats.Add(Backend.GetDebugString());
		PullStats.Add(Backend.GetDebugString());
	}

	FCookStats::CallStats& GetPushStats(const IVirtualizationBackend& Backend)
	{
		return *PushStats.Find(Backend.GetDebugString());
	}

	FCookStats::CallStats& GetPullStats(const IVirtualizationBackend& Backend)
	{
		return *PullStats.Find(Backend.GetDebugString());
	}

	void LogStats()
	{
		if (PushStats.IsEmpty() && PullStats.IsEmpty())
		{
			return; // Early out if we have no data to show at all
		}

		UE_LOG(LogVirtualization, Log, TEXT("Virtualization ProfileData"));

		if (PushStats.Num() > 0)
		{
			UE_LOG(LogVirtualization, Log, TEXT("%-40s|%17s|%12s|%14s|"), TEXT("Pushing Data"), TEXT("TotalSize (MB)"), TEXT("TotalTime(s)"), TEXT("DataRate(MB/S)"));

			for (const auto& Iterator : PushStats)
			{
				const double Time = Iterator.Value.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Cycles) * FPlatformTime::GetSecondsPerCycle();
				const int64 DataSizeMB = Iterator.Value.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Bytes) / (1024 * 1024);
				const double MBPerSecond = DataSizeMB / Time;

				UE_LOG(LogVirtualization, Log, TEXT("%-40.40s|%17" UINT64_FMT "|%12.3f|%14.3f|"),
					*Iterator.Key,
					DataSizeMB,
					Time,
					MBPerSecond);
			}
		}

		if (PullStats.Num() > 0)
		{
			UE_LOG(LogVirtualization, Log, TEXT("%-40s|%17s|%12s|%14s|"), TEXT("Pulling Data"), TEXT("TotalSize (MB)"), TEXT("TotalTime(s)"), TEXT("DataRate(MB/S)"));

			for (const auto& Iterator : PullStats)
			{
				const double Time = Iterator.Value.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Cycles) * FPlatformTime::GetSecondsPerCycle();
				const int64 DataSizeMB = Iterator.Value.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Bytes) / (1024 * 1024);
				const double MBPerSecond = DataSizeMB / Time;

				UE_LOG(LogVirtualization, Log, TEXT("%-40.40s|%17" UINT64_FMT "|%12.3f|%14.3f|"),
					*Iterator.Key,
					DataSizeMB,
					Time,
					MBPerSecond);
			}
		}
	}
#endif // ENABLE_COOK_STATS
} //namespace Profiling

FVirtualizationManager& FVirtualizationManager::Get()
{
	// TODO: Do we really need to make this a singleton? Easier for prototyping.
	static FVirtualizationManager Singleton;
	return Singleton;
}

FVirtualizationManager::FVirtualizationManager()
	: bEnablePayloadPushing(true)
	, MinPayloadLength(0)
	, BackendGraphName(TEXT("ContentVirtualizationBackendGraph_None"))
	, bForceSingleThreaded(false)
	, bFailPayloadPullOperations(false)
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

	MountBackends();
}

FVirtualizationManager::~FVirtualizationManager()
{
	UE_LOG(LogVirtualization, Log, TEXT("Destroying backends"));

	for (IVirtualizationBackend* Backend : AllBackendsArray)
	{
		delete Backend;
	}

	AllBackendsArray.Empty();
	PullEnabledBackendsArray.Empty();
	PushEnabledBackendsArray.Empty();

	UE_LOG(LogVirtualization, Log, TEXT("Virtualization manager destroyed"));
}

bool FVirtualizationManager::IsEnabled() const
{
	return !PushEnabledBackendsArray.IsEmpty();
}

bool FVirtualizationManager::PushData(const FPayloadId& Id, const FCompressedBuffer& Payload)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FVirtualizationManager::PushData);

	if (!Id.IsValid())
	{
		// TODO: Should an invalid FPayloadId be an expected input, if so demote this from Warning->Verbose
		UE_LOG(LogVirtualization, Warning, TEXT("Attempting to push a virtualized payload with an invalid FPayloadId"));
		return false;
	}

	FConditionalScopeLock _(&ForceSingleThreadedCS, bForceSingleThreaded);

	// Early out if there are no backends or if the pushing of payloads has been disabled
	if (PushEnabledBackendsArray.IsEmpty() || bEnablePayloadPushing == false)
	{
		return false;
	}

	// Early out if we have no payload
	if (Payload.GetCompressedSize() == 0)
	{
		// TODO: Should an invalid payload be an expected input, if so demote this from Warning->Verbose
		UE_LOG(LogVirtualization, Warning, TEXT("Attempting to push an invalid virtualized payload (id: %s)"), *Id.ToString());
		return false;
	}

	// Early out if the payload length is below our minimum required length
	if ((int64)Payload.GetCompressedSize() < MinPayloadLength)
	{
		UE_LOG(	LogVirtualization, 
				Verbose, 
				TEXT("Attempting to push a virtualized payload (id: %s) that is smaller (%" UINT64_FMT ") than the MinPayloadLength (%" INT64_FMT ")"), 
				*Id.ToString(), Payload.GetCompressedSize(), MinPayloadLength);

		return false;
	}

	// TODO: Note that all push operations are currently synchronous, probably 
	// should change to async at some point, although this makes handling failed
	// pushed much more difficult.
	bool bWasPayloadPushed = false;
	for (IVirtualizationBackend* Backend : PushEnabledBackendsArray)
	{
		const EPushResult Result = TryPushDataToBackend(*Backend, Id, Payload) ? EPushResult::Success : EPushResult::Failed;

		UE_CLOG(Result != EPushResult::Failed, LogVirtualization, Verbose, TEXT("[%s] Pushed the payload '%s'"), *Backend->GetDebugString(), *Id.ToString());
		UE_CLOG(Result == EPushResult::Failed, LogVirtualization, Error, TEXT("[%s] Failed to push the payload '%s'"), *Backend->GetDebugString(), *Id.ToString());

		if (Result != EPushResult::Failed)
		{
			bWasPayloadPushed = true;
		}
		
		// Debugging operation where we immediately try to pull the payload after each push (when possible) and assert 
		// that the pulled payload is the same as the original
		if (bValidateAfterPushOperation && Result != EPushResult::Failed && Backend->SupportsPullOperations())
		{
			FCompressedBuffer PulledPayload = PullDataFromBackend(*Backend, Id);
			checkf(Payload.GetRawHash() == PulledPayload.GetRawHash(), TEXT("[%s] Failed to pull payload '%s' after it was pushed to backend"), 
				*Backend->GetDebugString(), *Id.ToString());
		}
	}

	UE_CLOG(!bWasPayloadPushed, LogVirtualization, Fatal, TEXT("Payload '%s' failed to be pushed to any backend'"), *Id.ToString());

	return bWasPayloadPushed;
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

	if (PullEnabledBackendsArray.IsEmpty())
	{
		// TODO: See below, should errors here be fatal?
		UE_LOG(LogVirtualization, Error, TEXT("Payload '%s' failed to be pulled as there are no backends mounted!'"), *Id.ToString());
		return FCompressedBuffer();
	}

	if (bFailPayloadPullOperations)
	{
		UE_LOG(LogVirtualization, Error, TEXT("Payload '%s' failed to be pulled as the debug option 'FailPayloadPullOperations' is enabled!"), *Id.ToString());
		return FCompressedBuffer();
	}

	FConditionalScopeLock _(&ForceSingleThreadedCS, bForceSingleThreaded);

	// TODO: Once a payload is found, other backends should probably be notified 
	// (a local cache might want to replicate the data for example)
	for (IVirtualizationBackend* Backend : PullEnabledBackendsArray)
	{
		FCompressedBuffer Payload = PullDataFromBackend(*Backend, Id);
		if (Payload)
		{
			return Payload;
		}
	}

	// TODO: Maybe this should be a fatal error? If we keep it as an error we need to make sure any calling
	// code handles it properly.
	// Could be worth extending ::PullData to return error codes instead so we can make a better distinction 
	// between the payload not being found in any of the backends and one or more of the backends failing.
	UE_LOG(LogVirtualization, Error, TEXT("Payload '%s' failed to be pulled from any backend'"), *Id.ToString());

	return FCompressedBuffer();
}

FPayloadActivityInfo FVirtualizationManager::GetPayloadActivityInfo() const
{
	FPayloadActivityInfo Info;

#if ENABLE_COOK_STATS
	for (const auto& Iterator : Profiling::PushStats)
	{
		Info.PayloadsPushed += Iterator.Value.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Counter);
		Info.TotalSizePushed += Iterator.Value.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Bytes) / (1024 * 1024);
	}

	for (const auto& Iterator : Profiling::PullStats)
	{
		Info.PayloadsPulled += Iterator.Value.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Counter);
		Info.TotalSizePulled += Iterator.Value.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Bytes) / (1024 * 1024);
	}
#endif // ENABLE_COOK_STATS

	return Info;
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

	bool bFailPayloadPullOperationsFromIni = false;
	if (PlatformEngineIni.GetBool(TEXT("Core.ContentVirtualizationDebugOptions"), TEXT("FailPayloadPullOperations"), bFailPayloadPullOperationsFromIni))
	{
		bFailPayloadPullOperations = bFailPayloadPullOperationsFromIni;
		UE_LOG(LogVirtualization, Log, TEXT("\tFailPayloadPullOperations : %s"), bFailPayloadPullOperations ? TEXT("true") : TEXT("false"));
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
	UE_CLOG(bFailPayloadPullOperations, LogVirtualization, Error, TEXT("FailPayloadPullOperations is enabled, all virtualization pull operations will fail!"));
	UE_CLOG(bValidateAfterPushOperation, LogVirtualization, Error, TEXT("ValidateAfterPushOperation is enabled, each push will be followed by a pull to validate it!"));
}

void FVirtualizationManager::MountBackends()
{
	UE_LOG(LogVirtualization, Log, TEXT("Mounting virtualization backends..."));

	TMap<FName, IVirtualizationBackendFactory*> FactoryLookupTable = FindBackendFactories();
	UE_LOG(LogVirtualization, Verbose, TEXT("Found %d backend factories"), FactoryLookupTable.Num());

	const TCHAR* GraphName = *BackendGraphName;
	const TCHAR* HierarchyKey = TEXT("Hierarchy");

	UE_LOG(LogVirtualization, Log, TEXT("Using backend graph: '%s'"), GraphName);

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

	UE_LOG(LogVirtualization, Log, TEXT("The backend graph hierarchy has %d entries"), Entries.Num());

	for (const FString& Entry : Entries)
	{
		CreateBackend(GraphName, Entry, FactoryLookupTable);
	}
}

bool FVirtualizationManager::CreateBackend(const TCHAR* GraphName, const FString& ConfigEntryName, TMap<FName, IVirtualizationBackendFactory*>& FactoryLookupTable)
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

		IVirtualizationBackendFactory** FactoryPtr = FactoryLookupTable.Find(FName(BackendType));
		if (FactoryPtr != nullptr && *FactoryPtr != nullptr)
		{
			IVirtualizationBackendFactory* Factory = *FactoryPtr;
			IVirtualizationBackend* Backend = Factory->CreateInstance(ConfigEntryName);

			if (Backend == nullptr)
			{
				UE_LOG(LogVirtualization, Fatal, TEXT("IVirtualizationBackendFactory '%s' failed to create an instance!"), *Factory->GetName().ToString());
				return false;

			}
			
			if (Backend->Initialize(Cmdine))
			{
				AddBackend(Backend);
			}
			else
			{
				UE_LOG(LogVirtualization, Fatal, TEXT("Backend '%s' reported errors when initializing"), *ConfigEntryName);
				delete Backend;
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

void FVirtualizationManager::AddBackend(IVirtualizationBackend* Backend)
{
	checkf(!AllBackendsArray.Contains(Backend), TEXT("Adding the same virtualization backend (%s) multiple times!"), *Backend->GetDebugString());

	AllBackendsArray.Add(Backend);

	if (Backend->SupportsPullOperations())
	{
		PullEnabledBackendsArray.Add(Backend);
	}

	if (Backend->SupportsPushOperations())
	{
		PushEnabledBackendsArray.Add(Backend);
	}

	COOK_STAT(Profiling::CreateStats(*Backend));

	UE_LOG(LogVirtualization, Log, TEXT("Mounted backend: %s"), *Backend->GetDebugString());
}

bool FVirtualizationManager::TryPushDataToBackend(IVirtualizationBackend& Backend, const FPayloadId& Id, const FCompressedBuffer& Payload)
{
	COOK_STAT(FCookStats::FScopedStatsCounter Timer(Profiling::GetPushStats(Backend)));
	const EPushResult Result = Backend.PushData(Id, Payload);

	if (Result == EPushResult::Success)
	{
		COOK_STAT(Timer.AddHit(Payload.GetCompressedSize()));
	}

	return Result != EPushResult::Failed;
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

} // namespace UE::Virtualization

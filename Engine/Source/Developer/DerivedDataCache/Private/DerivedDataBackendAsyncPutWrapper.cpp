// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataBackendAsyncPutWrapper.h"
#include "MemoryDerivedDataBackend.h"

/** 
 * Async task to handle the fire and forget async put
 */
class FCachePutAsyncWorker
{
public:
	/** Cache Key for the put to InnerBackend **/
	FString								CacheKey;
	/** Data for the put to InnerBackend **/
	TArray<uint8>						Data;
	/** Backend to use for storage, my responsibilities are about async puts **/
	FDerivedDataBackendInterface*		InnerBackend;
	/** Memory based cache to clear once the put is finished **/
	FDerivedDataBackendInterface*		InflightCache;
	/** We remember outstanding puts so that we don't do them redundantly **/
	FThreadSet*							FilesInFlight;
	/**If true, then do not attempt skip the put even if CachedDataProbablyExists returns true **/
	bool								bPutEvenIfExists;
	/** Usage stats to track thread times. */
	FDerivedDataCacheUsageStats&        UsageStats;

	/** Constructor
	*/
	FCachePutAsyncWorker(const TCHAR* InCacheKey, TArrayView<const uint8> InData, FDerivedDataBackendInterface* InInnerBackend, bool InbPutEvenIfExists, FDerivedDataBackendInterface* InInflightCache, FThreadSet* InInFilesInFlight, FDerivedDataCacheUsageStats& InUsageStats)
		: CacheKey(InCacheKey)
		, Data(InData.GetData(), InData.Num())
		, InnerBackend(InInnerBackend)
		, InflightCache(InInflightCache)
		, FilesInFlight(InInFilesInFlight)
		, bPutEvenIfExists(InbPutEvenIfExists)
		, UsageStats(InUsageStats)
	{
		check(InnerBackend);
	}
		
	/** Call the inner backend and when that completes, remove the memory cache */
	void DoWork()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(DDCPut_DoWork);
		COOK_STAT(auto Timer = UsageStats.TimePut());
		bool bOk = true;
		bool bDidTry = false;
		if (bPutEvenIfExists || !InnerBackend->CachedDataProbablyExists(*CacheKey))
		{
			bDidTry = true;
			InnerBackend->PutCachedData(*CacheKey, Data, bPutEvenIfExists);
			COOK_STAT(Timer.AddHit(Data.Num()));
		}
		// if we tried to put it up there but it isn't there now, retry
		if (InflightCache && bDidTry && !InnerBackend->CachedDataProbablyExists(*CacheKey))
		{
			// retry after a brief wait
			FPlatformProcess::SleepNoStats(0.2f);
			InnerBackend->PutCachedData(*CacheKey, Data, false);

			if (!InnerBackend->CachedDataProbablyExists(*CacheKey))
			{
				UE_LOG(LogDerivedDataCache, Display, TEXT("%s: Put failed, keeping in memory copy %s."),*InnerBackend->GetName(), *CacheKey);

				// log the filesystem error
				uint32 ErrorCode = FPlatformMisc::GetLastError();
				TCHAR ErrorBuffer[1024];
				FPlatformMisc::GetSystemErrorMessage(ErrorBuffer, 1024, ErrorCode);
				UE_LOG(LogDerivedDataCache, Display, TEXT("Failed to write %s to %s. Error: %u (%s)"), *CacheKey, *InnerBackend->GetName(), ErrorCode, ErrorBuffer);
				bOk = false;
			}
		}
		if (bOk && InflightCache)
		{
			InflightCache->RemoveCachedData(*CacheKey, /*bTransient=*/ false); // we can remove this from the temp cache, since the real cache will hit now
		}
		FilesInFlight->Remove(CacheKey);
		FDerivedDataBackend::Get().AddToAsyncCompletionCounter(-1);
		UE_LOG(LogDerivedDataCache, VeryVerbose, TEXT("%s: Completed AsyncPut of %s."), *InnerBackend->GetName(), *CacheKey);
	}

	FORCEINLINE TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FCachePutAsyncWorker, STATGROUP_ThreadPoolAsyncTasks);
	}

	/** Indicates to the thread pool that this task is abandonable */
	bool CanAbandon()
	{
		return true;
	}

	/** Abandon routine, we need to remove the item from the in flight cache because something might be waiting for that */
	void Abandon()
	{
		if (InflightCache)
		{
			InflightCache->RemoveCachedData(*CacheKey, /*bTransient=*/ false); // we can remove this from the temp cache, since the real cache will hit now
		}
		FilesInFlight->Remove(CacheKey);
		FDerivedDataBackend::Get().AddToAsyncCompletionCounter(-1);
		UE_LOG(LogDerivedDataCache, VeryVerbose, TEXT("%s: Abandoned AsyncPut of %s."), *InnerBackend->GetName(), *CacheKey);
	}
};

FDerivedDataBackendAsyncPutWrapper::FDerivedDataBackendAsyncPutWrapper(FDerivedDataBackendInterface* InInnerBackend, bool bCacheInFlightPuts)
	: InnerBackend(InInnerBackend)
	, InflightCache(bCacheInFlightPuts ? (new FMemoryDerivedDataBackend(TEXT("InflightMemoryCache"))) : NULL)
{
	check(InnerBackend);
}

/** return true if this cache is writable **/
bool FDerivedDataBackendAsyncPutWrapper::IsWritable() const
{
	return InnerBackend->IsWritable();
}

FDerivedDataBackendInterface::ESpeedClass FDerivedDataBackendAsyncPutWrapper::GetSpeedClass() const
{
	return InnerBackend->GetSpeedClass();
}

/**
 * Synchronous test for the existence of a cache item
 *
 * @param	CacheKey	Alphanumeric+underscore key of this cache item
 * @return				true if the data probably will be found, this can't be guaranteed because of concurrency in the backends, corruption, etc
 */
bool FDerivedDataBackendAsyncPutWrapper::CachedDataProbablyExists(const TCHAR* CacheKey)
{
	COOK_STAT(auto Timer = UsageStats.TimeProbablyExists());
	bool Result = (InflightCache && InflightCache->CachedDataProbablyExists(CacheKey)) || InnerBackend->CachedDataProbablyExists(CacheKey);
	COOK_STAT(if (Result) {	Timer.AddHit(0); });

	UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s CachedDataProbablyExists=%d for %s"), *GetName(), Result, CacheKey);
	return Result;
}

bool FDerivedDataBackendAsyncPutWrapper::TryToPrefetch(const TCHAR* CacheKey)
{
	COOK_STAT(auto Timer = UsageStats.TimePrefetch());

	bool SkipCheck = (!InflightCache && InflightCache->CachedDataProbablyExists(CacheKey));
	
	bool Hit = false;

	if (!SkipCheck)
	{
		Hit = InnerBackend->TryToPrefetch(CacheKey);
	}

	COOK_STAT(if (Hit) { Timer.AddHit(0); });
	return Hit;
}

/*
	Determine if we would cache this by asking all our inner layers
*/
bool FDerivedDataBackendAsyncPutWrapper::WouldCache(const TCHAR* CacheKey, TArrayView<const uint8> InData)
{
	return InnerBackend->WouldCache(CacheKey, InData);
}

bool FDerivedDataBackendAsyncPutWrapper::ApplyDebugOptions(FBackendDebugOptions& InOptions)
{
	return InnerBackend->ApplyDebugOptions(InOptions);
}

bool FDerivedDataBackendAsyncPutWrapper::GetCachedData(const TCHAR* CacheKey, TArray<uint8>& OutData)
{
	COOK_STAT(auto Timer = UsageStats.TimeGet());
	if (InflightCache && InflightCache->GetCachedData(CacheKey, OutData))
	{
		COOK_STAT(Timer.AddHit(OutData.Num()));
		UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s CacheHit from InFlightCache on %s"), *GetName(), CacheKey);
		return true;
	}

	bool bSuccess = InnerBackend->GetCachedData(CacheKey, OutData);
	if (bSuccess)
	{
		UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s Cache hit on %s"), *GetName(), CacheKey);
		COOK_STAT(Timer.AddHit(OutData.Num()));
	}
	else
	{
		UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s Cache miss on %s"), *GetName(), CacheKey);
	}
	return bSuccess;
}

void FDerivedDataBackendAsyncPutWrapper::PutCachedData(const TCHAR* CacheKey, TArrayView<const uint8> InData, bool bPutEvenIfExists)
{
	COOK_STAT(auto Timer = PutSyncUsageStats.TimePut());

	if (!InnerBackend->IsWritable())
	{
		return; // no point in continuing down the chain
	}
	const bool bAdded = FilesInFlight.AddIfNotExists(CacheKey);
	if (!bAdded)
	{
		return; // if it is already on its way, we don't need to send it again
	}
	if (InflightCache)
	{
		if (InflightCache->CachedDataProbablyExists(CacheKey))
		{
			UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s skipping out of key already in in-flight cache %s"), *GetName(), CacheKey);
			return; // if it is already on its way, we don't need to send it again
		}
		InflightCache->PutCachedData(CacheKey, InData, true); // temp copy stored in memory while the async task waits to complete
		COOK_STAT(Timer.AddHit(InData.Num()));
	}

	UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s queueing %s for put"), *GetName(), CacheKey);

	FDerivedDataBackend::Get().AddToAsyncCompletionCounter(1);
	(new FAutoDeleteAsyncTask<FCachePutAsyncWorker>(CacheKey, InData, InnerBackend, bPutEvenIfExists, InflightCache.Get(), &FilesInFlight, UsageStats))->StartBackgroundTask();
}

void FDerivedDataBackendAsyncPutWrapper::RemoveCachedData(const TCHAR* CacheKey, bool bTransient)
{
	if (!InnerBackend->IsWritable())
	{
		return; // no point in continuing down the chain
	}
	while (FilesInFlight.Exists(CacheKey))
	{
		FPlatformProcess::Sleep(0.0f); // this is an exception condition (corruption), spin and wait for it to clear
	}
	if (InflightCache)
	{
		InflightCache->RemoveCachedData(CacheKey, bTransient);
	}
	InnerBackend->RemoveCachedData(CacheKey, bTransient);

	UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s removed %s"), *GetName(), CacheKey)
}

TSharedRef<FDerivedDataCacheStatsNode> FDerivedDataBackendAsyncPutWrapper::GatherUsageStats() const
{
	TSharedRef<FDerivedDataCacheStatsNode> Usage = MakeShared<FDerivedDataCacheStatsNode>(this, TEXT("AsyncPutWrapper"));
	Usage->Stats.Add(TEXT("AsyncPut"), UsageStats);
	Usage->Stats.Add(TEXT("AsyncPutSync"), PutSyncUsageStats);

	if (InnerBackend)
	{
		Usage->Children.Add(InnerBackend->GatherUsageStats());
	}
	if (InflightCache)
	{
		Usage->Children.Add(InflightCache->GatherUsageStats());
	}

	return Usage;
}

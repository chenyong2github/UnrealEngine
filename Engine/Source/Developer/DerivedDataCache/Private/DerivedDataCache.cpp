// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#include "CoreMinimal.h"
#include "Misc/CommandLine.h"
#include "HAL/ThreadSafeCounter.h"
#include "Misc/ScopeLock.h"
#include "Stats/StatsMisc.h"
#include "Stats/Stats.h"
#include "Async/AsyncWork.h"
#include "Async/TaskGraphInterfaces.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Modules/ModuleManager.h"

#include "DerivedDataCacheInterface.h"
#include "DerivedDataBackendInterface.h"
#include "DerivedDataPluginInterface.h"
#include "DDCCleanup.h"
#include "ProfilingDebugging/CookStats.h"

DEFINE_STAT(STAT_DDC_NumGets);
DEFINE_STAT(STAT_DDC_NumPuts);
DEFINE_STAT(STAT_DDC_NumBuilds);
DEFINE_STAT(STAT_DDC_NumExist);
DEFINE_STAT(STAT_DDC_SyncGetTime);
DEFINE_STAT(STAT_DDC_ASyncWaitTime);
DEFINE_STAT(STAT_DDC_PutTime);
DEFINE_STAT(STAT_DDC_SyncBuildTime);
DEFINE_STAT(STAT_DDC_ExistTime);

//#define DDC_SCOPE_CYCLE_COUNTER(x) QUICK_SCOPE_CYCLE_COUNTER(STAT_ ## x)
#define DDC_SCOPE_CYCLE_COUNTER(x) TRACE_CPUPROFILER_EVENT_SCOPE(x);


#if ENABLE_COOK_STATS
#include "DerivedDataCacheUsageStats.h"
namespace DerivedDataCacheCookStats
{
	// Use to prevent potential divide by zero issues
	inline double SafeDivide(const int64 Numerator, const int64 Denominator)
	{
		return Denominator != 0 ? (double)Numerator / (double)Denominator : 0.0;
	}

	FCookStatsManager::FAutoRegisterCallback RegisterCookStats([](FCookStatsManager::AddStatFuncRef AddStat)
	{
		TMap<FString, FDerivedDataCacheUsageStats> DDCStats;
		GetDerivedDataCacheRef().GatherUsageStats(DDCStats);
		{
			const FString StatName(TEXT("DDC.Usage"));
			for (const auto& UsageStatPair : DDCStats)
			{
				UsageStatPair.Value.LogStats(AddStat, StatName, UsageStatPair.Key);
			}
		}

		// Now lets add some summary data to that applies some crazy knowledge of how we set up our DDC. The goal 
		// is to print out the global hit rate, and the hit rate of the local and shared DDC.
		// This is done by adding up the total get/miss calls the root node receives.
		// Then we find the FileSystem nodes that correspond to the local and shared cache using some hacky logic to detect a "network drive".
		// If the DDC graph ever contains more than one local or remote filesystem, this will only find one of them.
		{
			TArray<FString, TInlineAllocator<20>> Keys;
			DDCStats.GenerateKeyArray(Keys);
			FString* RootKey = Keys.FindByPredicate([](const FString& Key) {return Key.StartsWith(TEXT(" 0:")); });
			// look for a Filesystem DDC that doesn't have a UNC path. Ugly, yeah, but we only cook on PC at the moment.
			FString* LocalDDCKey = Keys.FindByPredicate([](const FString& Key) {return Key.Contains(TEXT(": FileSystem.")) && !Key.Contains(TEXT("//")); });
			// look for a UNC path
			FString* SharedDDCKey = Keys.FindByPredicate([](const FString& Key) {return Key.Contains(TEXT(": FileSystem.//")); });
			if (RootKey)
			{
				const FDerivedDataCacheUsageStats& RootStats = DDCStats[*RootKey];
				int64 TotalGetHits =
					RootStats.GetStats.GetAccumulatedValue(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Counter, true) +
					RootStats.GetStats.GetAccumulatedValue(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Counter, false);
				int64 TotalGetMisses =
					RootStats.GetStats.GetAccumulatedValue(FCookStats::CallStats::EHitOrMiss::Miss, FCookStats::CallStats::EStatType::Counter, true) +
					RootStats.GetStats.GetAccumulatedValue(FCookStats::CallStats::EHitOrMiss::Miss, FCookStats::CallStats::EStatType::Counter, false);
				int64 TotalGets = TotalGetHits + TotalGetMisses;

				int64 LocalHits = 0;
				if (LocalDDCKey)
				{
					const FDerivedDataCacheUsageStats& LocalDDCStats = DDCStats[*LocalDDCKey];
					LocalHits =
						LocalDDCStats.GetStats.GetAccumulatedValue(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Counter, true) +
						LocalDDCStats.GetStats.GetAccumulatedValue(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Counter, false);
				}
				int64 SharedHits = 0;
				if (SharedDDCKey)
				{
					// The shared DDC is only queried if the local one misses (or there isn't one). So it's hit rate is technically 
					const FDerivedDataCacheUsageStats& SharedDDCStats = DDCStats[*SharedDDCKey];
					SharedHits =
						SharedDDCStats.GetStats.GetAccumulatedValue(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Counter, true) +
						SharedDDCStats.GetStats.GetAccumulatedValue(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Counter, false);
				}

				int64 TotalPutHits =
					RootStats.PutStats.GetAccumulatedValue(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Counter, true) +
					RootStats.PutStats.GetAccumulatedValue(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Counter, false);
				int64 TotalPutMisses =
					RootStats.PutStats.GetAccumulatedValue(FCookStats::CallStats::EHitOrMiss::Miss, FCookStats::CallStats::EStatType::Counter, true) +
					RootStats.PutStats.GetAccumulatedValue(FCookStats::CallStats::EHitOrMiss::Miss, FCookStats::CallStats::EStatType::Counter, false);
				int64 TotalPuts = TotalPutHits + TotalPutMisses;

				AddStat(TEXT("DDC.Summary"), FCookStatsManager::CreateKeyValueArray(
					TEXT("TotalGetHits"), TotalGetHits,
					TEXT("TotalGets"), TotalGets,
					TEXT("TotalGetHitPct"), SafeDivide(TotalGetHits, TotalGets),
					TEXT("LocalGetHitPct"), SafeDivide(LocalHits, TotalGets),
					TEXT("SharedGetHitPct"), SafeDivide(SharedHits, TotalGets),
					TEXT("OtherGetHitPct"), SafeDivide((TotalGetHits - LocalHits - SharedHits), TotalGets),
					TEXT("GetMissPct"), SafeDivide(TotalGetMisses, TotalGets),
					TEXT("TotalPutHits"), TotalPutHits,
					TEXT("TotalPuts"), TotalPuts,
					TEXT("TotalPutHitPct"), SafeDivide(TotalPutHits, TotalPuts),
					TEXT("PutMissPct"), SafeDivide(TotalPutMisses, TotalPuts)
					));
			}
		}
	});
}
#endif

/** Whether we want to verify the DDC (pass in -VerifyDDC on the command line)*/
bool GVerifyDDC = false;

/**
 * Implementation of the derived data cache
 * This API is fully threadsafe
**/
class FDerivedDataCache : public FDerivedDataCacheInterface
{

	/** 
	 * Async worker that checks the cache backend and if that fails, calls the deriver to build the data and then puts the results to the cache
	**/
	friend class FBuildAsyncWorker;
	class FBuildAsyncWorker : public FNonAbandonableTask
	{
	public:
		/** 
		 * Constructor for async task 
		 * @param	InDataDeriver	plugin to produce cache key and in the event of a miss, return the data.
		 * @param	InCacheKey		Complete cache key for this data.
		**/
		FBuildAsyncWorker(FDerivedDataPluginInterface* InDataDeriver, const TCHAR* InCacheKey, bool bInSynchronousForStats)
		: bSuccess(false)
		, bSynchronousForStats(bInSynchronousForStats)
		, bDataWasBuilt(false)
		, DataDeriver(InDataDeriver)
		, CacheKey(InCacheKey)
		{
		}
		
		/** Async worker that checks the cache backend and if that fails, calls the deriver to build the data and then puts the results to the cache **/
		void DoWork()
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(DDC_DoWork);

			const int32 NumBeforeDDC = Data.Num();
			bool bGetResult;
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(DDC_Get);

				INC_DWORD_STAT(STAT_DDC_NumGets);
				STAT(double ThisTime = 0);
				{
					SCOPE_SECONDS_COUNTER(ThisTime);
					bGetResult = FDerivedDataBackend::Get().GetRoot().GetCachedData(*CacheKey, Data);
				}
				INC_FLOAT_STAT_BY(STAT_DDC_SyncGetTime, bSynchronousForStats ? (float)ThisTime : 0.0f);
			}
			if (bGetResult)
			{
				
				if(GVerifyDDC && DataDeriver && DataDeriver->IsDeterministic())
				{
					TArray<uint8> CmpData;
					DataDeriver->Build(CmpData);
					const int32 NumInDDC = Data.Num() - NumBeforeDDC;
					const int32 NumGenerated = CmpData.Num();
					
					bool bMatchesInSize = NumGenerated == NumInDDC;
					bool bDifferentMemory = true;
					int32 DifferentOffset = 0;
					if (bMatchesInSize)
					{
						bDifferentMemory = false;
						for (int32 i = 0; i < NumGenerated; i++)
						{
							if (CmpData[i] != Data[i])
							{
								bDifferentMemory = true;
								DifferentOffset = i;
								break;
							}
						}
					}

					if(!bMatchesInSize || bDifferentMemory)
					{
						FString ErrMsg = FString::Printf(TEXT("There is a mismatch between the DDC data and the generated data for plugin (%s) for asset (%s). BytesInDDC:%d, BytesGenerated:%d, bDifferentMemory:%d, offset:%d"), DataDeriver->GetPluginName(), *DataDeriver->GetDebugContextString(), NumInDDC, NumGenerated, bDifferentMemory, DifferentOffset);
						ensureMsgf(false, TEXT("%s"), *ErrMsg);
						UE_LOG(LogDerivedDataCache, Error, TEXT("%s"), *ErrMsg );
					}
					
				}

				check(Data.Num());
				bSuccess = true;
				delete DataDeriver;
				DataDeriver = NULL;
			}
			else if (DataDeriver)
			{
				{
					TRACE_CPUPROFILER_EVENT_SCOPE(DDC_Build);

					INC_DWORD_STAT(STAT_DDC_NumBuilds);
					STAT(double ThisTime = 0);
					{
						SCOPE_SECONDS_COUNTER(ThisTime);
						bSuccess = DataDeriver->Build(Data);
						bDataWasBuilt = true;
					}
					INC_FLOAT_STAT_BY(STAT_DDC_SyncBuildTime, bSynchronousForStats ? (float)ThisTime : 0.0f);
				}
				delete DataDeriver;
				DataDeriver = NULL;
				if (bSuccess)
				{
					check(Data.Num());

					TRACE_CPUPROFILER_EVENT_SCOPE(DDC_Put);

					INC_DWORD_STAT(STAT_DDC_NumPuts);
					STAT(double ThisTime = 0);
					{
						SCOPE_SECONDS_COUNTER(ThisTime);
						FDerivedDataBackend::Get().GetRoot().PutCachedData(*CacheKey, Data, true);
					}
					INC_FLOAT_STAT_BY(STAT_DDC_PutTime, bSynchronousForStats ? (float)ThisTime : 0.0f);
				}
			}
			if (!bSuccess)
			{
				Data.Empty();
			}
			FDerivedDataBackend::Get().AddToAsyncCompletionCounter(-1);
		}

		FORCEINLINE TStatId GetStatId() const
		{
			RETURN_QUICK_DECLARE_CYCLE_STAT(FBuildAsyncWorker, STATGROUP_ThreadPoolAsyncTasks);
		}

		/** true in the case of a cache hit, otherwise the result of the deriver build call **/
		bool							bSuccess;
		/** true if we should record the timing **/
		bool							bSynchronousForStats;
		/** true if we had to build the data */
		bool							bDataWasBuilt;
		/** Data dervier we are operating on **/
		FDerivedDataPluginInterface*	DataDeriver;
		/** Cache key associated with this build **/
		FString							CacheKey;
		/** Data to return to caller, later **/
		TArray<uint8>					Data;
	};

public:

	/** Constructor, called once to cereate a singleton **/
	FDerivedDataCache()
		: CurrentHandle(19248) // we will skip some potential handles to catch errors
	{
		FDerivedDataBackend::Get(); // we need to make sure this starts before we all us to start

		GVerifyDDC = FParse::Param(FCommandLine::Get(), TEXT("VerifyDDC"));
	}

	/** Destructor, flushes all sync tasks **/
	~FDerivedDataCache()
	{
		FScopeLock ScopeLock(&SynchronizationObject);
		for (TMap<uint32,FAsyncTask<FBuildAsyncWorker>*>::TIterator It(PendingTasks); It; ++It)
		{
			It.Value()->EnsureCompletion();
			delete It.Value();
		}
		PendingTasks.Empty();
	}

	virtual bool GetSynchronous(FDerivedDataPluginInterface* DataDeriver, TArray<uint8>& OutData, bool* bDataWasBuilt = nullptr) override
	{
		DDC_SCOPE_CYCLE_COUNTER(DDC_GetSynchronous);
		check(DataDeriver);
		FString CacheKey = FDerivedDataCache::BuildCacheKey(DataDeriver);
		UE_LOG(LogDerivedDataCache, Verbose, TEXT("GetSynchronous %s"), *CacheKey);
		FAsyncTask<FBuildAsyncWorker> PendingTask(DataDeriver, *CacheKey, true);
		AddToAsyncCompletionCounter(1);
		PendingTask.StartSynchronousTask();
		OutData = PendingTask.GetTask().Data;
		if (bDataWasBuilt)
		{
			*bDataWasBuilt = PendingTask.GetTask().bDataWasBuilt;
		}
		return PendingTask.GetTask().bSuccess;
	}

	virtual uint32 GetAsynchronous(FDerivedDataPluginInterface* DataDeriver) override
	{
		DDC_SCOPE_CYCLE_COUNTER(DDC_GetAsynchronous);
		FScopeLock ScopeLock(&SynchronizationObject);
		const uint32 Handle = NextHandle();
		FString CacheKey = FDerivedDataCache::BuildCacheKey(DataDeriver);
		UE_LOG(LogDerivedDataCache, Verbose, TEXT("GetAsynchronous %s"), *CacheKey);
		const bool bSync = !DataDeriver->IsBuildThreadsafe();
		FAsyncTask<FBuildAsyncWorker>* AsyncTask = new FAsyncTask<FBuildAsyncWorker>(DataDeriver, *CacheKey, bSync);
		check(!PendingTasks.Contains(Handle));
		PendingTasks.Add(Handle,AsyncTask);
		AddToAsyncCompletionCounter(1);
		if (!bSync)
		{
			AsyncTask->StartBackgroundTask();
		}
		else
		{
			AsyncTask->StartSynchronousTask();
		}
		// Must return a valid handle
		check(Handle != 0);
		return Handle;
	}

	virtual bool PollAsynchronousCompletion(uint32 Handle) override
	{
		DDC_SCOPE_CYCLE_COUNTER(DDC_PollAsynchronousCompletion);
		FAsyncTask<FBuildAsyncWorker>* AsyncTask = NULL;
		{
			FScopeLock ScopeLock(&SynchronizationObject);
			AsyncTask = PendingTasks.FindRef(Handle);
		}
		check(AsyncTask);
		return AsyncTask->IsDone();
	}

	virtual void WaitAsynchronousCompletion(uint32 Handle) override
	{
		DDC_SCOPE_CYCLE_COUNTER(DDC_WaitAsynchronousCompletion);
		STAT(double ThisTime = 0);
		{
			SCOPE_SECONDS_COUNTER(ThisTime);
			FAsyncTask<FBuildAsyncWorker>* AsyncTask = NULL;
			{
				FScopeLock ScopeLock(&SynchronizationObject);
				AsyncTask = PendingTasks.FindRef(Handle);
			}
			check(AsyncTask);
			AsyncTask->EnsureCompletion();
		}
		INC_FLOAT_STAT_BY(STAT_DDC_ASyncWaitTime,(float)ThisTime);
	}

	virtual bool GetAsynchronousResults(uint32 Handle, TArray<uint8>& OutData, bool* bDataWasBuilt = nullptr) override
	{
		DDC_SCOPE_CYCLE_COUNTER(DDC_GetAsynchronousResults);
		FAsyncTask<FBuildAsyncWorker>* AsyncTask = NULL;
		{
			FScopeLock ScopeLock(&SynchronizationObject);
			PendingTasks.RemoveAndCopyValue(Handle,AsyncTask);
		}
		check(AsyncTask);
		if (bDataWasBuilt)
		{
			*bDataWasBuilt = AsyncTask->GetTask().bDataWasBuilt;
		}
		if (!AsyncTask->GetTask().bSuccess)
		{
			delete AsyncTask;
			return false;
		}
		OutData = MoveTemp(AsyncTask->GetTask().Data);
		delete AsyncTask;
		check(OutData.Num());
		return true;
	}

	virtual bool GetSynchronous(const TCHAR* CacheKey, TArray<uint8>& OutData) override
	{
		DDC_SCOPE_CYCLE_COUNTER(DDC_GetSynchronous_Data);
		UE_LOG(LogDerivedDataCache, Verbose, TEXT("GetSynchronous %s"), CacheKey);
		FAsyncTask<FBuildAsyncWorker> PendingTask((FDerivedDataPluginInterface*)NULL, CacheKey, true);
		AddToAsyncCompletionCounter(1);
		PendingTask.StartSynchronousTask();
		OutData = PendingTask.GetTask().Data;
		return PendingTask.GetTask().bSuccess;
	}

	virtual uint32 GetAsynchronous(const TCHAR* CacheKey) override
	{
		DDC_SCOPE_CYCLE_COUNTER(DDC_GetAsynchronous_Handle);
		FScopeLock ScopeLock(&SynchronizationObject);
		UE_LOG(LogDerivedDataCache, Verbose, TEXT("GetAsynchronous %s"), CacheKey);
		const uint32 Handle = NextHandle();
		FAsyncTask<FBuildAsyncWorker>* AsyncTask = new FAsyncTask<FBuildAsyncWorker>((FDerivedDataPluginInterface*)NULL, CacheKey, false);
		check(!PendingTasks.Contains(Handle));
		PendingTasks.Add(Handle, AsyncTask);
		AddToAsyncCompletionCounter(1);
		AsyncTask->StartBackgroundTask();
		return Handle;
	}

	virtual void Put(const TCHAR* CacheKey, TArray<uint8>& Data, bool bPutEvenIfExists = false) override
	{
		DDC_SCOPE_CYCLE_COUNTER(DDC_Put);
		STAT(double ThisTime = 0);
		{
			SCOPE_SECONDS_COUNTER(ThisTime);
			FDerivedDataBackend::Get().GetRoot().PutCachedData(CacheKey, Data, bPutEvenIfExists);
		}
		INC_FLOAT_STAT_BY(STAT_DDC_PutTime,(float)ThisTime);
		INC_DWORD_STAT(STAT_DDC_NumPuts);
	}

	virtual void MarkTransient(const TCHAR* CacheKey) override
	{
		FDerivedDataBackend::Get().GetRoot().RemoveCachedData(CacheKey, /*bTransient=*/ true);
	}

	virtual bool CachedDataProbablyExists(const TCHAR* CacheKey) override
	{
		DDC_SCOPE_CYCLE_COUNTER(DDC_CachedDataProbablyExists);
		bool bResult;
		INC_DWORD_STAT(STAT_DDC_NumExist);
		STAT(double ThisTime = 0);
		{
			SCOPE_SECONDS_COUNTER(ThisTime);
			bResult = FDerivedDataBackend::Get().GetRoot().CachedDataProbablyExists(CacheKey);
		}
		INC_FLOAT_STAT_BY(STAT_DDC_ExistTime, (float)ThisTime);
		return bResult;
	}

	void NotifyBootComplete() override
	{
		DDC_SCOPE_CYCLE_COUNTER(DDC_NotifyBootComplete);
		FDerivedDataBackend::Get().NotifyBootComplete();
	}

	void AddToAsyncCompletionCounter(int32 Addend) override
	{
		FDerivedDataBackend::Get().AddToAsyncCompletionCounter(Addend);
	}

	void WaitForQuiescence(bool bShutdown) override
	{
		DDC_SCOPE_CYCLE_COUNTER(DDC_WaitForQuiescence);
		FDerivedDataBackend::Get().WaitForQuiescence(bShutdown);
	}

	/** Get whether a Shared Data Cache is in use */
	virtual bool GetUsingSharedDDC() const override
	{		
		return FDerivedDataBackend::Get().GetUsingSharedDDC();
	}

	void GetDirectories(TArray<FString>& OutResults) override
	{
		FDerivedDataBackend::Get().GetDirectories(OutResults);
	}

	virtual void GatherUsageStats(TMap<FString, FDerivedDataCacheUsageStats>& UsageStatsMap) override
	{
		FDerivedDataBackend::Get().GatherUsageStats(UsageStatsMap);
	}

	/** Get event delegate for data cache notifications */
	virtual FOnDDCNotification& GetDDCNotificationEvent()
	{
		return DDCNotificationEvent;
	}

protected:
	uint32 NextHandle()
	{
		return (uint32)CurrentHandle.Increment();
	}


private:

	/** 
	 * Internal function to build a cache key out of the plugin name, versions and plugin specific info
	 * @param	DataDeriver	plugin to produce the elements of the cache key.
	 * @return				Assembled cache key
	**/
	static FString BuildCacheKey(FDerivedDataPluginInterface* DataDeriver)
	{
		FString Result = FDerivedDataCacheInterface::BuildCacheKey(DataDeriver->GetPluginName(), DataDeriver->GetVersionString(), *DataDeriver->GetPluginSpecificCacheKeySuffix());
		return Result;
	}


	/** Counter used to produce unique handles **/
	FThreadSafeCounter			CurrentHandle;
	/** Object used for synchronization via a scoped lock **/
	FCriticalSection			SynchronizationObject;
	/** Map of handle to pending task **/
	TMap<uint32,FAsyncTask<FBuildAsyncWorker>*>	PendingTasks;

	/** Cache notification delegate */
	FOnDDCNotification DDCNotificationEvent;
};

/**
 * Module for the DDC
 */
class FDerivedDataCacheModule : public IDerivedDataCacheModule
{
public:
	virtual FDerivedDataCacheInterface& GetDDC() override
	{
		static FDerivedDataCache SingletonInstance;
		return SingletonInstance;
	}

	virtual void StartupModule() override
	{
		GetDDC();
	}

	virtual void ShutdownModule() override
	{
		FDDCCleanup::Shutdown();
	}
};

IMPLEMENT_MODULE( FDerivedDataCacheModule, DerivedDataCache);

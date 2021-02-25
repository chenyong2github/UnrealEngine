// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataCache.h"
#include "DerivedDataCacheInterface.h"

#include "CoreMinimal.h"
#include "Algo/AllOf.h"
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

#include "DerivedDataBackendInterface.h"
#include "DerivedDataPluginInterface.h"
#include "DDCCleanup.h"
#include "ProfilingDebugging/CookStats.h"

#include "Algo/AllOf.h"
#include "Algo/Transform.h"
#include "DerivedDataBackendInterface.h"
#include "Misc/CoreMisc.h"
#include "Misc/ScopeExit.h"
#include "Misc/StringBuilder.h"
#include "Misc/CommandLine.h"
#include "Serialization/CompactBinary.h"
#include "Serialization/CompactBinaryValidation.h"
#include "Serialization/CompactBinaryWriter.h"

#include <atomic>

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

	// AddCookStats cannot be a lambda because of false positives in static analysis.
	// See https://developercommunity.visualstudio.com/content/problem/576913/c6244-regression-in-new-lambda-processorpermissive.html
	static void AddCookStats(FCookStatsManager::AddStatFuncRef AddStat)
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
			// look for a Cloud path
			FString* CloudDDCKey = Keys.FindByPredicate([](const FString& Key) {return Key.Contains(TEXT("0: HTTP")); });

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
				int64 CloudHits = 0;
				if (CloudDDCKey)
				{
					const FDerivedDataCacheUsageStats& CloudDDCStats = DDCStats[*CloudDDCKey];
					CloudHits =
						CloudDDCStats.GetStats.GetAccumulatedValue(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Counter, true) +
						CloudDDCStats.GetStats.GetAccumulatedValue(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Counter, false);
				}

				int64 TotalPutHits =
					RootStats.PutStats.GetAccumulatedValue(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Counter, true) +
					RootStats.PutStats.GetAccumulatedValue(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Counter, false);
				int64 TotalPutMisses =
					RootStats.PutStats.GetAccumulatedValue(FCookStats::CallStats::EHitOrMiss::Miss, FCookStats::CallStats::EStatType::Counter, true) +
					RootStats.PutStats.GetAccumulatedValue(FCookStats::CallStats::EHitOrMiss::Miss, FCookStats::CallStats::EStatType::Counter, false);
				int64 TotalPuts = TotalPutHits + TotalPutMisses;

				AddStat(TEXT("DDC.Summary"), FCookStatsManager::CreateKeyValueArray(
					TEXT("BackEnd"), FDerivedDataBackend::Get().GetGraphName(),
					TEXT("HasLocalCache"), LocalDDCKey != nullptr,
					TEXT("HasSharedCache"), SharedDDCKey!=nullptr,
					TEXT("HasCloudCache"), CloudDDCKey !=nullptr,
					TEXT("TotalGetHits"), TotalGetHits,
					TEXT("TotalGets"), TotalGets,
					TEXT("TotalGetHitPct"), SafeDivide(TotalGetHits, TotalGets),
					TEXT("LocalGetHitPct"), SafeDivide(LocalHits, TotalGets),
					TEXT("SharedGetHitPct"), SafeDivide(SharedHits, TotalGets),
					TEXT("CloudGetHitPct"), SafeDivide(CloudHits, TotalGets),
					TEXT("OtherGetHitPct"), SafeDivide((TotalGetHits - LocalHits - SharedHits), TotalGets),
					TEXT("GetMissPct"), SafeDivide(TotalGetMisses, TotalGets),
					TEXT("TotalPutHits"), TotalPutHits,
					TEXT("TotalPuts"), TotalPuts,
					TEXT("TotalPutHitPct"), SafeDivide(TotalPutHits, TotalPuts),
					TEXT("PutMissPct"), SafeDivide(TotalPutMisses, TotalPuts)
					));
			}
		}
	}

	FCookStatsManager::FAutoRegisterCallback RegisterCookStats(AddCookStats);
}
#endif

namespace UE
{
namespace DerivedData
{

ICache* CreateCache();

} // DerivedData
} // UE

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
		enum EWorkerState : uint32
		{
			WorkerStateNone			= 0,
			WorkerStateRunning		= 1 << 0,
			WorkerStateFinished		= 1 << 1,
			WorkerStateDestroyed	= 1 << 2,
		};

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

		virtual ~FBuildAsyncWorker()
		{
			// Record that the task is destroyed and check that it was not running or destroyed previously.
			{
				const uint32 PreviousState = WorkerState.fetch_or(WorkerStateDestroyed, std::memory_order_relaxed);
				checkf(!(PreviousState & WorkerStateRunning), TEXT("Destroying DDC worker that is still running! Key: %s"), *CacheKey);
				checkf(!(PreviousState & WorkerStateDestroyed), TEXT("Destroying DDC worker that has been destroyed previously! Key: %s"), *CacheKey);
			}
		}

		/** Async worker that checks the cache backend and if that fails, calls the deriver to build the data and then puts the results to the cache **/
		void DoWork()
		{
			// Record that the task is running and check that it was not running, finished, or destroyed previously.
			{
				const uint32 PreviousState = WorkerState.fetch_or(WorkerStateRunning, std::memory_order_relaxed);
				checkf(!(PreviousState & WorkerStateRunning), TEXT("Starting DDC worker that is already running! Key: %s"), *CacheKey);
				checkf(!(PreviousState & WorkerStateFinished), TEXT("Starting DDC worker that is already finished! Key: %s"), *CacheKey);
				checkf(!(PreviousState & WorkerStateDestroyed), TEXT("Starting DDC worker that has been destroyed! Key: %s"), *CacheKey);
			}

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

			// Record that the task is finished and check that it was running and not finished or destroyed previously.
			{
				const uint32 PreviousState = WorkerState.fetch_xor(WorkerStateRunning | WorkerStateFinished, std::memory_order_relaxed);
				checkf((PreviousState & WorkerStateRunning), TEXT("Finishing DDC worker that was not running! Key: %s"), *CacheKey);
				checkf(!(PreviousState & WorkerStateFinished), TEXT("Finishing DDC worker that is already finished! Key: %s"), *CacheKey);
				checkf(!(PreviousState & WorkerStateDestroyed), TEXT("Finishing DDC worker that has been destroyed! Key: %s"), *CacheKey);
			}
		}

		FORCEINLINE TStatId GetStatId() const
		{
			RETURN_QUICK_DECLARE_CYCLE_STAT(FBuildAsyncWorker, STATGROUP_ThreadPoolAsyncTasks);
		}

		std::atomic<uint32>				WorkerState{WorkerStateNone};
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
		, Cache(UE::DerivedData::CreateCache())
	{
		FDerivedDataBackend::Get(); // we need to make sure this starts before we all us to start

		GVerifyDDC = FParse::Param(FCommandLine::Get(), TEXT("VerifyDDC"));

		UE_CLOG(GVerifyDDC, LogDerivedDataCache, Display, TEXT("Items retrieved from the DDC will be verified (-VerifyDDC)"));
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

	virtual UE::DerivedData::ICache& GetCache()
	{
		return *Cache;
	}

	virtual bool GetSynchronous(FDerivedDataPluginInterface* DataDeriver, TArray<uint8>& OutData, bool* bDataWasBuilt = nullptr) override
	{
		DDC_SCOPE_CYCLE_COUNTER(DDC_GetSynchronous);
		check(DataDeriver);
		FString CacheKey = FDerivedDataCache::BuildCacheKey(DataDeriver);
		UE_LOG(LogDerivedDataCache, VeryVerbose, TEXT("GetSynchronous %s from '%s'"), *CacheKey, *DataDeriver->GetDebugContextString());
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
		UE_LOG(LogDerivedDataCache, VeryVerbose, TEXT("GetAsynchronous %s from '%s', Handle %d"), *CacheKey, *DataDeriver->GetDebugContextString(), Handle);
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
			UE_LOG(LogDerivedDataCache, Verbose, TEXT("WaitAsynchronousCompletion, Handle %d"), Handle);
		}
		INC_FLOAT_STAT_BY(STAT_DDC_ASyncWaitTime,(float)ThisTime);
	}

	virtual bool GetAsynchronousResults(uint32 Handle, TArray<uint8>& OutData, bool* bOutDataWasBuilt = nullptr) override
	{
		DDC_SCOPE_CYCLE_COUNTER(DDC_GetAsynchronousResults);
		FAsyncTask<FBuildAsyncWorker>* AsyncTask = NULL;
		{
			FScopeLock ScopeLock(&SynchronizationObject);
			PendingTasks.RemoveAndCopyValue(Handle,AsyncTask);
		}
		check(AsyncTask);
		const bool bDataWasBuilt = AsyncTask->GetTask().bDataWasBuilt;
		if (bOutDataWasBuilt)
		{
			*bOutDataWasBuilt = bDataWasBuilt;
		}
		if (!AsyncTask->GetTask().bSuccess)
		{
			UE_LOG(LogDerivedDataCache, Verbose, TEXT("GetAsynchronousResults, bDataWasBuilt: %d, Handle %d, FAILED"), (int32)bDataWasBuilt, Handle);
			delete AsyncTask;
			return false;
		}

		UE_LOG(LogDerivedDataCache, Verbose, TEXT("GetAsynchronousResults, bDataWasBuilt: %d, Handle %d, SUCCESS"), (int32)bDataWasBuilt, Handle);
		OutData = MoveTemp(AsyncTask->GetTask().Data);
		delete AsyncTask;
		check(OutData.Num());
		return true;
	}

	virtual bool GetSynchronous(const TCHAR* CacheKey, TArray<uint8>& OutData, FStringView DataContext) override
	{
		DDC_SCOPE_CYCLE_COUNTER(DDC_GetSynchronous_Data);
		UE_LOG(LogDerivedDataCache, VeryVerbose, TEXT("GetSynchronous %s from '%.*s'"), CacheKey, DataContext.Len(), DataContext.GetData());
		ValidateCacheKey(CacheKey);
		FAsyncTask<FBuildAsyncWorker> PendingTask((FDerivedDataPluginInterface*)NULL, CacheKey, true);
		AddToAsyncCompletionCounter(1);
		PendingTask.StartSynchronousTask();
		OutData = PendingTask.GetTask().Data;
		return PendingTask.GetTask().bSuccess;
	}

	virtual uint32 GetAsynchronous(const TCHAR* CacheKey, FStringView DataContext) override
	{
		DDC_SCOPE_CYCLE_COUNTER(DDC_GetAsynchronous_Handle);
		FScopeLock ScopeLock(&SynchronizationObject);
		const uint32 Handle = NextHandle();
		UE_LOG(LogDerivedDataCache, VeryVerbose, TEXT("GetAsynchronous %s from '%.*s', Handle %d"), CacheKey, DataContext.Len(), DataContext.GetData(), Handle);
		ValidateCacheKey(CacheKey);
		FAsyncTask<FBuildAsyncWorker>* AsyncTask = new FAsyncTask<FBuildAsyncWorker>((FDerivedDataPluginInterface*)NULL, CacheKey, false);
		check(!PendingTasks.Contains(Handle));
		PendingTasks.Add(Handle, AsyncTask);
		AddToAsyncCompletionCounter(1);
		// This request is I/O only, doesn't do any processing, send it to the I/O only thread-pool to avoid wasting worker threads on long I/O waits.
		AsyncTask->StartBackgroundTask(GDDCIOThreadPool);
		return Handle;
	}

	virtual void Put(const TCHAR* CacheKey, TArrayView<const uint8> Data, FStringView DataContext, bool bPutEvenIfExists = false) override
	{
		DDC_SCOPE_CYCLE_COUNTER(DDC_Put);
		UE_LOG(LogDerivedDataCache, VeryVerbose, TEXT("Put %s from '%.*s'"), CacheKey, DataContext.Len(), DataContext.GetData());
		ValidateCacheKey(CacheKey);
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
		ValidateCacheKey(CacheKey);
		FDerivedDataBackend::Get().GetRoot().RemoveCachedData(CacheKey, /*bTransient=*/ true);
	}

	virtual bool CachedDataProbablyExists(const TCHAR* CacheKey) override
	{
		DDC_SCOPE_CYCLE_COUNTER(DDC_CachedDataProbablyExists);
		ValidateCacheKey(CacheKey);
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

	virtual TBitArray<> CachedDataProbablyExistsBatch(TConstArrayView<FString> CacheKeys) override
	{
		TBitArray<> Result;
		if (CacheKeys.Num() > 1)
		{
			DDC_SCOPE_CYCLE_COUNTER(DDC_CachedDataProbablyExistsBatch);
			INC_DWORD_STAT(STAT_DDC_NumExist);
			STAT(double ThisTime = 0);
			{
				SCOPE_SECONDS_COUNTER(ThisTime);
				Result = FDerivedDataBackend::Get().GetRoot().CachedDataProbablyExistsBatch(CacheKeys);
				check(Result.Num() == CacheKeys.Num());
			}
			INC_FLOAT_STAT_BY(STAT_DDC_ExistTime, (float)ThisTime);
		}
		else if (CacheKeys.Num() == 1)
		{
			Result.Add(CachedDataProbablyExists(*CacheKeys[0]));
		}
		return Result;
	}

	virtual bool AllCachedDataProbablyExists(TConstArrayView<FString> CacheKeys) override
	{
		return CacheKeys.Num() == 0 || CachedDataProbablyExistsBatch(CacheKeys).CountSetBits() == CacheKeys.Num();
	}

	virtual bool TryToPrefetch(TConstArrayView<FString> CacheKeys, FStringView DebugContext) override
	{
		if (!CacheKeys.IsEmpty())
		{
			DDC_SCOPE_CYCLE_COUNTER(DDC_TryToPrefetch);
			UE_LOG(LogDerivedDataCache, VeryVerbose, TEXT("TryToPrefetch %d keys including %s from '%.*s'"),
				CacheKeys.Num(), *CacheKeys[0], DebugContext.Len(), DebugContext.GetData());
			return FDerivedDataBackend::Get().GetRoot().TryToPrefetch(CacheKeys);
		}
		return true;
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

	virtual const TCHAR* GetGraphName() const override
	{
		return FDerivedDataBackend::Get().GetGraphName();
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

	static void ValidateCacheKey(const TCHAR* CacheKey)
	{
		checkf(Algo::AllOf(FStringView(CacheKey), [](TCHAR C) { return FChar::IsAlnum(C) || FChar::IsUnderscore(C) || C == TEXT('$'); }),
			TEXT("Invalid characters in cache key %s. Use SanitizeCacheKey or BuildCacheKey to create valid keys."), CacheKey);
	}

	/** Counter used to produce unique handles **/
	FThreadSafeCounter			CurrentHandle;
	/** Object used for synchronization via a scoped lock **/
	FCriticalSection			SynchronizationObject;
	/** Map of handle to pending task **/
	TMap<uint32,FAsyncTask<FBuildAsyncWorker>*>	PendingTasks;

	/** Cache notification delegate */
	FOnDDCNotification DDCNotificationEvent;

	TUniquePtr<UE::DerivedData::ICache> Cache;
};

static FDerivedDataCacheInterface* GDerivedDataCacheInstance;

PRAGMA_DISABLE_DEPRECATION_WARNINGS

/**
 * Module for the DDC
 */
class FDerivedDataCacheModule final : public IDerivedDataCacheModule
{
public:
	virtual FDerivedDataCacheInterface& GetDDC() final
	{
		return **CreateOrGetDDC();
	}

	virtual FDerivedDataCacheInterface* const* CreateOrGetDDC() final
	{
		FScopeLock Lock(&CreateLock);
		if (!GDerivedDataCacheInstance)
		{
			GDerivedDataCacheInstance = new FDerivedDataCache();
			check(GDerivedDataCacheInstance);
		}
		return &GDerivedDataCacheInstance;
	}

	virtual void ShutdownModule() final
	{
		FDDCCleanup::Shutdown();

		delete GDerivedDataCacheInstance;
		GDerivedDataCacheInstance = nullptr;
	}

private:
	FCriticalSection CreateLock;
};

PRAGMA_ENABLE_DEPRECATION_WARNINGS

IMPLEMENT_MODULE(FDerivedDataCacheModule, DerivedDataCache);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace UE
{
namespace DerivedData
{

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FCache final : public ICache
{
public:
	virtual ~FCache() = default;

	virtual FRequest Put(
		TArrayView<FCacheRecord> Records,
		FStringView Context,
		ECachePolicy Policy,
		EPriority Priority,
		FOnCachePutComplete&& Callback) final;

	virtual FRequest Get(
		TConstArrayView<FCacheKey> Keys,
		FStringView Context,
		ECachePolicy Policy,
		EPriority Priority,
		FOnCacheGetComplete&& Callback) final;

	virtual FRequest GetPayloads(
		TConstArrayView<FCachePayloadKey> Keys,
		FStringView Context,
		ECachePolicy Policy,
		EPriority Priority,
		FOnCacheGetPayloadComplete&& Callback) final;

	virtual void CancelAll() final;

private:
	void Put(
		FCacheRecord Record,
		FStringView Context,
		ECachePolicy Policy,
		EPriority Priority,
		FOnCachePutComplete& Callback);

	void Get(
		const FCacheKey& Key,
		FStringView Context,
		ECachePolicy Policy,
		EPriority Priority,
		FOnCacheGetComplete& Callback);

	void GetPayload(
		const FCachePayloadKey& Key,
		FStringView Context,
		ECachePolicy Policy,
		EPriority Priority,
		FOnCacheGetPayloadComplete& Callback);

	template <int32 BufferSize>
	class TToString
	{
	public:
		template <typename T>
		explicit TToString(const T& Input)
		{
			Buffer << Input;
		}

		inline const TCHAR* operator*() const { return Buffer.ToString(); }
		inline const TCHAR* ToString() const { return Buffer.ToString(); }
		inline operator const TCHAR*() const { return Buffer.ToString(); }

	private:
		TStringBuilder<BufferSize> Buffer;
	};

	static FString MakeRecordKey(const FCacheKey& Key)
	{
		check(Key.IsValid());
		TStringBuilder<96> Out;
		Out << Key.GetBucket() << TEXT("_") << Key.GetHash();
		return FDerivedDataCacheInterface::SanitizeCacheKey(Out.ToString());
	}

	static FString MakePayloadKey(const FCachePayloadKey& PayloadKey)
	{
		check(PayloadKey.IsValid());
		TStringBuilder<96> Out;
		Out << PayloadKey.GetKey().GetBucket() << TEXT("_") << PayloadKey.GetKey().GetHash() << TEXT("_") << PayloadKey.GetId();
		return FDerivedDataCacheInterface::SanitizeCacheKey(Out.ToString());
	}
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FCache::Put(
	FCacheRecord Record,
	FStringView Context,
	ECachePolicy Policy,
	EPriority Priority,
	FOnCachePutComplete& Callback)
{
	FCachePutCompleteParams Params;
	Params.Key = Record.GetKey();

	ON_SCOPE_EXIT
	{
		if (Callback)
		{
			Callback(MoveTemp(Params));
		}
	};

	if (!EnumHasAnyFlags(Policy, ECachePolicy::Store))
	{
		UE_LOG(LogDerivedDataCache, VeryVerbose, TEXT("Cache: Put skipped for %s from '%.*s'"),
			*TToString<96>(Params.Key), Context.Len(), Context.GetData());
		Params.Status = ECacheStatus::NotCached;
		return;
	}

	const auto PutPayload = [&Key = Params.Key, Context](const FPayload& Payload)
	{
		const FSharedBuffer Buffer = Payload.GetBuffer().GetCompressed();
		check(Buffer && Buffer.GetSize() <= MAX_int32);
		TConstArrayView<uint8> BufferView = MakeArrayView(
			static_cast<const uint8*>(Buffer.GetData()),
			static_cast<int32>(Buffer.GetSize()));
		GetDerivedDataCacheRef().Put(*MakePayloadKey(FCachePayloadKey(Key, Payload.GetId())), BufferView, Context);
	};

	FCbWriter Writer;
	Writer.BeginObject();
	{
		if (const FPayload& Value = Record.GetValuePayload())
		{
			PutPayload(Value);
			Writer.BeginObject("Value"_ASV);
			Writer.AddObjectId("Id"_ASV, Value.GetId().ToObjectId());
			Writer.AddBinaryAttachment("Hash"_ASV, Value.GetHash());
			Writer.EndObject();
		}
		if (!Record.GetAttachmentPayloads().IsEmpty())
		{
			Writer.BeginArray("Attachments"_ASV);
			for (const FPayload& Attachment : Record.GetAttachmentPayloads())
			{
				PutPayload(Attachment);
				Writer.BeginObject();
				Writer.AddObjectId("Id"_ASV, Attachment.GetId().ToObjectId());
				Writer.AddBinaryAttachment("Hash"_ASV, Attachment.GetHash());
				Writer.EndObject();
			}
			Writer.EndArray();
		}

		const FCbObject& Meta = Record.GetMeta();
		if (Meta.CreateViewIterator())
		{
			Writer.AddObject("Meta"_ASV, Meta);
		}
	}
	Writer.EndObject();

	TArray<uint8> Data;
	const uint64 SaveSize = Writer.GetSaveSize();
	check(SaveSize <= MAX_int32);
	Data.SetNumUninitialized(static_cast<int32>(SaveSize));
	Writer.Save(MakeMemoryView(Data));

	GetDerivedDataCacheRef().Put(*MakeRecordKey(Params.Key), Data, Context);

	UE_LOG(LogDerivedDataCache, VeryVerbose, TEXT("Cache: Put for %s from '%.*s'"),
		*TToString<96>(Params.Key), Context.Len(), Context.GetData());
	Params.Status = ECacheStatus::Cached;
}

FRequest FCache::Put(
	TArrayView<FCacheRecord> Records,
	FStringView Context,
	ECachePolicy Policy,
	EPriority Priority,
	FOnCachePutComplete&& Callback)
{
	for (FCacheRecord& Record : Records)
	{
		Put(MoveTemp(Record), Context, Policy, Priority, Callback);
	}
	return FRequest();
}

void FCache::Get(
	const FCacheKey& Key,
	FStringView Context,
	ECachePolicy Policy,
	EPriority Priority,
	FOnCacheGetComplete& Callback)
{
	FPayload Value;
	TArray<FPayload> Attachments;
	TArray<FPayloadId, TInlineAllocator<1>> SkippedPayloads;
	FCacheRecordBuilder Builder;
	Builder.SetKey(Key);

	FCacheGetCompleteParams Params;
	ON_SCOPE_EXIT
	{
		if (Callback)
		{
			Params.Record = Builder.Build();
			Callback(MoveTemp(Params));
		}
	};

	// Skip the request if querying the cache is disabled.
	if (!EnumHasAnyFlags(Policy, ECachePolicy::Query))
	{
		UE_LOG(LogDerivedDataCache, VeryVerbose, TEXT("Cache: Get skipped for %s from '%.*s'"),
			*TToString<96>(Key), Context.Len(), Context.GetData());
		return;
	}

	// Request the record from storage.
	FSharedBuffer RecordBuffer;
	{
		TArray<uint8> RecordData;
		if (!GetDerivedDataCacheRef().GetSynchronous(*MakeRecordKey(Key), RecordData, Context))
		{
			UE_LOG(LogDerivedDataCache, Verbose, TEXT("Cache: Get cache miss for %s from '%.*s'"),
				*TToString<96>(Key), Context.Len(), Context.GetData());
			return;
		}
		RecordBuffer = MakeSharedBufferFromArray(MoveTemp(RecordData));
	}

	// Validate that the record can be read as compact binary without crashing.
	if (ValidateCompactBinaryRange(RecordBuffer, ECbValidateMode::Default) != ECbValidateError::None)
	{
		UE_LOG(LogDerivedDataCache, Verbose, TEXT("Cache: Get cache miss with corrupted record for %s from '%.*s'"),
			*TToString<96>(Key), Context.Len(), Context.GetData());
		return;
	}

	// Read the record from its compact binary fields.
	const FCbObject RecordObject(MoveTemp(RecordBuffer));
	const FCbObject MetaObject = RecordObject.Find("Meta"_ASV).AsObject();
	const FCbObjectView ValueObject = RecordObject["Value"_ASV].AsObjectView();
	const FCbArrayView AttachmentsArray = RecordObject["Attachments"_ASV].AsArrayView();

	// Read the value and attachments if they have been requested.
	const auto GetPayload = [&SkippedPayloads, &Key, Policy, Context](FCbObjectView PayloadObject, ECachePolicy SkipFlag) -> FPayload
	{
		TArray<uint8> PayloadData;
		const FPayloadId PayloadId = FPayloadId::FromObjectId(PayloadObject["Id"_ASV].AsObjectId());
		const FIoHash PayloadHash = PayloadObject["Hash"_ASV].AsBinaryAttachment();
		const FCachePayloadKey PayloadKey(Key, PayloadId);
		if (EnumHasAnyFlags(Policy, SkipFlag))
		{
			SkippedPayloads.Add(PayloadId);
			return FPayload(PayloadId, PayloadHash);
		}
		else if (GetDerivedDataCacheRef().GetSynchronous(*MakePayloadKey(PayloadKey), PayloadData, Context))
		{
			if (FIoHash::HashBuffer(MakeMemoryView(PayloadData)) == PayloadHash)
			{
				return FPayload(PayloadId, PayloadHash, FCompressedBuffer(MakeSharedBufferFromArray(MoveTemp(PayloadData))));
			}
			else
			{
				UE_LOG(LogDerivedDataCache, Verbose, TEXT("Cache: Get cache miss with corrupted content for %s from '%.*s'"),
					*TToString<96>(PayloadKey), Context.Len(), Context.GetData());
				return FPayload();
			}
		}
		else
		{
			UE_LOG(LogDerivedDataCache, Verbose, TEXT("Cache: Get cache miss with missing content for %s from '%.*s'"),
				*TToString<96>(PayloadKey), Context.Len(), Context.GetData());
			return FPayload();
		}
	};

	if (ValueObject.CreateViewIterator() && !(Value = GetPayload(ValueObject, ECachePolicy::SkipValue)))
	{
		return;
	}

	Attachments.Reserve(AttachmentsArray.Num());
	for (FCbFieldView AttachmentField : AttachmentsArray)
	{
		Attachments.Add(GetPayload(AttachmentField.AsObjectView(), ECachePolicy::SkipAttachments));
		if (!Attachments.Last())
		{
			return;
		}
	}

	// Check for existence of the value and attachments if they were skipped.
	if (!SkippedPayloads.IsEmpty())
	{
		TArray<FString, TInlineAllocator<1>> SkippedPayloadKeys;
		SkippedPayloadKeys.Reserve(SkippedPayloads.Num());
		Algo::Transform(SkippedPayloads, SkippedPayloadKeys,
			[&Key](const FPayloadId& Id) -> FString { return MakePayloadKey(FCachePayloadKey(Key, Id)); });
		const bool bPayloadsExist = EnumHasAnyFlags(Policy, ECachePolicy::StoreLocal)
			? GetDerivedDataCacheRef().TryToPrefetch(SkippedPayloadKeys, Context)
			: GetDerivedDataCacheRef().AllCachedDataProbablyExists(SkippedPayloadKeys);
		if (!bPayloadsExist)
		{
			UE_LOG(LogDerivedDataCache, Verbose, TEXT("Cache: Get cache miss with missing content for %s from '%.*s'"),
				*TToString<96>(Key), Context.Len(), Context.GetData());
			return;
		}
	}

	// Populate the builder now that any error conditions have been handled.
	if (!EnumHasAnyFlags(Policy, ECachePolicy::SkipMeta) && MetaObject.CreateViewIterator())
	{
		Builder.SetMeta(MetaObject);
	}
	if (Value)
	{
		Builder.SetValue(MoveTemp(Value));
	}
	for (FPayload& Attachment : Attachments)
	{
		Builder.AddAttachment(MoveTemp(Attachment));
	}

	UE_LOG(LogDerivedDataCache, Verbose, TEXT("Cache: Get cache hit for %s from '%.*s'"),
		*TToString<96>(Key), Context.Len(), Context.GetData());
	Params.Status = ECacheStatus::Cached;
}

FRequest FCache::Get(
	TConstArrayView<FCacheKey> Keys,
	FStringView Context,
	ECachePolicy Policy,
	EPriority Priority,
	FOnCacheGetComplete&& Callback)
{
	for (const FCacheKey& Key : Keys)
	{
		Get(Key, Context, Policy, Priority, Callback);
	}
	return FRequest();
}

void FCache::GetPayload(
	const FCachePayloadKey& Key,
	FStringView Context,
	ECachePolicy Policy,
	EPriority Priority,
	FOnCacheGetPayloadComplete& Callback)
{
	FCacheGetPayloadCompleteParams Params;
	Params.Key = Key.GetKey();

	ON_SCOPE_EXIT
	{
		if (Callback)
		{
			if (!Params.Payload)
			{
				Params.Payload = FPayload(Key.GetId(), FIoHash());
			}
			Callback(MoveTemp(Params));
		}
	};

	// Skip the request if querying the cache is disabled.
	if (!EnumHasAnyFlags(Policy, ECachePolicy::Query))
	{
		UE_LOG(LogDerivedDataCache, VeryVerbose, TEXT("Cache: GetPayload skipped %s from '%.*s'"),
			*TToString<96>(Key), Context.Len(), Context.GetData());
		return;
	}

	// Check for existence of the payload if it is being skipped.
	if (EnumHasAnyFlags(Policy, ECachePolicy::SkipValue))
	{
		const bool bPayloadExists = EnumHasAnyFlags(Policy, ECachePolicy::StoreLocal)
			? GetDerivedDataCacheRef().TryToPrefetch({MakePayloadKey(Key)}, Context)
			: GetDerivedDataCacheRef().CachedDataProbablyExists(*MakePayloadKey(Key));
		if (bPayloadExists)
		{
			// Request the record from storage.
			FSharedBuffer RecordBuffer;
			{
				TArray<uint8> RecordData;
				if (!GetDerivedDataCacheRef().GetSynchronous(*MakeRecordKey(Key.GetKey()), RecordData, Context))
				{
					UE_LOG(LogDerivedDataCache, Verbose, TEXT("Cache: Get cache miss with missing record for %s from '%.*s'"),
						*TToString<96>(Key), Context.Len(), Context.GetData());
					return;
				}
				RecordBuffer = MakeSharedBufferFromArray(MoveTemp(RecordData));
			}

			// Validate that the record can be read as compact binary without crashing.
			if (ValidateCompactBinaryRange(RecordBuffer, ECbValidateMode::Default) != ECbValidateError::None)
			{
				UE_LOG(LogDerivedDataCache, Verbose, TEXT("Cache: Get cache miss with corrupted record for %s from '%.*s'"),
					*TToString<96>(Key), Context.Len(), Context.GetData());
				return;
			}

			// Read the record from its compact binary fields.
			const FCbObject RecordObject(MoveTemp(RecordBuffer));
			const FCbObjectView ValueObject = RecordObject["Value"_ASV].AsObjectView();
			const FCbArrayView AttachmentsArray = RecordObject["Attachments"_ASV].AsArrayView();

			// Find the hash of the payload in the record.
			const FCbObjectId KeyObjectId = Key.GetId().ToObjectId();
			if (ValueObject["Id"_ASV].AsObjectId() == KeyObjectId)
			{
				Params.Payload = FPayload(Key.GetId(), ValueObject["Hash"_ASV].AsBinaryAttachment());
			}
			else
			{
				for (FCbFieldView AttachmentField : AttachmentsArray)
				{
					if (AttachmentField.AsObjectView()["Id"_ASV].AsObjectId() == KeyObjectId)
					{
						Params.Payload = FPayload(Key.GetId(), AttachmentField.AsObjectView()["Hash"_ASV].AsBinaryAttachment());
						break;
					}
				}
			}

			if (Params.Payload)
			{
				UE_LOG(LogDerivedDataCache, Verbose, TEXT("Cache: GetPayload (exists) cache hit for %s from '%.*s'"),
					*TToString<96>(Key), Context.Len(), Context.GetData());
				Params.Status = ECacheStatus::Cached;
			}
			else
			{
				UE_LOG(LogDerivedDataCache, Verbose,
					TEXT("Cache: GetPayload (exists) cache miss with missing payload in record for %s from '%.*s'"),
					*TToString<96>(Key), Context.Len(), Context.GetData());
			}
		}
		else
		{
			UE_LOG(LogDerivedDataCache, Verbose, TEXT("Cache: GetPayload (exists) cache miss for %s from '%.*s'"),
				*TToString<96>(Key), Context.Len(), Context.GetData());
		}
		return;
	}

	// Request the payload from storage.
	TArray<uint8> Data;
	if (!GetDerivedDataCacheRef().GetSynchronous(*MakePayloadKey(Key), Data, Context))
	{
		UE_LOG(LogDerivedDataCache, Verbose, TEXT("Cache: GetPayload cache miss for %s from '%.*s'"),
			*TToString<96>(Key), Context.Len(), Context.GetData());
		return;
	}

	Params.Payload = FPayload(Key.GetId(), FCompressedBuffer(MakeSharedBufferFromArray(MoveTemp(Data))));

	UE_LOG(LogDerivedDataCache, Verbose, TEXT("Cache: GetPayload cache hit for %s from '%.*s'"),
		*TToString<96>(Key), Context.Len(), Context.GetData());
	Params.Status = ECacheStatus::Cached;
}

FRequest FCache::GetPayloads(
	TConstArrayView<FCachePayloadKey> Keys,
	FStringView Context,
	ECachePolicy Policy,
	EPriority Priority,
	FOnCacheGetPayloadComplete&& Callback)
{
	for (const FCachePayloadKey& Key : Keys)
	{
		GetPayload(Key, Context, Policy, Priority, Callback);
	}
	return FRequest();
}

void FCache::CancelAll()
{
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ICache* CreateCache()
{
	return new FCache();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

} // DerivedData
} // UE

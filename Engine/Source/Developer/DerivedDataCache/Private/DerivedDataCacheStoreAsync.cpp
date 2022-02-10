// Copyright Epic Games, Inc. All Rights Reserved.

#include "Async/AsyncWork.h"
#include "DerivedDataCachePrivate.h"
#include "DerivedDataCacheStore.h"
#include "DerivedDataCacheUsageStats.h"
#include "DerivedDataLegacyCacheStore.h"
#include "DerivedDataRequest.h"
#include "DerivedDataRequestOwner.h"
#include "DerivedDataValueId.h"
#include "Experimental/Async/LazyEvent.h"
#include "FileBackedDerivedDataBackend.h"
#include "Memory/SharedBuffer.h"
#include "ProfilingDebugging/CookStats.h"
#include "Stats/Stats.h"
#include "Tasks/Task.h"
#include "Templates/Invoke.h"

namespace UE::DerivedData::CacheStore::Memory
{
FFileBackedDerivedDataBackend* CreateMemoryDerivedDataBackend(const TCHAR* Name, int64 MaxCacheSize, bool bCanBeDisabled);
} // UE::DerivedData::CacheStore::Memory

namespace UE::DerivedData
{

/**
 * A cache store that executes non-blocking requests in a dedicated thread pool.
 *
 * Puts can be stored in a memory cache while they are in flight.
 */
class FCacheStoreAsync : public ILegacyCacheStore
{
public:
	FCacheStoreAsync(ILegacyCacheStore* InInnerCache, ECacheStoreFlags InInnerFlags, bool bCacheInFlightPuts);

	virtual void Put(
		TConstArrayView<FCachePutRequest> Requests,
		IRequestOwner& Owner,
		FOnCachePutComplete&& OnComplete) override
	{
		Execute(COOK_STAT(&FDerivedDataCacheUsageStats::TimePut,) Requests, Owner, MoveTemp(OnComplete), &ICacheStore::Put);
	}

	virtual void Get(
		TConstArrayView<FCacheGetRequest> Requests,
		IRequestOwner& Owner,
		FOnCacheGetComplete&& OnComplete) override
	{
		Execute(COOK_STAT(&FDerivedDataCacheUsageStats::TimeGet,) Requests, Owner, MoveTemp(OnComplete), &ICacheStore::Get);
	}

	virtual void PutValue(
		TConstArrayView<FCachePutValueRequest> Requests,
		IRequestOwner& Owner,
		FOnCachePutValueComplete&& OnComplete) override
	{
		Execute(COOK_STAT(&FDerivedDataCacheUsageStats::TimePut,) Requests, Owner, MoveTemp(OnComplete), &ICacheStore::PutValue);
	}

	virtual void GetValue(
		TConstArrayView<FCacheGetValueRequest> Requests,
		IRequestOwner& Owner,
		FOnCacheGetValueComplete&& OnComplete) override
	{
		Execute(COOK_STAT(&FDerivedDataCacheUsageStats::TimeGet,) Requests, Owner, MoveTemp(OnComplete), &ICacheStore::GetValue);
	}

	virtual void GetChunks(
		TConstArrayView<FCacheGetChunkRequest> Requests,
		IRequestOwner& Owner,
		FOnCacheGetChunkComplete&& OnComplete) override
	{
		Execute(COOK_STAT(&FDerivedDataCacheUsageStats::TimeGet,) Requests, Owner, MoveTemp(OnComplete), &ICacheStore::GetChunks);
	}

	virtual void LegacyPut(
		TConstArrayView<FLegacyCachePutRequest> Requests,
		IRequestOwner& Owner,
		FOnLegacyCachePutComplete&& OnComplete) override
	{
		Execute(COOK_STAT(&FDerivedDataCacheUsageStats::TimePut,) Requests, Owner, MoveTemp(OnComplete), &ILegacyCacheStore::LegacyPut);
	}

	virtual void LegacyGet(
		TConstArrayView<FLegacyCacheGetRequest> Requests,
		IRequestOwner& Owner,
		FOnLegacyCacheGetComplete&& OnComplete) override
	{
		Execute(COOK_STAT(&FDerivedDataCacheUsageStats::TimeGet,) Requests, Owner, MoveTemp(OnComplete), &ILegacyCacheStore::LegacyGet);
	}

	virtual void LegacyDelete(
		TConstArrayView<FLegacyCacheDeleteRequest> Requests,
		IRequestOwner& Owner,
		FOnLegacyCacheDeleteComplete&& OnComplete) override
	{
		Execute(COOK_STAT(&FDerivedDataCacheUsageStats::TimePut,) Requests, Owner, MoveTemp(OnComplete), &ILegacyCacheStore::LegacyDelete);
	}

	virtual void LegacyStats(FDerivedDataCacheStatsNode& OutNode) override;

	virtual bool LegacyDebugOptions(FBackendDebugOptions& Options) override
	{
		return InnerCache->LegacyDebugOptions(Options);
	}

private:
	COOK_STAT(using CookStatsFunction = FCookStats::FScopedStatsCounter (FDerivedDataCacheUsageStats::*)());

	template <typename RequestType, typename OnCompleteType, typename OnExecuteType>
	void Execute(
		COOK_STAT(CookStatsFunction OnAddStats,)
		TConstArrayView<RequestType> Requests,
		IRequestOwner& Owner,
		OnCompleteType&& OnComplete,
		OnExecuteType&& OnExecute);

	ILegacyCacheStore* InnerCache;
	ECacheStoreFlags InnerFlags;
	TUniquePtr<ILegacyCacheStore> MemoryCache;
	FDerivedDataCacheUsageStats UsageStats;
};

FCacheStoreAsync::FCacheStoreAsync(ILegacyCacheStore* InInnerCache, ECacheStoreFlags InInnerFlags, bool bCacheInFlightPuts)
	: InnerCache(InInnerCache)
	, InnerFlags(InInnerFlags)
	, MemoryCache(bCacheInFlightPuts ? CacheStore::Memory::CreateMemoryDerivedDataBackend(TEXT("InflightMemoryCache"), /*MaxCacheSize*/ -1, /*bCanBeDisabled*/ false) : nullptr)
{
	check(InnerCache);
}

void FCacheStoreAsync::LegacyStats(FDerivedDataCacheStatsNode& OutNode)
{
	OutNode = {TEXT("AsyncWrapper"), TEXT(""), EnumHasAnyFlags(InnerFlags, ECacheStoreFlags::Local)};
	OutNode.Stats.Add(TEXT(""), UsageStats);

	InnerCache->LegacyStats(OutNode.Children.Add_GetRef(MakeShared<FDerivedDataCacheStatsNode>()).Get());
	if (MemoryCache)
	{
		MemoryCache->LegacyStats(OutNode.Children.Add_GetRef(MakeShared<FDerivedDataCacheStatsNode>()).Get());
	}
}

class FDerivedDataAsyncWrapperRequest final : public FRequestBase, private IQueuedWork
{
public:
	inline FDerivedDataAsyncWrapperRequest(
		IRequestOwner& InOwner,
		TUniqueFunction<void (IRequestOwner& Owner, bool bCancel)>&& InFunction)
		: Owner(InOwner)
		, Function(MoveTemp(InFunction))
	{
	}

	inline void Start(EPriority Priority)
	{
		Owner.Begin(this);
		DoneEvent.Reset();
		Private::GCacheThreadPool->AddQueuedWork(this, GetPriority(Priority));
	}

	inline void Execute(bool bCancel)
	{
		FScopeCycleCounter Scope(GetStatId(), /*bAlways*/ true);
		Owner.End(this, [this, bCancel]
		{
			Function(Owner, bCancel);
			DoneEvent.Trigger();
		});
		// DO NOT ACCESS ANY MEMBERS PAST THIS POINT!
	}

	// IRequest Interface

	inline void SetPriority(EPriority Priority) final
	{
		if (Private::GCacheThreadPool->RetractQueuedWork(this))
		{
			Private::GCacheThreadPool->AddQueuedWork(this, GetPriority(Priority));
		}
	}

	inline void Cancel() final
	{
		if (!DoneEvent.Wait(0))
		{
			if (Private::GCacheThreadPool->RetractQueuedWork(this))
			{
				Abandon();
			}
			else
			{
				FScopeCycleCounter Scope(GetStatId());
				DoneEvent.Wait();
			}
		}
	}

	inline void Wait() final
	{
		if (!DoneEvent.Wait(0))
		{
			if (Private::GCacheThreadPool->RetractQueuedWork(this))
			{
				DoThreadedWork();
			}
			else
			{
				FScopeCycleCounter Scope(GetStatId());
				DoneEvent.Wait();
			}
		}
	}

private:
	static EQueuedWorkPriority GetPriority(EPriority Priority)
	{
		switch (Priority)
		{
		case EPriority::Blocking: return EQueuedWorkPriority::Blocking;
		case EPriority::Highest:  return EQueuedWorkPriority::Highest;
		case EPriority::High:     return EQueuedWorkPriority::High;
		case EPriority::Normal:   return EQueuedWorkPriority::Normal;
		case EPriority::Low:      return EQueuedWorkPriority::Low;
		case EPriority::Lowest:   return EQueuedWorkPriority::Lowest;
		default: checkNoEntry();  return EQueuedWorkPriority::Normal;
		}
	}

	// IQueuedWork Interface

	inline void DoThreadedWork() final
	{
		Execute(/*bCancel*/ false);
	}

	inline void Abandon() final
	{
		Execute(/*bCancel*/ true);
	}

	inline TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FDerivedDataAsyncWrapperRequest, STATGROUP_ThreadPoolAsyncTasks);
	}

private:
	IRequestOwner& Owner;
	TUniqueFunction<void (IRequestOwner& Owner, bool bCancel)> Function;
	FLazyEvent DoneEvent{EEventMode::ManualReset};
};

void Private::ExecuteInCacheThreadPool(
	IRequestOwner& Owner,
	TUniqueFunction<void (IRequestOwner& Owner, bool bCancel)>&& Function)
{
	FDerivedDataAsyncWrapperRequest* Request = new FDerivedDataAsyncWrapperRequest(Owner, MoveTemp(Function));
	Request->Start(Owner.GetPriority());
}

template <typename RequestType, typename OnCompleteType, typename OnExecuteType>
void FCacheStoreAsync::Execute(
	COOK_STAT(CookStatsFunction OnAddStats,)
	const TConstArrayView<RequestType> Requests,
	IRequestOwner& Owner,
	OnCompleteType&& OnComplete,
	OnExecuteType&& OnExecute)
{
	auto ExecuteWithStats = [this, COOK_STAT(OnAddStats,) OnExecute](TConstArrayView<RequestType> Requests, IRequestOwner& Owner, OnCompleteType&& OnComplete) mutable
	{
		Invoke(OnExecute, *InnerCache, Requests, Owner, [this, COOK_STAT(OnAddStats,) OnComplete = MoveTemp(OnComplete)](auto&& Response)
		{
			if (Response.Status == EStatus::Ok)
			{
				COOK_STAT(Invoke(OnAddStats, UsageStats).AddHit(0));
			}
			OnComplete(MoveTemp(Response));
			FDerivedDataBackend::Get().AddToAsyncCompletionCounter(-1);
		});
	};

	FDerivedDataBackend::Get().AddToAsyncCompletionCounter(Requests.Num());
	if (Owner.GetPriority() == EPriority::Blocking || !Private::GCacheThreadPool)
	{
		return ExecuteWithStats(Requests, Owner, MoveTemp(OnComplete));
	}

	Private::ExecuteInCacheThreadPool(Owner,
		[this,
		Requests = TArray<RequestType>(Requests),
		OnComplete = MoveTemp(OnComplete),
		ExecuteWithStats = MoveTemp(ExecuteWithStats)]
		(IRequestOwner& Owner, bool bCancel) mutable
		{
			if (!bCancel)
			{
				ExecuteWithStats(Requests, Owner, MoveTemp(OnComplete));
			}
			else
			{
				CompleteWithStatus(Requests, MoveTemp(OnComplete), EStatus::Canceled);
				FDerivedDataBackend::Get().AddToAsyncCompletionCounter(-Requests.Num());
			}
		});
}

ILegacyCacheStore* CreateCacheStoreAsync(ILegacyCacheStore* InnerCache, ECacheStoreFlags InnerFlags, bool bCacheInFlightPuts)
{
	return new FCacheStoreAsync(InnerCache, InnerFlags, bCacheInFlightPuts);
}

} // UE::DerivedData

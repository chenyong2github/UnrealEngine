// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Paths.h"
#include "DerivedDataBackendInterface.h"
#include "ProfilingDebugging/CookStats.h"
#include "DerivedDataCacheUsageStats.h"
#include "Misc/ScopeLock.h"
#include "Misc/FileHelper.h"

class Error;

namespace UE::DerivedData::Backends
{

/**
 * FDerivedDataBackendVerifyWrapper, a wrapper for derived data that verifies the cache is bit-wise identical by failing all gets and verifying the puts
**/
class FDerivedDataBackendVerifyWrapper : public FDerivedDataBackendInterface
{
public:

	/**
	 * Constructor
	 *
	 * @param	InInnerBackend		Backend to use for storage, my responsibilities are about async puts
	 * @param	InbFixProblems		if true, fix any problems found
	 */
	FDerivedDataBackendVerifyWrapper(FDerivedDataBackendInterface* InInnerBackend, bool InbFixProblems)
		: bFixProblems(InbFixProblems)
		, InnerBackend(InInnerBackend)
	{
		check(InnerBackend);
	}

	/** Return a name for this interface */
	virtual FString GetName() const override
	{
		return FString::Printf(TEXT("VerifyWrapper (%s)"), *InnerBackend->GetName());
	}

	virtual bool IsWritable() const override
	{
		return true;
	}

	virtual ESpeedClass GetSpeedClass() const override
	{
		return ESpeedClass::Local;
	}

	virtual bool CachedDataProbablyExists(const TCHAR* CacheKey) override
	{
		COOK_STAT(auto Timer = UsageStats.TimeProbablyExists());
		FScopeLock ScopeLock(&SynchronizationObject);
		if (AlreadyTested.Contains(FString(CacheKey)))
		{
			COOK_STAT(Timer.AddHit(0));
			return true;
		}
		return false;
	}

	virtual bool GetCachedData(const TCHAR* CacheKey, TArray<uint8>& OutData) override
	{
		COOK_STAT(auto Timer = UsageStats.TimeGet());
		bool bAlreadyTested = false;
		{
			FScopeLock ScopeLock(&SynchronizationObject);
			if (AlreadyTested.Contains(FString(CacheKey)))
			{
				bAlreadyTested = true;
			}
		}
		if (bAlreadyTested)
		{
			bool bResult = InnerBackend->GetCachedData(CacheKey, OutData);
			if (bResult)
			{
				COOK_STAT(Timer.AddHit(OutData.Num()));
			}
			return bResult;
		}
		return false;
	}

	virtual EPutStatus PutCachedData(const TCHAR* CacheKey, TArrayView<const uint8> InData, bool bPutEvenIfExists) override
	{
		COOK_STAT(auto Timer = UsageStats.TimePut());
		{
			FScopeLock ScopeLock(&SynchronizationObject);
			if (AlreadyTested.Contains(FString(CacheKey)))
			{
				return EPutStatus::Cached;
			}
			AlreadyTested.Add(FString(CacheKey));
		}

		COOK_STAT(Timer.AddHit(InData.Num()));
		TArray<uint8> OutData;
		bool bSuccess = InnerBackend->GetCachedData(CacheKey, OutData);
		if (bSuccess)
		{
			if (OutData != InData)
			{
				UE_LOG(LogDerivedDataCache, Error, TEXT("Verify: Cached data differs from newly generated data %s."), CacheKey);
				FString Cache = FPaths::ProjectSavedDir() / TEXT("VerifyDDC") / CacheKey + TEXT(".fromcache");
				FFileHelper::SaveArrayToFile(OutData, *Cache);
				FString Verify = FPaths::ProjectSavedDir() / TEXT("VerifyDDC") / CacheKey + TEXT(".verify");;
				FFileHelper::SaveArrayToFile(InData, *Verify);
				if (bFixProblems)
				{
					UE_LOG(LogDerivedDataCache, Display, TEXT("Verify: Wrote newly generated data to the cache."), CacheKey);
					InnerBackend->PutCachedData(CacheKey, InData, true);
				}
			}
			else
			{
				UE_LOG(LogDerivedDataCache, Log, TEXT("Verify: Cached data exists and matched %s."), CacheKey);
			}
			return EPutStatus::Cached;
		}
		else
		{
			UE_LOG(LogDerivedDataCache, Warning, TEXT("Verify: Cached data didn't exist %s."), CacheKey);
			return InnerBackend->PutCachedData(CacheKey, InData, bPutEvenIfExists);
		}
	}

	virtual void RemoveCachedData(const TCHAR* CacheKey, bool bTransient) override
	{
		InnerBackend->RemoveCachedData(CacheKey, bTransient);
	}

	virtual TSharedRef<FDerivedDataCacheStatsNode> GatherUsageStats() const override
	{
		TSharedRef<FDerivedDataCacheStatsNode> Usage = MakeShared<FDerivedDataCacheStatsNode>(this, TEXT("VerifyWrapper"));
		Usage->Stats.Add(TEXT(""), UsageStats);

		if (InnerBackend)
		{
			Usage->Children.Add(InnerBackend->GatherUsageStats());
		}

		return Usage;
	}

	virtual bool TryToPrefetch(TConstArrayView<FString> CacheKeys) override
	{
		return InnerBackend->TryToPrefetch(CacheKeys);
	}

	virtual bool WouldCache(const TCHAR* CacheKey, TArrayView<const uint8> InData) override
	{
		return InnerBackend->WouldCache(CacheKey, InData);
	}

	bool ApplyDebugOptions(FBackendDebugOptions& InOptions) override
	{
		return InnerBackend->ApplyDebugOptions(InOptions);
	}

	virtual FRequest Put(
		TArrayView<FCacheRecord> Records,
		FStringView Context,
		ECachePolicy Policy,
		EPriority Priority,
		FOnCachePutComplete&& OnComplete) override
	{
		return InnerBackend->Put(Records, Context, Policy, Priority, MoveTemp(OnComplete));
	}

	virtual FRequest Get(
		TConstArrayView<FCacheKey> Keys,
		FStringView Context,
		ECachePolicy Policy,
		EPriority Priority,
		FOnCacheGetComplete&& OnComplete) override
	{
		return InnerBackend->Get(Keys, Context, Policy, Priority, MoveTemp(OnComplete));
	}

	virtual FRequest GetPayload(
		TConstArrayView<FCachePayloadKey> Keys,
		FStringView Context,
		ECachePolicy Policy,
		EPriority Priority,
		FOnCacheGetPayloadComplete&& OnComplete) override
	{
		return InnerBackend->GetPayload(Keys, Context, Policy, Priority, MoveTemp(OnComplete));
	}

	virtual void CancelAll() override
	{
		InnerBackend->CancelAll();
	}

private:
	FDerivedDataCacheUsageStats UsageStats;

	/** If problems are encountered, do we fix them?						*/
	bool											bFixProblems;
	/** Object used for synchronization via a scoped lock						*/
	FCriticalSection								SynchronizationObject;
	/** Set of cache keys we already tested **/
	TSet<FString>									AlreadyTested;
	/** Backend to service the actual requests */
	FDerivedDataBackendInterface*					InnerBackend;
};

} // UE::DerivedData::Backends

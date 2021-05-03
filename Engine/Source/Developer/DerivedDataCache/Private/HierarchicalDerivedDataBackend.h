// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DerivedDataBackendInterface.h"
#include "ProfilingDebugging/CookStats.h"
#include "DerivedDataCacheRecord.h"
#include "DerivedDataCacheUsageStats.h"
#include "DerivedDataBackendAsyncPutWrapper.h"
#include "Templates/UniquePtr.h"
#include "Containers/ArrayView.h"

extern bool GVerifyDDC;

namespace UE::DerivedData::Backends
{

/** 
 * A backend wrapper that implements a cache hierarchy of backends. 
**/
class FHierarchicalDerivedDataBackend : public FDerivedDataBackendInterface
{
public:

	/**
	 * Constructor
	 * @param	InInnerBackends Backends to call into for actual storage of the cache, first item is the "fastest cache"
	 */
	FHierarchicalDerivedDataBackend(ICacheFactory& InFactory, const TArray<FDerivedDataBackendInterface*>& InInnerBackends)
		: Factory(InFactory)
		, InnerBackends(InInnerBackends)
		, bIsWritable(false)
		, bHasLocalBackends(false)
		, bHasRemoteBackends(false)
		, bHasMultipleLocalBackends(false)
		, bHasMultipleRemoteBackends(false)
		, bHasWritableLocalBackends(false)
		, bHasWritableRemoteBackends(false)
	{
		check(InnerBackends.Num() > 1); // if it is just one, then you don't need this wrapper
		UpdateAsyncInnerBackends();
	}

	/** Return a name for this interface */
	virtual FString GetName() const override
	{
		return TEXT("HierarchicalDerivedDataBackend");
	}

	/** Are we a remote cache? */
	virtual ESpeedClass GetSpeedClass() const override
	{
		return ESpeedClass::Local;
	}

	void UpdateAsyncInnerBackends()
	{
		bIsWritable = false;
		bHasLocalBackends = false;
		bHasRemoteBackends = false;
		bHasMultipleLocalBackends = false;
		bHasMultipleRemoteBackends = false;
		bHasWritableLocalBackends = false;
		bHasWritableRemoteBackends = false;
		for (int32 CacheIndex = 0; CacheIndex < InnerBackends.Num(); CacheIndex++)
		{
			const bool bIsWritableBackend = InnerBackends[CacheIndex]->IsWritable();
			bIsWritable |= bIsWritableBackend;
			if (InnerBackends[CacheIndex]->GetSpeedClass() == ESpeedClass::Local)
			{
				bHasWritableLocalBackends |= bIsWritableBackend;
				bHasMultipleLocalBackends = bHasLocalBackends;
				bHasLocalBackends = true;
			}
			else
			{
				bHasWritableRemoteBackends |= bIsWritableBackend;
				bHasMultipleRemoteBackends = bHasRemoteBackends;
				bHasRemoteBackends = true;
			}
		}
		if (bIsWritable)
		{
			for (int32 CacheIndex = 0; CacheIndex < InnerBackends.Num(); CacheIndex++)
			{
				// async puts to allow us to fill all levels without holding up the engine
				AsyncPutInnerBackends.Emplace(new FDerivedDataBackendAsyncPutWrapper(Factory, InnerBackends[CacheIndex], false));
			}
		}
	}

	/** Adds inner backend. */
	void AddInnerBackend(FDerivedDataBackendInterface* InInner) 
	{
		InnerBackends.Add(InInner);
		AsyncPutInnerBackends.Empty();
		UpdateAsyncInnerBackends();
	}

	/** Removes inner backend. */
	bool RemoveInnerBackend(FDerivedDataBackendInterface* InInner) 
	{
		int32 NumRemoved = InnerBackends.Remove(InInner);
		AsyncPutInnerBackends.Empty();
		UpdateAsyncInnerBackends();
		return NumRemoved != 0;
	}

	/** return true if this cache is writable **/
	virtual bool IsWritable() const override
	{
		return bIsWritable;
	}

	/**
	 * Synchronous test for the existence of a cache item
	 *
	 * @param	CacheKey	Alphanumeric+underscore key of this cache item
	 * @return				true if the data probably will be found, this can't be guaranteed because of concurrency in the backends, corruption, etc
	 */
	virtual bool CachedDataProbablyExists(const TCHAR* CacheKey) override
	{
		COOK_STAT(auto Timer = UsageStats.TimeProbablyExists());
		for (int32 CacheIndex = 0; CacheIndex < InnerBackends.Num(); CacheIndex++)
		{
			// Skip slow caches because the primary users of this function assume that
			// they will have fast access to the data if this returns true. It will be
			// better in those cases to rebuild the data locally than to block on slow
			// fetch operations because the build may be, and often is, asynchronous.
			bool bFastCache = InnerBackends[CacheIndex]->GetSpeedClass() >= ESpeedClass::Fast;
			if (!bFastCache)
			{
				continue;
			}

			if (InnerBackends[CacheIndex]->CachedDataProbablyExists(CacheKey))
			{
				COOK_STAT(Timer.AddHit(0));
				return true;
			}
			else
			{
				if (GVerifyDDC)
				{
					ensureMsgf(!AsyncPutInnerBackends[CacheIndex]->CachedDataProbablyExists(CacheKey), TEXT("%s did not exist in sync interface for CachedDataProbablyExists but was found in async wrapper"), CacheKey);
				}
			}
		}
		return false;
	}

	/**
	 * Synchronous test for the existence of multiple cache items
	 *
	 * @param	CacheKeys	Alphanumeric+underscore key of the cache items
	 * @return				A bit array with bits indicating whether the data for the corresponding key will probably be found
	 */
	virtual TBitArray<> CachedDataProbablyExistsBatch(TConstArrayView<FString> CacheKeys) override
	{
		COOK_STAT(auto Timer = UsageStats.TimeProbablyExists());
		check(InnerBackends.Num() > 0);

		TBitArray<> Result;

		for (int32 CacheIndex = 0; CacheIndex < InnerBackends.Num(); ++CacheIndex)
		{
			const int32 MissingKeys = CacheKeys.Num() - Result.CountSetBits();
			if (MissingKeys == 0)
			{
				break;
			}

			// Skip slow caches because the primary users of this function assume that
			// they will have fast access to the data if this returns true. It will be
			// better in those cases to rebuild the data locally than to block on slow
			// fetch operations because the build may be, and often is, asynchronous.
			bool bFastCache = InnerBackends[CacheIndex]->GetSpeedClass() >= ESpeedClass::Fast;
			if (!bFastCache)
			{
				continue;
			}

			if (MissingKeys == CacheKeys.Num())
			{
				Result = InnerBackends[CacheIndex]->CachedDataProbablyExistsBatch(CacheKeys);
				check(Result.Num() == CacheKeys.Num());
			}
			else
			{
				TArray<FString> RemainingKeys;
				for (int32 KeyIndex = 0; KeyIndex < CacheKeys.Num(); ++KeyIndex)
				{
					if (!Result[KeyIndex])
					{
						RemainingKeys.Add(CacheKeys[KeyIndex]);
					}
				}

				TBitArray<>::FIterator ResultIt(Result);
				TBitArray<> NewResult = InnerBackends[CacheIndex]->CachedDataProbablyExistsBatch(RemainingKeys);
				check(NewResult.Num() == RemainingKeys.Num());
				for (TBitArray<>::FConstIterator NewIt(NewResult); NewIt; ++NewIt)
				{
					// Skip to the bit for the next remaining key.
					while (ResultIt.GetValue())
					{
						++ResultIt;
					}
					ResultIt.GetValue() = NewIt.GetValue();
					++ResultIt;
				}
			}
		}

		if (Result.IsEmpty())
		{
			Result.Add(false, CacheKeys.Num());
		}

		if (Result.CountSetBits() == CacheKeys.Num())
		{
			COOK_STAT(Timer.AddHit(0));
		}
		return Result;
	}

	/**
	 * Attempt to make sure the cached data will be available as optimally as possible.
	 *
	 * @param	CacheKeys	Alphanumeric+underscore keys of the cache items
	 * @return				true if the data will probably be found in a fast backend on a future request.
	 */
	virtual bool TryToPrefetch(TConstArrayView<FString> CacheKeys) override
	{
		COOK_STAT(auto Timer = UsageStats.TimePrefetch());

		TArray<FString, TInlineAllocator<16>> SearchKeys(CacheKeys);

		bool bHasFastBackendToWrite = false;
		bool bHasSlowBackend = false;

		for (FDerivedDataBackendInterface* Interface : InnerBackends)
		{
			if (Interface->GetSpeedClass() < ESpeedClass::Fast)
			{
				bHasSlowBackend = true;
			}
			else
			{
				bHasFastBackendToWrite = bHasFastBackendToWrite || Interface->IsWritable();

				// Remove keys that exist in a fast backend because we already have fast access to them.
				TBitArray<> Hits = Interface->CachedDataProbablyExistsBatch(SearchKeys);
				TArray<FString, TInlineAllocator<16>>::TIterator It = SearchKeys.CreateIterator();
				It.SetToEnd();
				for (--It; It; --It)
				{
					if (Hits[It.GetIndex()])
					{
						It.RemoveCurrent();
					}
				}

				// No fetch is necessary if every key already exists in a fast backend.
				if (SearchKeys.IsEmpty())
				{
					COOK_STAT(Timer.AddHit(0));
					return true;
				}
			}
		}

		// Try to fetch remaining keys, which will fill them from slow backends into writable fast backends.
		bool bHit = true;
		int64 BytesFetched = 0;
		if (bHasSlowBackend && bHasFastBackendToWrite)
		{
			for (const FString& CacheKey : SearchKeys)
			{
				TArray<uint8> Ignored;
				bHit &= GetCachedData(*CacheKey, Ignored);
				BytesFetched += Ignored.Num();
			}
		}

		COOK_STAT(if (bHit) { Timer.AddHit(BytesFetched); });
		return bHit;
	}

	/*
		Determine if we would cache this by asking all our inner layers
	*/
	virtual bool WouldCache(const TCHAR* CacheKey, TArrayView<const uint8> InData) override
	{
		for (int32 CacheIndex = 0; CacheIndex < InnerBackends.Num(); CacheIndex++)
		{
			if (InnerBackends[CacheIndex]->WouldCache(CacheKey, InData))
			{
				return true;
			}
		}

		return false;
	}

	bool ApplyDebugOptions(FBackendDebugOptions& InOptions) override
	{
		bool bSuccess = true;
		for (int32 CacheIndex = 0; CacheIndex < InnerBackends.Num(); CacheIndex++)
		{
			if (!InnerBackends[CacheIndex]->ApplyDebugOptions(InOptions))
			{
				bSuccess = false;
			}
		}

		return bSuccess;
	}



	/**
	 * Synchronous retrieve of a cache item
	 *
	 * @param	CacheKey	Alphanumeric+underscore key of this cache item
	 * @param	OutData		Buffer to receive the results, if any were found
	 * @return				true if any data was found, and in this case OutData is non-empty
	 */
	virtual bool GetCachedData(const TCHAR* CacheKey, TArray<uint8>& OutData) override
	{
		COOK_STAT(auto Timer = UsageStats.TimeGet());

		for (int32 CacheIndex = 0; CacheIndex < InnerBackends.Num(); CacheIndex++)
		{
			FDerivedDataBackendInterface* GetInterface = InnerBackends[CacheIndex];

			// just try and get the cached data. It's faster to try and fail than it is to check and succeed. 			
			if (GetInterface->GetCachedData(CacheKey, OutData))
			{
				// if this hierarchy is writable..
				if (bIsWritable)
				{
					// fill in the higher level caches (start with the highest level as that should be the biggest 
					// !/$ if any of our puts get interrupted or fail)
					for (int32 MissedCacheIndex = 0; MissedCacheIndex < CacheIndex; MissedCacheIndex++)
					{
						FDerivedDataBackendInterface* MissedCache = InnerBackends[MissedCacheIndex];

						if (MissedCache->IsWritable())
						{
							// We want to make sure that the relationship between ProbablyExists and GetCachedData is valid but
							// only if we have a fast cache. Mismatches are edge cases caused by failed writes or corruption. 
							// They get handled, so can be left to eventually be rectified by a faster machine
							bool bFastCache = MissedCache->GetSpeedClass() >= ESpeedClass::Fast;
							bool bDidExist = bFastCache ? MissedCache->CachedDataProbablyExists(CacheKey) : false;
							bool bForcePut = false;

							// the cache failed to return data it thinks it has, so clean it up. (todo - can it just be stomped?)
							if (bDidExist)
							{
								MissedCache->RemoveCachedData(CacheKey, /*bTransient=*/ false); // it apparently failed, so lets delete what is there				
								bForcePut = true;
							}

							// use the async interface to perform the put
							AsyncPutInnerBackends[MissedCacheIndex]->PutCachedData(CacheKey, OutData, bForcePut);
							UE_LOG(LogDerivedDataCache, Verbose, TEXT("Forward-filling cache %s with: %s (%d bytes) (force=%d)"), *MissedCache->GetName(), CacheKey, OutData.Num(), bForcePut);
						}
					}

					// cascade this data to any lower level back ends that may be missing the data
					if (InnerBackends[CacheIndex]->BackfillLowerCacheLevels())
					{
						// fill in the lower level caches
						for (int32 PutCacheIndex = CacheIndex + 1; PutCacheIndex < AsyncPutInnerBackends.Num(); PutCacheIndex++)
						{
							FDerivedDataBackendInterface* PutBackend = InnerBackends[PutCacheIndex];

							// If the key is in a distributed cache (e.g. Pak or S3) then don't backfill any further. 
							bool IsInDistributedCache = !PutBackend->IsWritable() && !PutBackend->BackfillLowerCacheLevels() && PutBackend->CachedDataProbablyExists(CacheKey);

							if (!IsInDistributedCache)
							{
								// only backfill to fast caches (todo - need a way to put data that was created locally into the cache for other people)
								bool bFastCache = PutBackend->GetSpeedClass() >= ESpeedClass::Fast;

								// No need to validate that the cache data might exist since the check can be expensive, the async put will do the check and early out in that case
								if (bFastCache && PutBackend->IsWritable())
								{								
									AsyncPutInnerBackends[PutCacheIndex]->PutCachedData(CacheKey, OutData, false); // we do not need to force a put here
									UE_LOG(LogDerivedDataCache, Verbose, TEXT("Back-filling cache %s with: %s (%d bytes) (force=%d)"), *PutBackend->GetName(), CacheKey, OutData.Num(), false);
								}
							}
							else
							{ 
								UE_LOG(LogDerivedDataCache, Verbose, TEXT("Item %s exists in distributed cache %s. Skipping any further backfills."), CacheKey, *PutBackend->GetName());
								break;
							}
						}
					}
				}
				COOK_STAT(Timer.AddHit(OutData.Num()));
				return true;
			}
			else
			{
				if (GVerifyDDC)
				{
					TArray<uint8> TempData;
					ensureMsgf(!AsyncPutInnerBackends[CacheIndex]->GetCachedData(CacheKey, TempData), TEXT("CacheKey %s did not exist in sync interface for GetCachedData but was found in async wrapper"), CacheKey);
				}
			}
		}
		return false;
	}
	/**
	 * Asynchronous, fire-and-forget placement of a cache item
	 *
	 * @param	CacheKey	Alphanumeric+underscore key of this cache item
	 * @param	InData		Buffer containing the data to cache, can be destroyed after the call returns, immediately
	 * @param	bPutEvenIfExists	If true, then do not attempt skip the put even if CachedDataProbablyExists returns true
	 */
	virtual EPutStatus PutCachedData(const TCHAR* CacheKey, TArrayView<const uint8> InData, bool bPutEvenIfExists) override
	{
		COOK_STAT(auto Timer = UsageStats.TimePut());
		EPutStatus Status = EPutStatus::NotCached;
		if (!bIsWritable)
		{
			return Status; // no point in continuing down the chain
		}
		for (int32 PutCacheIndex = 0; PutCacheIndex < InnerBackends.Num(); PutCacheIndex++)
		{
			if (!InnerBackends[PutCacheIndex]->IsWritable() && !InnerBackends[PutCacheIndex]->BackfillLowerCacheLevels() && InnerBackends[PutCacheIndex]->CachedDataProbablyExists(CacheKey))
			{
				break; //do not write things that are already in the read only pak file
			}
			if (InnerBackends[PutCacheIndex]->IsWritable())
			{
				COOK_STAT(Timer.AddHit(InData.Num()));
				// we must do at least one synchronous put to a writable cache before we return
				if (Status != EPutStatus::Cached)
				{
					Status = InnerBackends[PutCacheIndex]->PutCachedData(CacheKey, InData, bPutEvenIfExists);
				}
				else
				{
					AsyncPutInnerBackends[PutCacheIndex]->PutCachedData(CacheKey, InData, bPutEvenIfExists);
				}
			}
		}
		return Status;
	}

	virtual void RemoveCachedData(const TCHAR* CacheKey, bool bTransient) override
	{
		if (!bIsWritable)
		{
			return; // no point in continuing down the chain
		}
		for (int32 PutCacheIndex = 0; PutCacheIndex < InnerBackends.Num(); PutCacheIndex++)
		{
			InnerBackends[PutCacheIndex]->RemoveCachedData(CacheKey, bTransient);
		}
	}

	virtual TSharedRef<FDerivedDataCacheStatsNode> GatherUsageStats() const override
	{
		TSharedRef<FDerivedDataCacheStatsNode> Usage = MakeShared<FDerivedDataCacheStatsNode>(this, TEXT("Hierarchical"));
		Usage->Stats.Add(TEXT(""), UsageStats);

		// All the inner backends are actually wrapped by AsyncPut backends in writable cases (most cases in practice)
		if (AsyncPutInnerBackends.Num() > 0)
		{
			for (const auto& InnerBackend : AsyncPutInnerBackends)
			{
				Usage->Children.Add(InnerBackend->GatherUsageStats());
			}
		}
		else
		{
			for (auto InnerBackend : InnerBackends)
			{
				Usage->Children.Add(InnerBackend->GatherUsageStats());
			}
		}

		return Usage;
	}

	virtual FRequest Put(
		TConstArrayView<FCacheRecord> Records,
		FStringView Context,
		ECachePolicy Policy,
		EPriority Priority,
		FOnCachePutComplete&& OnComplete) override
	{
		TSet<FCacheKey> RecordsOk;
		for (int32 PutCacheIndex = 0; PutCacheIndex < InnerBackends.Num(); PutCacheIndex++)
		{
			if (InnerBackends[PutCacheIndex]->IsWritable())
			{
				// Every record must put synchronously before switching to async calls.
				if (RecordsOk.Num() < Records.Num())
				{
					InnerBackends[PutCacheIndex]->Put(Records, Context, Policy, Priority,
						[&OnComplete, &RecordsOk](FCachePutCompleteParams&& Params)
						{
							if (Params.Status == EStatus::Ok)
							{
								bool bIsAlreadyInSet = false;
								RecordsOk.FindOrAdd(Params.Key, &bIsAlreadyInSet);
								if (OnComplete && !bIsAlreadyInSet)
								{
									OnComplete(MoveTemp(Params));
								}
							}
						}).Wait();
				}
				else
				{
					AsyncPutInnerBackends[PutCacheIndex]->Put(Records, Context, Policy, Priority);
				}
			}
		}

		if (OnComplete && RecordsOk.Num() < Records.Num())
		{
			for (const FCacheRecord& Record : Records)
			{
				if (!RecordsOk.Contains(Record.GetKey()))
				{
					OnComplete({Record.GetKey(), EStatus::Error});
				}
			}
		}

		return FRequest();
	}

	virtual FRequest Get(
		TConstArrayView<FCacheKey> Keys,
		FStringView Context,
		ECachePolicy Policy,
		EPriority Priority,
		FOnCacheGetComplete&& OnComplete) override
	{
		const bool bQueryLocal = bHasLocalBackends && EnumHasAnyFlags(Policy, ECachePolicy::QueryLocal);
		const bool bStoreLocal = bHasWritableLocalBackends && EnumHasAnyFlags(Policy, ECachePolicy::StoreLocal);
		const bool bQueryRemote = bHasRemoteBackends && EnumHasAnyFlags(Policy, ECachePolicy::QueryRemote);
		const bool bStoreRemote = bHasWritableRemoteBackends && EnumHasAnyFlags(Policy, ECachePolicy::StoreRemote);
		const bool bStoreLocalCopy = bStoreLocal && bHasMultipleLocalBackends && !EnumHasAnyFlags(Policy, ECachePolicy::SkipLocalCopy);

		TArray<FCacheKey, TInlineAllocator<16>> RemainingKeys(Keys);

		TSet<FCacheKey> KeysOk;
		for (int32 GetCacheIndex = 0; GetCacheIndex < InnerBackends.Num() && !RemainingKeys.IsEmpty(); ++GetCacheIndex)
		{
			// Remove SkipData flags when possibly filling other backends because only complete records can be written.
			const bool bIsLocalGet = InnerBackends[GetCacheIndex]->GetSpeedClass() == ESpeedClass::Local;
			const bool bFill =
				(bIsLocalGet && bStoreLocalCopy) ||
				(bIsLocalGet && bStoreRemote) ||
				(!bIsLocalGet && bStoreLocal) ||
				(!bIsLocalGet && bStoreRemote && bHasMultipleRemoteBackends);
			const ECachePolicy FillPolicy = bFill ? (Policy & ~ECachePolicy::SkipData) : Policy;

			// Block on this because backends in this hierarchy are not expected to be asynchronous.
			InnerBackends[GetCacheIndex]->Get(RemainingKeys, Context, Policy, Priority,
				[this, Context, GetCacheIndex, bStoreLocal, bStoreRemote, bStoreLocalCopy, bIsLocalGet, bFill, &OnComplete, &KeysOk](FCacheGetCompleteParams&& Params)
				{
					if (Params.Status == EStatus::Ok)
					{
						if (bFill)
						{
							for (int32 FillCacheIndex = 0; FillCacheIndex < InnerBackends.Num(); ++FillCacheIndex)
							{
								if (GetCacheIndex != FillCacheIndex)
								{
									const bool bIsLocalFill = InnerBackends[FillCacheIndex]->GetSpeedClass() == ESpeedClass::Local;
									if ((bIsLocalFill && bStoreLocal && bIsLocalGet <= bStoreLocalCopy) || (!bIsLocalFill && bStoreRemote))
									{
										AsyncPutInnerBackends[FillCacheIndex]->Put({Params.Record}, Context);
									}
								}
							}
						}

						KeysOk.Add(Params.Record.GetKey());
						if (OnComplete)
						{
							OnComplete(MoveTemp(Params));
						}
					}
				}).Wait();

			RemainingKeys.RemoveAll([&KeysOk](const FCacheKey& Key) { return KeysOk.Contains(Key); });
		}

		if (OnComplete)
		{
			for (const FCacheKey& Key : RemainingKeys)
			{
				OnComplete({Factory.CreateRecord(Key).Build(), EStatus::Error});
			}
		}

		return FRequest();
	}

	virtual FRequest GetPayload(
		TConstArrayView<FCachePayloadKey> Keys,
		FStringView Context,
		ECachePolicy Policy,
		EPriority Priority,
		FOnCacheGetPayloadComplete&& OnComplete) override
	{
		TArray<FCachePayloadKey, TInlineAllocator<16>> RemainingKeys(Keys);

		TSet<FCachePayloadKey> KeysOk;
		for (int32 GetCacheIndex = 0; GetCacheIndex < InnerBackends.Num() && !RemainingKeys.IsEmpty(); ++GetCacheIndex)
		{
			InnerBackends[GetCacheIndex]->GetPayload(RemainingKeys, Context, Policy, Priority,
				[&OnComplete, &KeysOk](FCacheGetPayloadCompleteParams&& Params)
				{
					if (Params.Status == EStatus::Ok)
					{
						KeysOk.Add({Params.Key, Params.Payload.GetId()});
						if (OnComplete)
						{
							OnComplete(MoveTemp(Params));
						}
					}
				}).Wait();

			RemainingKeys.RemoveAll([&KeysOk](const FCachePayloadKey& Key) { return KeysOk.Contains(Key); });
		}

		if (OnComplete)
		{
			for (const FCachePayloadKey& Key : RemainingKeys)
			{
				OnComplete({Key.CacheKey, FPayload(Key.Id), EStatus::Error});
			}
		}

		return FRequest();
	}

	virtual void CancelAll() override
	{
	}

private:
	FDerivedDataCacheUsageStats UsageStats;

	ICacheFactory& Factory;
	/** Array of backends forming the hierarchical cache...the first element is the fastest cache. **/
	TArray<FDerivedDataBackendInterface*> InnerBackends;
	/** Each of the backends wrapped with an async put **/
	TArray<TUniquePtr<FDerivedDataBackendInterface> > AsyncPutInnerBackends;
	/** As an optimization, we check our writable status at contruction **/
	bool bIsWritable;
	bool bHasLocalBackends;
	bool bHasRemoteBackends;
	bool bHasMultipleLocalBackends;
	bool bHasMultipleRemoteBackends;
	bool bHasWritableLocalBackends;
	bool bHasWritableRemoteBackends;
};

} // UE::DerivedData::Backends

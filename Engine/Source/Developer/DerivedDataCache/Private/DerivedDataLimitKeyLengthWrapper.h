// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DerivedDataBackendInterface.h"
#include "ProfilingDebugging/CookStats.h"
#include "DerivedDataCacheUsageStats.h"
#include "Misc/SecureHash.h"

namespace UE::DerivedData::Backends
{

/** 
 * A backend wrapper that limits the key size and uses hashing...in this case it wraps the payload and the payload contains the full key to verify the integrity of the hash
**/
class FDerivedDataLimitKeyLengthWrapper : public FDerivedDataBackendInterface
{
public:

	/**
	 * Constructor
	 *
	 * @param	InInnerBackend	Backend to use for storage, my responsibilities are about key length
	 */
	FDerivedDataLimitKeyLengthWrapper(FDerivedDataBackendInterface* InInnerBackend, int32 InMaxKeyLength)
		: InnerBackend(InInnerBackend)
		, MaxKeyLength(InMaxKeyLength)
	{
		check(InnerBackend);
	}

	/** Return a name for this interface */
	virtual FString GetName() const override 
	{ 
		return FString::Printf(TEXT("LimitKeyLengthWrapper (%s)"), *InnerBackend->GetName());
	}

	/** return true if this cache is writable **/
	virtual bool IsWritable() const override
	{
		return InnerBackend->IsWritable();
	}

	/** Returns a class of speed for this interface **/
	virtual ESpeedClass GetSpeedClass() const override
	{
		return InnerBackend->GetSpeedClass();
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
		FString NewKey;
		ShortenKey(CacheKey, NewKey);
		bool Result = InnerBackend->CachedDataProbablyExists(*NewKey);
		if (Result)
		{
			COOK_STAT(Timer.AddHit(0));
		}
		return Result;
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
		TArray<FString> NewKeys;
		NewKeys.Reserve(CacheKeys.Num());
		for (const FString& CacheKey : CacheKeys)
		{
			ShortenKey(*CacheKey, NewKeys.Emplace_GetRef());
		}
		TBitArray<> Result = InnerBackend->CachedDataProbablyExistsBatch(NewKeys);
		if (Result.CountSetBits() == CacheKeys.Num())
		{
			COOK_STAT(Timer.AddHit(0));
		}
		return Result;
	}

	/**
	 * Attempts to make sure the cached data will be available as optimally as possible. This is left up to the implementation to do
	 * @param	CacheKey	Alphanumeric+underscore key of this cache item
	 * @return				true if any steps were performed to optimize future retrieval
	 */
	virtual bool TryToPrefetch(TConstArrayView<FString> CacheKeys) override
	{
		COOK_STAT(auto Timer = UsageStats.TimePrefetch());
		TArray<FString> NewKeys;
		NewKeys.Reserve(CacheKeys.Num());
		for (const FString& CacheKey : CacheKeys)
		{
			ShortenKey(*CacheKey, NewKeys.Emplace_GetRef());
		}
		bool Result = InnerBackend->TryToPrefetch(NewKeys);
		if (Result)
		{
			COOK_STAT(Timer.AddHit(0));
		}

		return Result;
	}

	/*
		Determines if we have any interest in caching this data
	*/
	virtual bool WouldCache(const TCHAR* CacheKey, TArrayView<const uint8> InData) override
	{
		return InnerBackend->WouldCache(CacheKey, InData);
	}

	virtual bool ApplyDebugOptions(FBackendDebugOptions& InOptions) override 
	{ 
		return InnerBackend->ApplyDebugOptions(InOptions);
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
		int64 InnerGetCycles = 0;

		FString NewKey;
		bool bOk;
		if (!ShortenKey(CacheKey, NewKey))
		{
			// no shortening needed
			bOk = InnerBackend->GetCachedData(CacheKey, OutData);
		}
		else
		{
			bOk = InnerBackend->GetCachedData(*NewKey, OutData);
			if (bOk)
			{
				int32 KeyLen = FCString::Strlen(CacheKey) + 1;
				if (OutData.Num() < KeyLen)
				{
					UE_LOG(LogDerivedDataCache, Display, TEXT("FDerivedDataLimitKeyLengthWrapper: Short file or Hash Collision, ignoring and deleting %s."), CacheKey);
					bOk	= false;
				}
				else
				{
					int32 Compare = FCStringAnsi::Strcmp(TCHAR_TO_ANSI(CacheKey), (char*)&OutData[OutData.Num() - KeyLen]);
					OutData.RemoveAt(OutData.Num() - KeyLen, KeyLen);
					if (Compare == 0)
					{
						UE_LOG(LogDerivedDataCache, VeryVerbose, TEXT("FDerivedDataLimitKeyLengthWrapper: cache hit, key match is ok %s"), CacheKey);
					}
					else
					{
						UE_LOG(LogDerivedDataCache, Warning, TEXT("FDerivedDataLimitKeyLengthWrapper: HASH COLLISION, ignoring and deleting %s."), CacheKey);
						bOk	= false;
					}
				}
				if (!bOk)
				{
					// _we_ detected corruption, so _we_ will force a flush of the corrupted data
					InnerBackend->RemoveCachedData(*NewKey, /*bTransient=*/ false);
				}
			}
		}
		if (!bOk)
		{
			OutData.Empty();
		}
		else
		{
			COOK_STAT(Timer.AddHit(OutData.Num()));
		}
		return bOk;
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
		if (!InnerBackend->IsWritable())
		{
			return EPutStatus::NotCached; // no point in continuing down the chain
		}
		COOK_STAT(Timer.AddHit(InData.Num()));
		FString NewKey;
		if (!ShortenKey(CacheKey, NewKey))
		{
			return InnerBackend->PutCachedData(CacheKey, InData, bPutEvenIfExists);
		}
		TArray<uint8> Data(InData.GetData(), InData.Num());
		check(Data.Num());
		int32 KeyLen = FCString::Strlen(CacheKey) + 1;
		checkf(int64(Data.Num()) + KeyLen <= INT32_MAX,
			TEXT("FDerivedDataLimitKeyLengthWrapper: shortening the key makes the data exceed 2 GiB for %s"), CacheKey);
		Data.AddUninitialized(KeyLen);
		FCStringAnsi::Strcpy((char*)&Data[Data.Num() - KeyLen], KeyLen, TCHAR_TO_ANSI(CacheKey));
		check(Data.Last()==0);
		return InnerBackend->PutCachedData(*NewKey, Data, bPutEvenIfExists);
	}

	virtual void RemoveCachedData(const TCHAR* CacheKey, bool bTransient) override
	{
		if (!InnerBackend->IsWritable())
		{
			return; // no point in continuing down the chain
		}
		FString NewKey;
		ShortenKey(CacheKey, NewKey);
		return InnerBackend->RemoveCachedData(*NewKey, bTransient);
	}

	virtual TSharedRef<FDerivedDataCacheStatsNode> GatherUsageStats() const override
	{
		TSharedRef<FDerivedDataCacheStatsNode> Usage =
			MakeShared<FDerivedDataCacheStatsNode>(TEXT("LimitKeyLength"), TEXT(""), InnerBackend->GetSpeedClass() == ESpeedClass::Local);
		Usage->Stats.Add(TEXT(""), UsageStats);
		Usage->Children.Add(InnerBackend->GatherUsageStats());
		return Usage;
	}

	virtual void Put(
		TConstArrayView<FCachePutRequest> Requests,
		IRequestOwner& Owner,
		FOnCachePutComplete&& OnComplete) override
	{
	#if ENABLE_COOK_STATS
		return InnerBackend->Put(Requests, Owner,
			[this, OnComplete = MoveTemp(OnComplete)](FCachePutResponse&& Response)
			{
				if (Response.Status == EStatus::Ok)
				{
					UsageStats.TimePut().AddHit(0);
				}
				if (OnComplete)
				{
					OnComplete(MoveTemp(Response));
				}
			});
	#else
		return InnerBackend->Put(Requests, Owner, MoveTemp(OnComplete));
	#endif
	}

	virtual void Get(
		TConstArrayView<FCacheGetRequest> Requests,
		IRequestOwner& Owner,
		FOnCacheGetComplete&& OnComplete) override
	{
	#if ENABLE_COOK_STATS
		return InnerBackend->Get(Requests, Owner,
			[this, OnComplete = MoveTemp(OnComplete)](FCacheGetResponse&& Response)
			{
				if (Response.Status == EStatus::Ok)
				{
					UsageStats.TimeGet().AddHit(0);
				}
				if (OnComplete)
				{
					OnComplete(MoveTemp(Response));
				}
			});
	#else
		return InnerBackend->Get(Requests, Owner, MoveTemp(OnComplete));
	#endif
	}

	virtual void PutValue(
		TConstArrayView<FCachePutValueRequest> Requests,
		IRequestOwner& Owner,
		FOnCachePutValueComplete&& OnComplete) override
	{
	#if ENABLE_COOK_STATS
		return InnerBackend->PutValue(Requests, Owner,
			[this, OnComplete = MoveTemp(OnComplete)](FCachePutValueResponse&& Response)
			{
				if (Response.Status == EStatus::Ok)
				{
					UsageStats.TimePut().AddHit(0);
				}
				if (OnComplete)
				{
					OnComplete(MoveTemp(Response));
				}
			});
	#else
		return InnerBackend->PutValue(Requests, Owner, MoveTemp(OnComplete));
	#endif
	}

	virtual void GetValue(
		TConstArrayView<FCacheGetValueRequest> Requests,
		IRequestOwner& Owner,
		FOnCacheGetValueComplete&& OnComplete) override
	{
	#if ENABLE_COOK_STATS
		return InnerBackend->GetValue(Requests, Owner,
			[this, OnComplete = MoveTemp(OnComplete)](FCacheGetValueResponse&& Response)
			{
				if (Response.Status == EStatus::Ok)
				{
					UsageStats.TimeGet().AddHit(0);
				}
				if (OnComplete)
				{
					OnComplete(MoveTemp(Response));
				}
			});
	#else
		return InnerBackend->GetValue(Requests, Owner, MoveTemp(OnComplete));
	#endif
	}

	virtual void GetChunks(
		TConstArrayView<FCacheChunkRequest> Requests,
		IRequestOwner& Owner,
		FOnCacheChunkComplete&& OnComplete) override
	{
	#if ENABLE_COOK_STATS
		return InnerBackend->GetChunks(Requests, Owner,
			[this, OnComplete = MoveTemp(OnComplete)](FCacheChunkResponse&& Response)
			{
				if (Response.Status == EStatus::Ok)
				{
					UsageStats.TimeGet().AddHit(0);
				}
				if (OnComplete)
				{
					OnComplete(MoveTemp(Response));
				}
			});
	#else
		return InnerBackend->GetChunks(Requests, Owner, MoveTemp(OnComplete));
	#endif
	}

private:
	FDerivedDataCacheUsageStats UsageStats;

	/** Shorten the cache key and return true if shortening was required **/
	bool ShortenKey(const TCHAR* CacheKey, FString& Result)
	{
		Result = FString(CacheKey);
		if (Result.Len() <= MaxKeyLength)
		{
			return false;
		}

		FSHA1 HashState;
		int32 Length = Result.Len();
		HashState.Update((const uint8*)&Length, sizeof(int32));

		auto ResultSrc = StringCast<UCS2CHAR>(*Result);

		// This is pretty redundant. Incorporating the CRC of the name into the hash
		// which also ends up computing SHA1 of the name is not really going to make 
		// any meaningful difference to the strength of the key so it's just a waste
		// of CPU. However it's difficult to get rid of without invalidating the DDC
		// contents so here we are.
		const uint32 CRCofPayload(FCrc::MemCrc32(ResultSrc.Get(), Length * sizeof(UCS2CHAR)));
		HashState.Update((const uint8*)&CRCofPayload, sizeof(uint32));

		HashState.Update((const uint8*)ResultSrc.Get(), Length * sizeof(UCS2CHAR));

		HashState.Final();
		uint8 Hash[FSHA1::DigestSize];
		HashState.GetHash(Hash);
		const FString HashString = BytesToHex(Hash, FSHA1::DigestSize);

		int32 HashStringSize = HashString.Len();
		int32 OriginalPart = MaxKeyLength - HashStringSize - 2;
		Result = Result.Left(OriginalPart) + TEXT("__") + HashString;
		check(Result.Len() == MaxKeyLength && Result.Len() > 0);
		UE_LOG(LogDerivedDataCache, VeryVerbose, TEXT("ShortenKey %s -> %s"), CacheKey, *Result);
		return true;
	}

	/** Backend to use for storage, my responsibilities are about key length **/
	FDerivedDataBackendInterface* InnerBackend;

	/** Maximum size of the backend key length **/
	int32 MaxKeyLength;
};

} // UE::DerivedData::Backends

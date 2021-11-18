// Copyright Epic Games, Inc. All Rights Reserved.

#include "HTTPResponseCache.h"
#include "Player/PlayerSessionServices.h"
#include "Interfaces/IHttpResponse.h"


namespace Electra
{
	const TCHAR* const OptionKeyResponseCacheMaxEntries = TEXT("httpcache_max_entries");
	const TCHAR* const OptionKeyResponseCacheMaxByteSize = TEXT("httpcache_max_bytesize");


class FHTTPResponseCache : public IHTTPResponseCache
{
public:
	FHTTPResponseCache(IPlayerSessionServices* SessionServices, const FParamDict& Options);
	virtual ~FHTTPResponseCache();

	virtual void HandleEntityExpiration() override;
	virtual void CacheEntity(TSharedPtrTS<FCacheItem> EntityToAdd) override;
	virtual bool GetCachedEntity(TSharedPtrTS<FCacheItem>& OutCachedEntity, const FString& URL, const IElectraHttpManager::FParams::FRange& Range) override;

private:
	void EvictToAddSize(int64 ResponseSize);
	void RemoveIfExists(TSharedPtrTS<FCacheItem> Entity);

	IPlayerSessionServices* SessionServices = nullptr;
	int64 MaxElementSize = 0;
	int32 MaxNumElements = 0;

	int64 SizeInUse = 0;

	mutable FCriticalSection Lock;
	TArray<TSharedPtrTS<FCacheItem>> Cache;
};

/***************************************************************************************************************************************************/
/***************************************************************************************************************************************************/
/***************************************************************************************************************************************************/

TSharedPtrTS<IHTTPResponseCache> IHTTPResponseCache::Create(IPlayerSessionServices* SessionServices, const FParamDict& Options)
{
	return MakeSharedTS<FHTTPResponseCache>(SessionServices, Options);
}

/***************************************************************************************************************************************************/
/***************************************************************************************************************************************************/
/***************************************************************************************************************************************************/

FHTTPResponseCache::FHTTPResponseCache(IPlayerSessionServices* InSessionServices, const FParamDict& InOptions)
	: SessionServices(InSessionServices)
{
	int64 v;
	v = InOptions.GetValue(OptionKeyResponseCacheMaxByteSize).SafeGetInt64(0);
	if (v >= 0)
	{
		MaxElementSize = (int64)v;
	}
	// Max number of elements is probably not used a lot, if at all.
	// If not specified we allow for some reasonable number.
	v = InOptions.GetValue(OptionKeyResponseCacheMaxEntries).SafeGetInt64(8192);
	if (v >= 0)
	{
		MaxNumElements = (int32) v;
	}
}

FHTTPResponseCache::~FHTTPResponseCache()
{
	FScopeLock ScopeLock(&Lock);
	Cache.Empty();
}


void FHTTPResponseCache::EvictToAddSize(int64 ResponseSize)
{
	FScopeLock ScopeLock(&Lock);
	while(Cache.Num() && (MaxElementSize - SizeInUse < ResponseSize || Cache.Num() >= MaxNumElements))
	{
		TSharedPtrTS<FCacheItem> Item = Cache.Pop();
		SizeInUse -= Item->Response->GetContent().Num();
	}
}

void FHTTPResponseCache::RemoveIfExists(TSharedPtrTS<FCacheItem> Entity)
{
	FScopeLock ScopeLock(&Lock);
	TSharedPtrTS<FCacheItem> Item;
	for(int32 i=0, iMax=Cache.Num(); i<iMax; ++i)
	{
		if (Cache[i]->URL.Equals(Entity->URL) && Cache[i]->Range.Equals(Entity->Range))
		{
			SizeInUse -= Cache[i]->Response->GetContent().Num();
			Cache.RemoveAt(i);
			break;
		}
	}
}


void FHTTPResponseCache::HandleEntityExpiration()
{
}


void FHTTPResponseCache::CacheEntity(TSharedPtrTS<FCacheItem> EntityToAdd)
{
	if (EntityToAdd.IsValid())
	{
		RemoveIfExists(EntityToAdd);
		int64 SizeRequired = EntityToAdd->Response->GetContent().Num();
		EvictToAddSize(SizeRequired);
		FScopeLock ScopeLock(&Lock);
		if (SizeInUse + SizeRequired <= MaxElementSize && Cache.Num() < MaxNumElements)
		{
			Cache.Insert(MoveTemp(EntityToAdd), 0);
			SizeInUse += SizeRequired;
		}
	}
}

bool FHTTPResponseCache::GetCachedEntity(TSharedPtrTS<FCacheItem>& OutCachedEntity, const FString& URL, const IElectraHttpManager::FParams::FRange& Range)
{
	FScopeLock ScopeLock(&Lock);
	TSharedPtrTS<FCacheItem> Item;
	for(int32 i=0, iMax=Cache.Num(); i<iMax; ++i)
	{
		if (Cache[i]->URL.Equals(URL) && Cache[i]->Range.Equals(Range))
		{
			Item = Cache[i];
			// Move to the beginning
			if (i > 0)
			{
				Cache.RemoveAt(i);
				Cache.Insert(Item, 0);
			}
			OutCachedEntity = MoveTemp(Item);
			return true;
		}
	}
	return false;
}




} // namespace Electra


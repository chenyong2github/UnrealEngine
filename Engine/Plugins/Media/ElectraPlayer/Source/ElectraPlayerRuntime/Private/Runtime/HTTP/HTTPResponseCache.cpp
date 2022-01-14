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
	virtual EScatterResult GetScatteredCacheEntity(TSharedPtrTS<FCacheItem>& OutScatteredCachedEntity, const FString& URL, const IElectraHttpManager::FParams::FRange& Range) override;

private:
	class FResponseFromCache : public IHttpResponse
	{
	public:
		virtual ~FResponseFromCache() = default;
		FResponseFromCache() = default;

		virtual int32 GetResponseCode() const override
		{ return HTTPResponseCode; }

		virtual FString GetContentAsString() const override
		{ check(!"do not call"); return FString(); }

		virtual FString GetURL() const override
		{ return URL; }

		virtual FString GetURLParameter(const FString& ParameterName) const override
		{ check(!"do not call"); return FString(); }

		virtual FString GetHeader(const FString& HeaderName) const override
		{ check(!"do not call"); return FString(); }

		virtual TArray<FString> GetAllHeaders() const override
		{ return Headers; }

		virtual FString GetContentType() const override
		{ return GetHeader(TEXT("Content-Type")); }

		virtual int32 GetContentLength() const override
		{ return Payload.Num(); }

		virtual const TArray<uint8>& GetContent() const override
		{ return Payload; }

		void CopyHeadersFromExceptContentSizes(FHttpResponsePtr CachedResponse)
		{
			check(CachedResponse);
			if (CachedResponse.IsValid())
			{
				// Copy all headers except for those containing the content size or length.
				for(auto &Header : CachedResponse->GetAllHeaders())
				{
					if (!Header.StartsWith(TEXT("Content-Length:"), ESearchCase::IgnoreCase) &&
						!Header.StartsWith(TEXT("Content-Range:"), ESearchCase::IgnoreCase))
					{
						Headers.Emplace(MoveTemp(Header));
					}
				}
			}
		}
		void AddHeader(FString InHeader)
		{ Headers.Emplace(MoveTemp(InHeader)); }

		void SetPayload(TArray<uint8> InPayload)
		{ Payload = MoveTemp(InPayload); }

		void SetURL(const FString& InUrl)
		{ URL = InUrl; }

		void SetHTTPResponseCode(int32 InResponseCode)
		{ HTTPResponseCode = InResponseCode; }

	private:
		TArray<uint8> Payload;
		TArray<FString> Headers;
		FString URL;
		int32 HTTPResponseCode = 200;
	};

	void EvictToAddSize(int64 ResponseSize);

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


void FHTTPResponseCache::HandleEntityExpiration()
{
}


void FHTTPResponseCache::CacheEntity(TSharedPtrTS<FCacheItem> EntityToAdd)
{
	if (EntityToAdd.IsValid())
	{
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

FHTTPResponseCache::EScatterResult FHTTPResponseCache::GetScatteredCacheEntity(TSharedPtrTS<FCacheItem>& OutScatteredCachedEntity, const FString& URL, const IElectraHttpManager::FParams::FRange& InRange)
{
	FScopeLock lock(&Lock);
	// Get a list of all cached blocks for this URL.
	TArray<TSharedPtrTS<FCacheItem>> CachedBlocks;
	for(auto &Entry : Cache)
	{
		if (Entry->URL.Equals(URL))
		{
			CachedBlocks.Add(Entry);
		}
	}

	IElectraHttpManager::FParams::FRange Range(InRange);

	// Set up the default output that encompasses the request.
	// This is what needs to be fetched for cache misses.
	OutScatteredCachedEntity = MakeSharedTS<IHTTPResponseCache::FCacheItem>();
	OutScatteredCachedEntity->URL = URL;
	OutScatteredCachedEntity->Range = Range;

	// Quick out if there are no cached blocks at all.
	if (CachedBlocks.Num() == 0)
	{
		return EScatterResult::Miss;
	}

	// Adjust the input range to valid values.
	Range.DocumentSize = CachedBlocks[0]->Range.GetDocumentSize();
	if (Range.GetStart() < 0)
	{
		Range.SetStart(0);
	}
	if (Range.GetEndIncluding() < 0)
	{
		Range.SetEndIncluding(Range.GetDocumentSize() - 1);
	}

	// Sort the blocks by ascending range.
	CachedBlocks.Sort([](const TSharedPtrTS<FCacheItem>& e1, const TSharedPtrTS<FCacheItem>& e2)
	{
		return e1->Range.GetStart() < e2->Range.GetStart();
	});

	// Find a cached block that overlaps the requested range.
	int64 rs = Range.GetStart();
	int64 re = Range.GetEndIncluding();
	int32 firstIndex;
	for(firstIndex=0; firstIndex<CachedBlocks.Num(); ++firstIndex)
	{
		// Any overlap?
		const int64 cbstart = CachedBlocks[firstIndex]->Range.GetStart();
		const int64 cbend = CachedBlocks[firstIndex]->Range.GetEndIncluding();
		if (re >= cbstart && rs <= cbend)
		{
			// Is the request start before the cached block, ie we need to get additional data at the beginning?
			if (rs < cbstart)
			{
				// The end of the requested range may also not be covered by the cache, but that is not relevant
				// here as we move forward sequentially, one overlap at a time.
				// Data up to the beginning of this cached block must be fetched first.
				OutScatteredCachedEntity->Range = Range;
				OutScatteredCachedEntity->Range.SetEndIncluding(cbstart - 1);
				return EScatterResult::PartialHit;
			}
			else
			{
				break;
			}
		}
	}
	// No overlap anywhere?
	if (firstIndex == CachedBlocks.Num())
	{
		return EScatterResult::Miss;
	}

	// Exact match?
	if (rs == CachedBlocks[firstIndex]->Range.GetStart() && re == CachedBlocks[firstIndex]->Range.GetEndIncluding())
	{
		OutScatteredCachedEntity = CachedBlocks[firstIndex];
		Cache.Remove(CachedBlocks[firstIndex]);
		Cache.Insert(CachedBlocks[firstIndex], 0);
		return EScatterResult::FullHit;
	}

	// Set up the partial cache response.
	TSharedPtr<FResponseFromCache, ESPMode::ThreadSafe> Response = MakeShared<FResponseFromCache, ESPMode::ThreadSafe>();
	Response->SetURL(URL);
	Response->CopyHeadersFromExceptContentSizes(CachedBlocks[firstIndex]->Response);

	// There is some overlap. Find how many consecutive bytes we can get from the cached blocks.
	TArray<uint8> Payload;
	TArray<TSharedPtrTS<FCacheItem>> TouchedBlocks;
	while(1)
	{
		int64 cbstart = CachedBlocks[firstIndex]->Range.GetStart();
		int64 cbend = CachedBlocks[firstIndex]->Range.GetEndIncluding();
		int64 last = re < cbend ? re : cbend;
		int64 offset = rs - cbstart;
		int64 numAvail = last + 1 - cbstart - offset;

		// Copy the response data into the gathering block.
		const TArray<uint8>& CachedContent = CachedBlocks[firstIndex]->Response->GetContent();
		Payload.Append(CachedContent.GetData() + offset, numAvail);

		// Remember which blocks we just touched.
		TouchedBlocks.Add(CachedBlocks[firstIndex]);

		// Move up the start to the end of this cached block.
		rs = last + 1;

		// End fully included?
		if (re <= cbend)
		{
			break;
		}
		// Is there another cached block?
		if (++firstIndex >= CachedBlocks.Num())
		{
			break;
		}
		// Does its start coincide with what we need next?
		if (CachedBlocks[firstIndex]->Range.GetStart() != rs)
		{
			break;
		}
	}

	// Move the blocks we touched to the front of the cached blocks (LRU).
	for(auto &Touched : TouchedBlocks)
	{
		Cache.Remove(Touched);
		Cache.Insert(Touched, 0);
	}

	Range.SetEndIncluding(rs - 1);
	if (InRange.IsSet())
	{
		Response->AddHeader(FString::Printf(TEXT("Content-Range: bytes %lld-%lld/%lld"), (long long int)Range.GetStart(), (long long int)Range.GetEndIncluding(), (long long int)Range.GetDocumentSize()));
		Response->SetHTTPResponseCode(206);
	}
	Response->AddHeader(FString::Printf(TEXT("Content-Length: %lld"), (long long int)Payload.Num()));
	Response->SetPayload(MoveTemp(Payload));
	OutScatteredCachedEntity->Response = Response;
	OutScatteredCachedEntity->Range = Range;
	return EScatterResult::PartialHit;
}


} // namespace Electra


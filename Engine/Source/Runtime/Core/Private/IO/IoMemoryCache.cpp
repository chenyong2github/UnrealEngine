// Copyright Epic Games, Inc. All Rights Reserved.

#include "IO/IoMemoryCache.h"

#include "Containers/IntrusiveDoubleLinkedList.h"
#include "IO/IoCache.h"
#include "IO/IoDispatcher.h"
#include "IO/IoHash.h"
#include "Misc/ScopeLock.h"

#if !UE_BUILD_SHIPPING

namespace UE::IO::Private
{

using namespace UE::Tasks;

////////////////////////////////////////////////////////////////////////////////
class FMemoryIoCache
	: public IIoCache
{
	struct FCacheEntry
		: public TIntrusiveDoubleLinkedListNode<FCacheEntry>
	{
		FIoHash Key;
		FIoBuffer Buffer;
	};

	using FCacheEntryList = TIntrusiveDoubleLinkedList<FCacheEntry>;
	using FCacheEntryLookup = TMap<FIoHash, TUniquePtr<FCacheEntry>>;

public:
	explicit FMemoryIoCache(uint64 CacheSize);
	virtual ~FMemoryIoCache();
	virtual bool ContainsChunk(const FIoHash& Key) const override;
	virtual TTask<TIoStatusOr<FIoBuffer>> GetChunk(const FIoHash& Key, const FIoReadOptions& Options, const FIoCancellationToken* CancellationToken) override;
	virtual FIoStatus PutChunk(const FIoHash& Key, FMemoryView Data) override;

private:
	const uint64 MaxCacheSize;
	uint64 TotalCacheSize;
	FCacheEntryLookup Lookup;
	FCacheEntryList Lru;
	mutable FCriticalSection CacheCS;
};

FMemoryIoCache::FMemoryIoCache(uint64 CacheSize)
	: MaxCacheSize(CacheSize)
	, TotalCacheSize(0)
{
}

FMemoryIoCache::~FMemoryIoCache()
{
}

bool FMemoryIoCache::ContainsChunk(const FIoHash& Key) const
{
	FScopeLock _(&CacheCS);
	return Lookup.Contains(Key);
}

TTask<TIoStatusOr<FIoBuffer>> FMemoryIoCache::GetChunk(const FIoHash& Key, const FIoReadOptions& Options, const FIoCancellationToken* CancellationToken)
{
	return UE::Tasks::Launch(UE_SOURCE_LOCATION, [this, Key, Options, CancellationToken]()
	{
		FScopeLock _(&CacheCS);

		if (TUniquePtr<FCacheEntry>* Entry = Lookup.Find(Key))
		{
			FCacheEntry* CacheEntry = Entry->Get();
			Lru.Remove(CacheEntry);
			Lru.AddHead(CacheEntry);

			if (CancellationToken && CancellationToken->IsCancelled())
			{
				return TIoStatusOr<FIoBuffer>(FIoStatus(EIoErrorCode::Cancelled));
			}

			const uint64 ReadOffset = Options.GetOffset();
			const uint64 ReadSize = FMath::Min(Options.GetSize(), CacheEntry->Buffer.GetSize());
			FIoBuffer Buffer = Options.GetTargetVa() ? FIoBuffer(FIoBuffer::Wrap, Options.GetTargetVa(), ReadSize) : FIoBuffer(ReadSize);
			Buffer.GetMutableView().CopyFrom(CacheEntry->Buffer.GetView().RightChop(ReadOffset));

			return TIoStatusOr<FIoBuffer>(Buffer);
		}

		return TIoStatusOr<FIoBuffer>(FIoStatus(EIoErrorCode::NotFound));
	});
}

FIoStatus FMemoryIoCache::PutChunk(const FIoHash& Key, FMemoryView Data)
{
	FScopeLock _(&CacheCS);

	if (Lookup.Contains(Key))
	{
		return FIoStatus::Ok;
	}

	while ((TotalCacheSize + Data.GetSize()) > MaxCacheSize)
	{
		FCacheEntry* Entry = Lru.PopTail();
		check(Entry);
		TotalCacheSize -= Entry->Buffer.GetSize();
		const FIoHash TmpKey = Entry->Key;
		Lookup.Remove(TmpKey);
	}

	TUniquePtr<FCacheEntry>& NewEntry = Lookup.Add(Key, MakeUnique<FCacheEntry>());
	NewEntry->Key = Key;
	NewEntry->Buffer = FIoBuffer(FIoBuffer::Clone, Data);
	Lru.AddHead(NewEntry.Get());

	return FIoStatus::Ok;
}

} // namespace UE::IO::Private

////////////////////////////////////////////////////////////////////////////////
TUniquePtr<IIoCache> MakeMemoryIoCache(uint64 CacheSize)
{
	return MakeUnique<UE::IO::Private::FMemoryIoCache>(CacheSize);
}

#endif // !UE_BUILD_SHIPPING

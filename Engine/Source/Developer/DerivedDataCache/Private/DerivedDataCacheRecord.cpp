// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataCacheRecord.h"

#include "Containers/UnrealString.h"
#include "DerivedDataCacheKey.h"
#include "HAL/CriticalSection.h"
#include "Misc/ScopeExit.h"
#include "Misc/ScopeRWLock.h"
#include "Misc/StringBuilder.h"
#include "Serialization/CompactBinary.h"
#include "UObject/NameTypes.h"
#include <atomic>

namespace UE::DerivedData::Private
{

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FCacheRecordBuilderInternal final : public ICacheRecordBuilderInternal
{
public:
	explicit FCacheRecordBuilderInternal(const FCacheKey& Key);
	~FCacheRecordBuilderInternal() final = default;

	void SetMeta(FCbObject&& Meta) final;

	FPayloadId SetValue(const FSharedBuffer& Buffer, const FPayloadId& Id) final;
	FPayloadId SetValue(const FPayload& Payload) final;

	FPayloadId AddAttachment(const FSharedBuffer& Buffer, const FPayloadId& Id) final;
	FPayloadId AddAttachment(const FPayload& Payload) final;

	FCacheRecord Build() final;
	FRequest BuildAsync(FOnCacheRecordComplete&& OnComplete, EPriority Priority) final;

	FCacheKey Key;
	FCbObject Meta;
	FPayload Value;
	TArray<FPayload> Attachments;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FCacheRecordInternal final : public ICacheRecordInternal
{
public:
	FCacheRecordInternal() = default;
	explicit FCacheRecordInternal(FCacheRecordBuilderInternal&& RecordBuilder);

	~FCacheRecordInternal() final = default;

	const FCacheKey& GetKey() const final;
	const FCbObject& GetMeta() const final;

	FSharedBuffer GetValue() const final;
	const FPayload& GetValuePayload() const final;

	FSharedBuffer GetAttachment(const FPayloadId& Id) const final;
	const FPayload& GetAttachmentPayload(const FPayloadId& Id) const final;
	TConstArrayView<FPayload> GetAttachmentPayloads() const final;

	const FPayload& GetPayload(const FPayloadId& Id) const final;

	inline void AddRef() const final
	{
		ReferenceCount.fetch_add(1, std::memory_order_relaxed);
	}

	inline void Release() const final
	{
		if (ReferenceCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
		{
			delete this;
		}
	}

private:
	mutable std::atomic<uint32> ReferenceCount{0};
	FCacheKey Key;
	FCbObject Meta;
	FPayload Value;
	TArray<FPayload> Attachments;
	mutable FRWLock CacheLock;
	mutable FSharedBuffer ValueCache;
	mutable TArray<FSharedBuffer> AttachmentsCache;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FPayloadId GetOrCreatePayloadId(const FPayloadId& Id, const FIoHash& RawHash)
{
	return Id.IsValid() ? Id : FPayloadId::FromHash(RawHash);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FCacheRecordInternal::FCacheRecordInternal(FCacheRecordBuilderInternal&& RecordBuilder)
	: Key(RecordBuilder.Key)
	, Meta(MoveTemp(RecordBuilder.Meta))
	, Value(MoveTemp(RecordBuilder.Value))
	, Attachments(MoveTemp(RecordBuilder.Attachments))
{
}

const FCacheKey& FCacheRecordInternal::GetKey() const
{
	return Key;
}

const FCbObject& FCacheRecordInternal::GetMeta() const
{
	return Meta;
}

FSharedBuffer FCacheRecordInternal::GetValue() const
{
	if (Value.IsNull())
	{
		return FSharedBuffer();
	}
	if (FReadScopeLock Lock(CacheLock); ValueCache)
	{
		return ValueCache;
	}
	FWriteScopeLock Lock(CacheLock);
	if (ValueCache)
	{
		return ValueCache;
	}
	ValueCache = Value.GetData().Decompress();
	return ValueCache;
}

const FPayload& FCacheRecordInternal::GetValuePayload() const
{
	return Value;
}

FSharedBuffer FCacheRecordInternal::GetAttachment(const FPayloadId& Id) const
{
	const int32 Index = Algo::BinarySearchBy(Attachments, Id, &FPayload::GetId);
	if (Attachments.IsValidIndex(Index))
	{
		if (FReadScopeLock Lock(CacheLock); AttachmentsCache.IsValidIndex(Index) && AttachmentsCache[Index])
		{
			return AttachmentsCache[Index];
		}
		FWriteScopeLock Lock(CacheLock);
		if (AttachmentsCache.IsEmpty())
		{
			AttachmentsCache.SetNum(Attachments.Num());
		}
		FSharedBuffer& Cache = AttachmentsCache[Index];
		if (Cache.IsNull())
		{
			Cache = Attachments[Index].GetData().Decompress();
		}
		return Cache;
	}
	return FSharedBuffer();
}

const FPayload& FCacheRecordInternal::GetAttachmentPayload(const FPayloadId& Id) const
{
	const int32 Index = Algo::BinarySearchBy(Attachments, Id, &FPayload::GetId);
	return Attachments.IsValidIndex(Index) ? Attachments[Index] : FPayload::Null;
}

TConstArrayView<FPayload> FCacheRecordInternal::GetAttachmentPayloads() const
{
	return Attachments;
}

const FPayload& FCacheRecordInternal::GetPayload(const FPayloadId& Id) const
{
	return Value.GetId() == Id ? Value : GetAttachmentPayload(Id);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FCacheRecordBuilderInternal::FCacheRecordBuilderInternal(const FCacheKey& InKey)
	: Key(InKey)
{
}

void FCacheRecordBuilderInternal::SetMeta(FCbObject&& InMeta)
{
	Meta = MoveTemp(InMeta);
	Meta.MakeOwned();
}

FPayloadId FCacheRecordBuilderInternal::SetValue(const FSharedBuffer& Buffer, const FPayloadId& Id)
{
	FCompressedBuffer CompressedBuffer = FCompressedBuffer::Compress(NAME_Default, Buffer);
	const FPayloadId ValueId = GetOrCreatePayloadId(Id, CompressedBuffer.GetRawHash());
	return SetValue(FPayload(ValueId, MoveTemp(CompressedBuffer)));
}

FPayloadId FCacheRecordBuilderInternal::SetValue(const FPayload& Payload)
{
	checkf(Payload, TEXT("Failed to set value on %s because the payload is null."), *WriteToString<96>(Key));
	const FPayloadId& Id = Payload.GetId();
	checkf(Value.IsNull(),
		TEXT("Cache: Failed to set value on %s with ID %s because it has an existing value with ID %s."),
		*WriteToString<96>(Key), *WriteToString<32>(Id), *WriteToString<32>(Value.GetId()));
	checkf(Algo::BinarySearchBy(Attachments, Id, &FPayload::GetId) == INDEX_NONE,
		TEXT("Failed to set on %s with ID %s because it has an existing attachment with that ID."),
		*WriteToString<96>(Key), *WriteToString<32>(Id));
	Value = Payload;
	return Id;
}

FPayloadId FCacheRecordBuilderInternal::AddAttachment(const FSharedBuffer& Buffer, const FPayloadId& Id)
{
	FCompressedBuffer CompressedBuffer = FCompressedBuffer::Compress(NAME_Default, Buffer);
	const FPayloadId AttachmentId = GetOrCreatePayloadId(Id, CompressedBuffer.GetRawHash());
	return AddAttachment(FPayload(AttachmentId, MoveTemp(CompressedBuffer)));
}

FPayloadId FCacheRecordBuilderInternal::AddAttachment(const FPayload& Payload)
{
	checkf(Payload, TEXT("Failed to add attachment on %s because the payload is null."), *WriteToString<96>(Key));
	const FPayloadId& Id = Payload.GetId();
	const int32 Index = Algo::LowerBoundBy(Attachments, Id, &FPayload::GetId);
	checkf(!(Attachments.IsValidIndex(Index) && Attachments[Index].GetId() == Id) && Value.GetId() != Id,
		TEXT("Failed to add attachment on %s with ID %s because it has an existing attachment or value with that ID."),
		*WriteToString<96>(Key), *WriteToString<32>(Id));
	Attachments.Insert(Payload, Index);
	return Id;
}

FCacheRecord FCacheRecordBuilderInternal::Build()
{
	return CreateCacheRecord(new FCacheRecordInternal(MoveTemp(*this)));
}

FRequest FCacheRecordBuilderInternal::BuildAsync(FOnCacheRecordComplete&& OnComplete, EPriority Priority)
{
	ON_SCOPE_EXIT { delete this; };
	checkf(OnComplete, TEXT("Failed to build cache record for %s because the completion callback is null."),
		*WriteToString<96>(Key));
	OnComplete(Build());
	return FRequest();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FCacheRecord CreateCacheRecord(ICacheRecordInternal* Record)
{
	return FCacheRecord(Record);
}

FCacheRecordBuilder CreateCacheRecordBuilder(ICacheRecordBuilderInternal* RecordBuilder)
{
	return FCacheRecordBuilder(RecordBuilder);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FCacheRecordBuilder CreateCacheRecordBuilder(const FCacheKey& Key)
{
	return CreateCacheRecordBuilder(new FCacheRecordBuilderInternal(Key));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

} // UE::DerivedData::Private

// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataCacheRecord.h"

#include "Algo/Accumulate.h"
#include "DerivedDataCacheKey.h"
#include "DerivedDataCachePrivate.h"
#include "DerivedDataPayload.h"
#include "HAL/CriticalSection.h"
#include "Misc/ScopeExit.h"
#include "Misc/ScopeRWLock.h"
#include "Misc/StringBuilder.h"
#include "Serialization/CompactBinary.h"
#include "Serialization/CompactBinaryPackage.h"
#include "Serialization/CompactBinaryWriter.h"
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

	void SetValue(const FCompositeBuffer& Buffer, const FPayloadId& Id, uint64 BlockSize) final;
	void SetValue(const FSharedBuffer& Buffer, const FPayloadId& Id, uint64 BlockSize) final;
	void SetValue(const FPayload& Payload) final;

	void AddAttachment(const FCompositeBuffer& Buffer, const FPayloadId& Id, uint64 BlockSize) final;
	void AddAttachment(const FSharedBuffer& Buffer, const FPayloadId& Id, uint64 BlockSize) final;
	void AddAttachment(const FPayload& Payload) final;

	FCacheRecord Build() final;
	void BuildAsync(IRequestOwner& Owner, FOnCacheRecordComplete&& OnComplete) final;

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
	FCacheKey Key;
	FCbObject Meta;
	FPayload Value;
	TArray<FPayload> Attachments;
	mutable FRWLock CacheLock;
	mutable FSharedBuffer ValueCache;
	mutable TArray<FSharedBuffer> AttachmentsCache;
	mutable std::atomic<uint32> ReferenceCount{0};
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FPayloadId GetOrCreatePayloadId(const FPayloadId& Id, const FCompressedBuffer& Buffer)
{
	return Id.IsValid() ? Id : FPayloadId::FromHash(Buffer.GetRawHash());
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

void FCacheRecordBuilderInternal::SetValue(const FCompositeBuffer& Buffer, const FPayloadId& Id, const uint64 BlockSize)
{
	FCompressedBuffer Compressed = FPayload::Compress(Buffer, BlockSize);
	const FPayloadId ValueId = GetOrCreatePayloadId(Id, Compressed);
	return SetValue(FPayload(ValueId, MoveTemp(Compressed)));
}

void FCacheRecordBuilderInternal::SetValue(const FSharedBuffer& Buffer, const FPayloadId& Id, const uint64 BlockSize)
{
	FCompressedBuffer Compressed = FPayload::Compress(Buffer, BlockSize);
	const FPayloadId ValueId = GetOrCreatePayloadId(Id, Compressed);
	return SetValue(FPayload(ValueId, MoveTemp(Compressed)));
}

void FCacheRecordBuilderInternal::SetValue(const FPayload& Payload)
{
	checkf(Payload, TEXT("Failed to set value on %s because the payload is null."), *WriteToString<96>(Key));
	checkf(Value.IsNull(),
		TEXT("Failed to set value on %s with ID %s because it has an existing value with ID %s."),
		*WriteToString<96>(Key), *WriteToString<32>(Payload.GetId()), *WriteToString<32>(Value.GetId()));
	checkf(Algo::BinarySearchBy(Attachments, Payload.GetId(), &FPayload::GetId) == INDEX_NONE,
		TEXT("Failed to set value on %s with ID %s because it has an existing attachment with that ID."),
		*WriteToString<96>(Key), *WriteToString<32>(Payload.GetId()));
	Value = Payload;
}

void FCacheRecordBuilderInternal::AddAttachment(const FCompositeBuffer& Buffer, const FPayloadId& Id, const uint64 BlockSize)
{
	FCompressedBuffer Compressed = FPayload::Compress(Buffer, BlockSize);
	const FPayloadId AttachmentId = GetOrCreatePayloadId(Id, Compressed);
	return AddAttachment(FPayload(AttachmentId, MoveTemp(Compressed)));
}

void FCacheRecordBuilderInternal::AddAttachment(const FSharedBuffer& Buffer, const FPayloadId& Id, const uint64 BlockSize)
{
	FCompressedBuffer Compressed = FPayload::Compress(Buffer, BlockSize);
	const FPayloadId AttachmentId = GetOrCreatePayloadId(Id, Compressed);
	return AddAttachment(FPayload(AttachmentId, MoveTemp(Compressed)));
}

void FCacheRecordBuilderInternal::AddAttachment(const FPayload& Payload)
{
	checkf(Payload, TEXT("Failed to add attachment on %s because the payload is null."), *WriteToString<96>(Key));
	const FPayloadId& Id = Payload.GetId();
	const int32 Index = Algo::LowerBoundBy(Attachments, Id, &FPayload::GetId);
	checkf(!(Attachments.IsValidIndex(Index) && Attachments[Index].GetId() == Id) && Value.GetId() != Id,
		TEXT("Failed to add attachment on %s with ID %s because it has an existing attachment or value with that ID."),
		*WriteToString<96>(Key), *WriteToString<32>(Id));
	Attachments.Insert(Payload, Index);
}

FCacheRecord FCacheRecordBuilderInternal::Build()
{
	return CreateCacheRecord(new FCacheRecordInternal(MoveTemp(*this)));
}

void FCacheRecordBuilderInternal::BuildAsync(IRequestOwner& Owner, FOnCacheRecordComplete&& OnComplete)
{
	ON_SCOPE_EXIT { delete this; };
	checkf(OnComplete, TEXT("Failed to build cache record for %s because the completion callback is null."),
		*WriteToString<96>(Key));
	OnComplete(Build());
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FCacheRecord CreateCacheRecord(ICacheRecordInternal* Record)
{
	return FCacheRecord(Record);
}

} // UE::DerivedData::Private

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace UE::DerivedData
{

FCbPackage FCacheRecord::Save() const
{
	FCbPackage Package;
	FCbWriter Writer;
	Writer.BeginObject();
	{
		const FCacheKey& Key = GetKey();
		Writer.BeginObject("Key"_ASV);
		Writer.AddString("Bucket"_ASV, Key.Bucket.ToString());
		Writer.AddHash("Hash"_ASV, Key.Hash);
		Writer.EndObject();
	}

	if (const FCbObject& Meta = GetMeta())
	{
		Writer.AddObject("Meta"_ASV, Meta);
	}
	auto SavePayload = [&Package, &Writer](const FPayload& Payload)
	{
		if (Payload.HasData())
		{
			Package.AddAttachment(FCbAttachment(Payload.GetData()));
		}
		Writer.BeginObject();
		Writer.AddObjectId("Id"_ASV, Payload.GetId());
		Writer.AddBinaryAttachment("RawHash"_ASV, Payload.GetRawHash());
		Writer.AddInteger("RawSize"_ASV, Payload.GetRawSize());
		Writer.EndObject();
	};
	if (const FPayload& Value = GetValuePayload())
	{
		Writer.SetName("Value"_ASV);
		SavePayload(Value);
	}
	TConstArrayView<FPayload> Attachments = GetAttachmentPayloads();
	if (!Attachments.IsEmpty())
	{
		Writer.BeginArray("Attachments"_ASV);
		for (const FPayload& Attachment : Attachments)
		{
			SavePayload(Attachment);
		}
		Writer.EndArray();
	}
	Writer.EndObject();

	Package.SetObject(Writer.Save().AsObject());
	return Package;
}

FOptionalCacheRecord FCacheRecord::Load(const FCbPackage& Package, FCbObjectView RecordObject)
{
	FCacheKey Key;
	FCbObjectView KeyObject = RecordObject["Key"_ASV].AsObjectView();
	auto TrySetBucketName = [](FUtf8StringView Name, FCacheKey& Key)
	{
		if (Private::IsValidCacheBucketName(Name))
		{
			Key.Bucket = FCacheBucket(Name);
			return true;
		}
		return false;
	};
	if (!TrySetBucketName(KeyObject["Bucket"_ASV].AsString(), Key))
	{
		return FOptionalCacheRecord();
	}
	Key.Hash = KeyObject["Hash"_ASV].AsHash();

	FCacheRecordBuilder Builder(Key);

	Builder.SetMeta(FCbObject::Clone(RecordObject["Meta"_ASV].AsObjectView()));

	auto LoadPayload = [&Package](const FCbObjectView& PayloadObject)
	{
		const FPayloadId PayloadId = PayloadObject["Id"_ASV].AsObjectId();
		if (PayloadId.IsNull())
		{
			return FPayload();
		}
		const FIoHash RawHash = PayloadObject["RawHash"_ASV].AsHash();
		if (const FCbAttachment* Attachment = Package.FindAttachment(RawHash))
		{
			if (const FCompressedBuffer& Compressed = Attachment->AsCompressedBinary())
			{
				return FPayload(PayloadId, Compressed);
			}
		}
		const uint64 RawSize = PayloadObject["RawSize"_ASV].AsUInt64(MAX_uint64);
		if (!RawHash.IsZero() && RawSize != MAX_uint64)
		{
			return FPayload(PayloadId, RawHash, RawSize);
		}
		else
		{
			return FPayload();
		}
	};

	if (FCbObjectView ValueObject = RecordObject["Value"_ASV].AsObjectView())
	{
		FPayload Value = LoadPayload(ValueObject);
		if (!Value)
		{
			return FOptionalCacheRecord();
		}
		Builder.SetValue(Value);
	}

	for (FCbFieldView AttachmentField : RecordObject["Attachments"_ASV])
	{
		FPayload Attachment = LoadPayload(AttachmentField.AsObjectView());
		if (!Attachment)
		{
			return FOptionalCacheRecord();
		}
		Builder.AddAttachment(Attachment);
	}

	return Builder.Build();
}

FOptionalCacheRecord FCacheRecord::Load(const FCbPackage& Package)
{
	return Load(Package, Package.GetObject());
}

FCacheRecordBuilder::FCacheRecordBuilder(const FCacheKey& Key)
	: RecordBuilder(new Private::FCacheRecordBuilderInternal(Key))
{
}

} // UE::DerivedData

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace UE::DerivedData::Private
{

uint64 GetCacheRecordCompressedSize(const FCacheRecord& Record)
{
	const uint64 ValueSize = Record.GetValuePayload().GetData().GetCompressedSize();
	return int64(Algo::TransformAccumulate(Record.GetAttachmentPayloads(),
		[](const FPayload& Payload) { return Payload.GetData().GetCompressedSize(); }, ValueSize));
}

uint64 GetCacheRecordTotalRawSize(const FCacheRecord& Record)
{
	const uint64 ValueSize = Record.GetValuePayload().GetRawSize();
	return int64(Algo::TransformAccumulate(Record.GetAttachmentPayloads(), &FPayload::GetRawSize, ValueSize));
}

uint64 GetCacheRecordRawSize(const FCacheRecord& Record)
{
	const uint64 ValueSize = Record.GetValuePayload().GetData().GetRawSize();
	return int64(Algo::TransformAccumulate(Record.GetAttachmentPayloads(),
		[](const FPayload& Payload) { return Payload.GetData().GetRawSize(); }, ValueSize));
}

} // UE::DerivedData::Private

// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataCacheRecord.h"

#include "Containers/UnrealString.h"
#include "DerivedDataCacheKey.h"
#include "Misc/StringBuilder.h"
#include "Serialization/CompactBinary.h"

namespace UE
{
namespace DerivedData
{
namespace Private
{

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FCacheRecordBuilderInternal final : public ICacheRecordBuilderInternal
{
public:
	explicit FCacheRecordBuilderInternal(const FCacheKey& Key);
	virtual ~FCacheRecordBuilderInternal() = default;

	virtual void SetMeta(FCbObject&& Meta) final;

	virtual FPayloadId SetValue(const FSharedBuffer& Buffer, const FPayloadId& Id) final;
	virtual FPayloadId SetValue(FPayload&& Payload) final;

	virtual FPayloadId AddAttachment(const FSharedBuffer& Buffer, const FPayloadId& Id) final;
	virtual FPayloadId AddAttachment(FPayload&& Payload) final;

	virtual FCacheRecord Build() final;
	virtual FRequest BuildAsync(FOnCacheRecordComplete&& Callback, EPriority Priority) final;

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

	virtual ~FCacheRecordInternal() = default;

	virtual FCacheRecord Clone() const final;

	virtual const FCacheKey& GetKey() const final;
	virtual const FCbObject& GetMeta() const final;

	virtual FSharedBuffer GetValue() const final;
	virtual const FPayload& GetValuePayload() const final;

	virtual FSharedBuffer GetAttachment(const FPayloadId& Id) const final;
	virtual const FPayload& GetAttachmentPayload(const FPayloadId& Id) const final;
	virtual TConstArrayView<FPayload> GetAttachmentPayloads() const final;

	FCacheKey Key;
	FCbObject Meta;
	FPayload Value;
	TArray<FPayload> Attachments;
	mutable FSharedBuffer ValueCache;
	mutable TArray<FSharedBuffer> AttachmentsCache;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static const FPayload& GetEmptyCachePayload()
{
	static const FPayload Empty;
	return Empty;
}

static FPayloadId GetOrCreatePayloadId(const FPayloadId& Id, const FSharedBuffer& Buffer)
{
	return Id.IsValid() ? Id : FPayloadId::FromHash(FIoHash::HashBuffer(Buffer));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FCacheRecordInternal::FCacheRecordInternal(FCacheRecordBuilderInternal&& RecordBuilder)
	: Key(RecordBuilder.Key)
	, Meta(MoveTemp(RecordBuilder.Meta))
	, Value(MoveTemp(RecordBuilder.Value))
	, Attachments(MoveTemp(RecordBuilder.Attachments))
{
}

FCacheRecord FCacheRecordInternal::Clone() const
{
	return FCacheRecord(new FCacheRecordInternal(*this));
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
	if (ValueCache.IsNull() && Value.IsValid())
	{
		ValueCache = Value.GetBuffer().Decompress();
	}
	return ValueCache;
}

const FPayload& FCacheRecordInternal::GetValuePayload() const
{
	return Value;
}

FSharedBuffer FCacheRecordInternal::GetAttachment(const FPayloadId& Id) const
{
	const int32 Index = Algo::LowerBound(Attachments, Id, FPayloadLessById());
	if (Attachments.IsValidIndex(Index) && FPayloadEqualById()(Attachments[Index], Id))
	{
		if (AttachmentsCache.IsEmpty())
		{
			AttachmentsCache.SetNum(Attachments.Num());
		}
		FSharedBuffer& DataCache = AttachmentsCache[Index];
		if (!DataCache)
		{
			DataCache = Attachments[Index].GetBuffer().Decompress();
		}
		return DataCache;
	}
	return FSharedBuffer();
}

const FPayload& FCacheRecordInternal::GetAttachmentPayload(const FPayloadId& Id) const
{
	const int32 Index = Algo::LowerBound(Attachments, Id, FPayloadLessById());
	if (Attachments.IsValidIndex(Index) && FPayloadEqualById()(Attachments[Index], Id))
	{
		return Attachments[Index];
	}
	return GetEmptyCachePayload();
}

TConstArrayView<FPayload> FCacheRecordInternal::GetAttachmentPayloads() const
{
	return Attachments;
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
	const FPayloadId ValueId = GetOrCreatePayloadId(Id, Buffer);
	return SetValue(FPayload(ValueId, FCompressedBuffer::Compress(Buffer)));
}

FPayloadId FCacheRecordBuilderInternal::SetValue(FPayload&& Payload)
{
	checkf(Value.IsNull(), TEXT("Failed to set value with ID %s because an there is an existing value with ID %s"),
		*WriteToString<32>(Payload.GetId()), *WriteToString<32>(Value.GetId()));
	Value = MoveTemp(Payload);
	return Value.GetId();
}

FPayloadId FCacheRecordBuilderInternal::AddAttachment(const FSharedBuffer& Buffer, const FPayloadId& Id)
{
	const FPayloadId AttachmentId = GetOrCreatePayloadId(Id, Buffer);
	return AddAttachment(FPayload(AttachmentId, FCompressedBuffer::Compress(Buffer)));
}

FPayloadId FCacheRecordBuilderInternal::AddAttachment(FPayload&& Payload)
{
	const int32 Index = Algo::LowerBound(Attachments, Payload, FPayloadLessById());
	checkf(!Attachments.IsValidIndex(Index) || !FPayloadEqualById()(Attachments[Index], Payload),
		TEXT("Failed to add attachment with ID %s because an existing attachment is using that ID"),
		*WriteToString<32>(Payload.GetId()));
	Attachments.Insert(MoveTemp(Payload), Index);
	return Attachments[Index].GetId();
}

FCacheRecord FCacheRecordBuilderInternal::Build()
{
	return FCacheRecord(new FCacheRecordInternal(MoveTemp(*this)));
}

FRequest FCacheRecordBuilderInternal::BuildAsync(FOnCacheRecordComplete&& Callback, EPriority Priority)
{
	Callback(Build());
	return FRequest();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FCacheRecordBuilder CreateCacheRecordBuilder(const FCacheKey& Key)
{
	return FCacheRecordBuilder(new FCacheRecordBuilderInternal(Key));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

} // Private
} // DerivedData
} // UE

// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataCacheRecord.h"

#include "Async/Future.h"
#include "Misc/StringBuilder.h"
#include "Serialization/CompactBinary.h"

namespace UE
{
namespace DerivedData
{

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct FCacheRecordBuilderData
{
	FCacheKey Key;
	FCbObject Meta;
	FPayload Value;
	TArray<FPayload> Attachments;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct FCacheRecordData
{
	FCacheRecordData() = default;
	explicit FCacheRecordData(FCacheRecordBuilderData&& Builder);

	FCacheKey Key;
	FCbObject Meta;
	FPayload Value;
	FSharedBuffer ValueCache;
	TArray<FPayload> Attachments;
	TArray<FSharedBuffer> AttachmentsCache;
};

FCacheRecordData::FCacheRecordData(FCacheRecordBuilderData&& Builder)
	: Key(Builder.Key)
	, Meta(MoveTemp(Builder.Meta))
	, Value(MoveTemp(Builder.Value))
	, Attachments(MoveTemp(Builder.Attachments))
{
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static const FPayload& GetEmptyCachePayload()
{
	static const FPayload Empty;
	return Empty;
}

static const FCacheRecordData& GetEmptyCacheRecord()
{
	static const FCacheRecordData Empty;
	return Empty;
}

static FPayloadId GetOrCreatePayloadId(const FPayloadId& Id, const FSharedBuffer& Buffer)
{
	return Id.IsValid() ? Id : FPayloadId::FromHash(FIoHash::HashBuffer(Buffer));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FCacheRecord::FCacheRecord(const FCacheRecordData& InData)
	: Data(MakePimpl<FCacheRecordData>(InData))
{
}

FCacheRecord::FCacheRecord(FCacheRecordBuilderData&& Builder)
	: Data(MakePimpl<FCacheRecordData>(MoveTemp(Builder)))
{
}

FCacheRecord FCacheRecord::Clone() const
{
	return Data ? FCacheRecord(*Data) : FCacheRecord();
}

const FCacheKey& FCacheRecord::GetKey() const
{
	return (Data ? *Data : GetEmptyCacheRecord()).Key;
}

const FCbObject& FCacheRecord::GetMeta() const
{
	return (Data ? *Data : GetEmptyCacheRecord()).Meta;
}

FSharedBuffer FCacheRecord::GetValue() const
{
	if (Data)
	{
		if (Data->ValueCache.IsNull() && Data->Value.IsValid())
		{
			Data->ValueCache = Data->Value.GetBuffer().Decompress();
		}
		return Data->ValueCache;
	}
	return FSharedBuffer();
}

const FPayload& FCacheRecord::GetValuePayload() const
{
	return (Data ? *Data : GetEmptyCacheRecord()).Value;
}

FSharedBuffer FCacheRecord::GetAttachment(const FPayloadId& Id) const
{
	TArray<FPayload>& Attachments = Data->Attachments;
	const int32 Index = Algo::LowerBound(Attachments, Id, FPayloadLessById());
	if (Attachments.IsValidIndex(Index) && FPayloadEqualById()(Attachments[Index], Id))
	{
		TArray<FSharedBuffer>& AttachmentsCache = Data->AttachmentsCache;
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

const FPayload& FCacheRecord::GetAttachmentPayload(const FPayloadId& Id) const
{
	TArray<FPayload>& Attachments = Data->Attachments;
	const int32 Index = Algo::LowerBound(Attachments, Id, FPayloadLessById());
	if (Attachments.IsValidIndex(Index) && FPayloadEqualById()(Attachments[Index], Id))
	{
		return Attachments[Index];
	}
	return GetEmptyCachePayload();
}

TConstArrayView<FPayload> FCacheRecord::GetAttachmentPayloads() const
{
	return (Data ? *Data : GetEmptyCacheRecord()).Attachments;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FCacheRecordBuilder::FCacheRecordBuilder()
	: Data(MakePimpl<FCacheRecordBuilderData>())
{
}

void FCacheRecordBuilder::SetKey(const FCacheKey& Key)
{
	Data->Key = Key;
}

void FCacheRecordBuilder::SetMeta(FCbObject Meta)
{
	Meta.MakeOwned();
	Data->Meta = MoveTemp(Meta);
}

FPayloadId FCacheRecordBuilder::SetValue(const FSharedBuffer& Buffer, FPayloadId Id)
{
	Id = GetOrCreatePayloadId(Id, Buffer);
	return SetValue(FPayload(Id, FCompressedBuffer::Compress(Buffer)));
}

FPayloadId FCacheRecordBuilder::SetValue(FPayload&& Payload)
{
	FPayload& Value = Data->Value;
	checkf(Value.IsNull(), TEXT("Failed to set value with ID %s because an there is an existing value with ID %s"),
		*Payload.GetId().ToString(), *Value.GetId().ToString());
	Value = MoveTemp(Payload);
	return Value.GetId();
}

FPayloadId FCacheRecordBuilder::AddAttachment(const FSharedBuffer& Buffer, FPayloadId Id)
{
	Id = GetOrCreatePayloadId(Id, Buffer);
	return AddAttachment(FPayload(Id, FCompressedBuffer::Compress(Buffer)));
}

FPayloadId FCacheRecordBuilder::AddAttachment(FPayload&& Payload)
{
	TArray<FPayload>& Attachments = Data->Attachments;
	const int32 Index = Algo::LowerBound(Attachments, Payload, FPayloadLessById());
	checkf(!Attachments.IsValidIndex(Index) || !FPayloadEqualById()(Attachments[Index], Payload),
		TEXT("Failed to add attachment with ID %s because an existing attachment is using that ID"),
		*Payload.GetId().ToString());
	Attachments.Insert(MoveTemp(Payload), Index);
	return Attachments[Index].GetId();
}

FCacheRecord FCacheRecordBuilder::Build()
{
	if (TPimplPtr<FCacheRecordBuilderData> LocalData = MoveTemp(Data))
	{
		return FCacheRecord(MoveTemp(*LocalData));
	}
	return FCacheRecord();
}

TFuture<FCacheRecord> FCacheRecordBuilder::BuildAsync()
{
	TPromise<FCacheRecord> Promise;
	Promise.SetValue(Build());
	return Promise.GetFuture();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

} // DerivedData
} // UE

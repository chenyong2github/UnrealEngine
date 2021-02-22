// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataCacheRecord.h"

#include "Async/Future.h"
#include "Misc/StringBuilder.h"

namespace UE
{
namespace DerivedData
{

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FCachePayload::FCachePayload(const FCachePayloadId& InId, const FIoHash& InCompressedDataHash)
	: Id(InId)
	, CompressedDataHash(InCompressedDataHash)
{
	checkf(Id.IsValid(), TEXT("A valid ID is required to construct a cache payload."));
}

FCachePayload::FCachePayload(const FCachePayloadId& InId, FSharedBuffer InCompressedData)
	: Id(InId)
	, CompressedData(MoveTemp(InCompressedData))
{
	checkf(Id.IsValid(), TEXT("A valid ID is required to construct a cache payload."));
	if (CompressedData.GetSize())
	{
		CompressedDataHash = FIoHash::HashBuffer(CompressedData);
	}
}

FCachePayload::FCachePayload(const FCachePayloadId& InId, FSharedBuffer InCompressedData, const FIoHash& InCompressedDataHash)
	: Id(InId)
	, CompressedData(MoveTemp(InCompressedData))
	, CompressedDataHash(InCompressedDataHash)
{
	checkf(Id.IsValid(), TEXT("A valid ID is required to construct a cache payload."));
	if (CompressedData.GetSize())
	{
		checkfSlow(CompressedDataHash == FIoHash::HashBuffer(CompressedData),
			TEXT("Provided compressed data hash %s does not match calculated hash %s"),
			*CompressedDataHash.ToString(), *FIoHash::HashBuffer(CompressedData).ToString());
		checkf(!CompressedDataHash.IsZero(), TEXT("A non-empty compressed data buffer must have a non-zero hash."));
	}
	else
	{
		checkf(CompressedDataHash.IsZero(), TEXT("A null or empty compressed data buffer must use a hash of zero."));
	}
}

FSharedBuffer FCachePayload::Compress(FSharedBuffer Buffer)
{
	// DDC-TODO: Switch to the new compression interface when it is ready. For now, match the "no compression" method.
	FUniqueBuffer CompressedBuffer = FUniqueBuffer::Alloc(Buffer.GetSize() + 1);
	FMemory::Memset(CompressedBuffer.GetData(), 0, 1);
	FMemory::Memcpy(CompressedBuffer.GetView().RightChop(1).GetData(), Buffer.GetData(), Buffer.GetSize());
	return FSharedBuffer(MoveTemp(CompressedBuffer));
}

FSharedBuffer FCachePayload::Decompress(FSharedBuffer Buffer)
{
	// DDC-TODO: Switch to the new compression interface when it is ready. For now, match the "no compression" method.
	if (Buffer)
	{
		const FMemoryView View = Buffer.GetView() + 1;
		return FSharedBuffer::MakeView(View, MoveTemp(Buffer));
	}
	return FSharedBuffer();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct FCacheRecordBuilderData
{
	FCacheKey Key;
	FCbObject Meta;
	FCachePayload Value;
	TArray<FCachePayload> Attachments;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct FCacheRecordData
{
	FCacheRecordData() = default;
	explicit FCacheRecordData(FCacheRecordBuilderData&& Builder);

	FCacheKey Key;
	FCbObject Meta;
	FCachePayload Value;
	FSharedBuffer ValueCache;
	TArray<FCachePayload> Attachments;
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

static const FCachePayload& GetEmptyCachePayload()
{
	static const FCachePayload Empty;
	return Empty;
}

static const FCacheRecordData& GetEmptyCacheRecord()
{
	static const FCacheRecordData Empty;
	return Empty;
}

static FCachePayloadId GetOrCreatePayloadId(const FCachePayloadId& Id, const FSharedBuffer& Buffer)
{
	return Id.IsValid() ? Id : FCachePayloadId::FromHash(FIoHash::HashBuffer(Buffer));
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
			Data->ValueCache = Data->Value.Decompress();
		}
		return Data->ValueCache;
	}
	return FSharedBuffer();
}

const FCachePayload& FCacheRecord::GetValuePayload() const
{
	return (Data ? *Data : GetEmptyCacheRecord()).Value;
}

FSharedBuffer FCacheRecord::GetAttachment(const FCachePayloadId& Id) const
{
	TArray<FCachePayload>& Attachments = Data->Attachments;
	const int32 Index = Algo::LowerBound(Attachments, Id, FCachePayloadLessById());
	if (Attachments.IsValidIndex(Index) && FCachePayloadEqualById()(Attachments[Index], Id))
	{
		TArray<FSharedBuffer>& AttachmentsCache = Data->AttachmentsCache;
		if (AttachmentsCache.IsEmpty())
		{
			AttachmentsCache.SetNum(Attachments.Num());
		}
		FSharedBuffer& DataCache = AttachmentsCache[Index];
		if (!DataCache)
		{
			DataCache = Attachments[Index].Decompress();
		}
		return DataCache;
	}
	return FSharedBuffer();
}

const FCachePayload& FCacheRecord::GetAttachmentPayload(const FCachePayloadId& Id) const
{
	TArray<FCachePayload>& Attachments = Data->Attachments;
	const int32 Index = Algo::LowerBound(Attachments, Id, FCachePayloadLessById());
	if (Attachments.IsValidIndex(Index) && FCachePayloadEqualById()(Attachments[Index], Id))
	{
		return Attachments[Index];
	}
	return GetEmptyCachePayload();
}

TConstArrayView<FCachePayload> FCacheRecord::GetAttachmentPayloads() const
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

FCachePayloadId FCacheRecordBuilder::SetValue(FSharedBuffer Buffer, FCachePayloadId Id)
{
	Id = GetOrCreatePayloadId(Id, Buffer);
	return SetValue(FCachePayload(Id, FCachePayload::Compress(MoveTemp(Buffer))));
}

FCachePayloadId FCacheRecordBuilder::SetValue(FCachePayload&& Payload)
{
	FCachePayload& Value = Data->Value;
	checkf(Value.IsNull(), TEXT("Failed to set value with ID %s because an there is an existing value with ID %s"),
		*Payload.GetId().ToString(), *Value.GetId().ToString());
	Value = MoveTemp(Payload);
	return Value.GetId();
}

FCachePayloadId FCacheRecordBuilder::AddAttachment(FSharedBuffer Buffer, FCachePayloadId Id)
{
	Id = GetOrCreatePayloadId(Id, Buffer);
	return AddAttachment(FCachePayload(Id, MoveTemp(Buffer)));
}

FCachePayloadId FCacheRecordBuilder::AddAttachment(FCachePayload&& Payload)
{
	TArray<FCachePayload>& Attachments = Data->Attachments;
	const int32 Index = Algo::LowerBound(Attachments, Payload, FCachePayloadLessById());
	checkf(!Attachments.IsValidIndex(Index) || !FCachePayloadEqualById()(Attachments[Index], Payload),
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

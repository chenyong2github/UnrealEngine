// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataCacheRecord.h"

#include "Algo/Accumulate.h"
#include "DerivedDataCacheKey.h"
#include "DerivedDataCachePrivate.h"
#include "DerivedDataValue.h"
#include "Misc/ScopeExit.h"
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

	void SetValue(const FCompositeBuffer& Buffer, const FValueId& Id, uint64 BlockSize) final;
	void SetValue(const FSharedBuffer& Buffer, const FValueId& Id, uint64 BlockSize) final;
	void SetValue(const FValue& Value, const FValueId& Id) final;
	void SetValue(const FValueWithId& Value) final;

	void AddAttachment(const FCompositeBuffer& Buffer, const FValueId& Id, uint64 BlockSize) final;
	void AddAttachment(const FSharedBuffer& Buffer, const FValueId& Id, uint64 BlockSize) final;
	void AddAttachment(const FValue& Value, const FValueId& Id) final;
	void AddAttachment(const FValueWithId& Value) final;

	FCacheRecord Build() final;
	void BuildAsync(IRequestOwner& Owner, FOnCacheRecordComplete&& OnComplete) final;

	FCacheKey Key;
	FCbObject Meta;
	FValueWithId Value;
	TArray<FValueWithId> Attachments;
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
	const FValueWithId& GetValue() const final;
	const FValueWithId& GetAttachment(const FValueId& Id) const final;
	TConstArrayView<FValueWithId> GetAttachments() const final;
	const FValueWithId& GetValue(const FValueId& Id) const final;

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
	FValueWithId Value;
	TArray<FValueWithId> Attachments;
	mutable std::atomic<uint32> ReferenceCount{0};
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FValueId GetOrCreateValueId(const FValueId& Id, const FValue& Value)
{
	return Id.IsValid() ? Id : FValueId::FromHash(Value.GetRawHash());
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

const FValueWithId& FCacheRecordInternal::GetValue() const
{
	return Value;
}

const FValueWithId& FCacheRecordInternal::GetAttachment(const FValueId& Id) const
{
	const int32 Index = Algo::BinarySearchBy(Attachments, Id, &FValueWithId::GetId);
	return Attachments.IsValidIndex(Index) ? Attachments[Index] : FValueWithId::Null;
}

TConstArrayView<FValueWithId> FCacheRecordInternal::GetAttachments() const
{
	return Attachments;
}

const FValueWithId& FCacheRecordInternal::GetValue(const FValueId& Id) const
{
	return Value.GetId() == Id ? Value : GetAttachment(Id);
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

void FCacheRecordBuilderInternal::SetValue(const FCompositeBuffer& Buffer, const FValueId& Id, const uint64 BlockSize)
{
	return SetValue(FValue::Compress(Buffer, BlockSize), Id);
}

void FCacheRecordBuilderInternal::SetValue(const FSharedBuffer& Buffer, const FValueId& Id, const uint64 BlockSize)
{
	return SetValue(FValue::Compress(Buffer, BlockSize), Id);
}

void FCacheRecordBuilderInternal::SetValue(const FValue& InValue, const FValueId& Id)
{
	const FValueId ValueId = GetOrCreateValueId(Id, InValue);
	checkf(Value.IsNull(),
		TEXT("Failed to set value on %s with ID %s because it has an existing value with ID %s."),
		*WriteToString<96>(Key), *WriteToString<32>(ValueId), *WriteToString<32>(Value.GetId()));
	checkf(Algo::BinarySearchBy(Attachments, ValueId, &FValueWithId::GetId) == INDEX_NONE,
		TEXT("Failed to set value on %s with ID %s because it has an existing attachment with that ID."),
		*WriteToString<96>(Key), *WriteToString<32>(ValueId));
	Value = FValueWithId(ValueId, InValue);
}

void FCacheRecordBuilderInternal::SetValue(const FValueWithId& InValue)
{
	checkf(InValue, TEXT("Failed to set value on %s because the value is null."), *WriteToString<96>(Key));
	SetValue(InValue, InValue.GetId());
}

void FCacheRecordBuilderInternal::AddAttachment(const FCompositeBuffer& Buffer, const FValueId& Id, const uint64 BlockSize)
{
	return AddAttachment(FValue::Compress(Buffer, BlockSize), Id);
}

void FCacheRecordBuilderInternal::AddAttachment(const FSharedBuffer& Buffer, const FValueId& Id, const uint64 BlockSize)
{
	return AddAttachment(FValue::Compress(Buffer, BlockSize), Id);
}

void FCacheRecordBuilderInternal::AddAttachment(const FValue& InValue, const FValueId& Id)
{
	const FValueId ValueId = GetOrCreateValueId(Id, InValue);
	checkf(Value.IsNull(),
		TEXT("Failed to set value on %s with ID %s because it has an existing value with ID %s."),
		*WriteToString<96>(Key), *WriteToString<32>(ValueId), *WriteToString<32>(Value.GetId()));
	const int32 Index = Algo::LowerBoundBy(Attachments, Id, &FValueWithId::GetId);
	checkf(!(Attachments.IsValidIndex(Index) && Attachments[Index].GetId() == Id) && Value.GetId() != Id,
		TEXT("Failed to add attachment on %s with ID %s because it has an existing attachment or value with that ID."),
		*WriteToString<96>(Key), *WriteToString<32>(Id));
	Attachments.Insert(FValueWithId(ValueId, InValue), Index);
}

void FCacheRecordBuilderInternal::AddAttachment(const FValueWithId& InValue)
{
	checkf(InValue, TEXT("Failed to add attachment on %s because the value is null."), *WriteToString<96>(Key));
	AddAttachment(InValue, InValue.GetId());
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
	auto SaveValue = [&Package, &Writer](const FValueWithId& ValueWithId)
	{
		if (ValueWithId.HasData())
		{
			Package.AddAttachment(FCbAttachment(ValueWithId.GetData()));
		}
		Writer.BeginObject();
		Writer.AddObjectId("Id"_ASV, ValueWithId.GetId());
		Writer.AddBinaryAttachment("RawHash"_ASV, ValueWithId.GetRawHash());
		Writer.AddInteger("RawSize"_ASV, ValueWithId.GetRawSize());
		Writer.EndObject();
	};
	if (const FValueWithId& Value = GetValue())
	{
		Writer.SetName("Value"_ASV);
		SaveValue(Value);
	}
	TConstArrayView<FValueWithId> Attachments = GetAttachments();
	if (!Attachments.IsEmpty())
	{
		Writer.BeginArray("Attachments"_ASV);
		for (const FValueWithId& Attachment : Attachments)
		{
			SaveValue(Attachment);
		}
		Writer.EndArray();
	}
	Writer.EndObject();

	Package.SetObject(Writer.Save().AsObject());
	return Package;
}

FOptionalCacheRecord FCacheRecord::Load(const FCbPackage& Attachments, const FCbObject& Object)
{
	const FCbObjectView ObjectView = Object;

	FCacheKey Key;
	FCbObjectView KeyObject = ObjectView["Key"_ASV].AsObjectView();
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

	Builder.SetMeta(Object["Meta"_ASV].AsObject());

	auto LoadValue = [&Attachments](const FCbObjectView& ValueObject)
	{
		const FValueId Id = ValueObject["Id"_ASV].AsObjectId();
		if (Id.IsNull())
		{
			return FValueWithId();
		}
		const FIoHash RawHash = ValueObject["RawHash"_ASV].AsHash();
		if (const FCbAttachment* Attachment = Attachments.FindAttachment(RawHash))
		{
			if (const FCompressedBuffer& Compressed = Attachment->AsCompressedBinary())
			{
				return FValueWithId(Id, Compressed);
			}
		}
		const uint64 RawSize = ValueObject["RawSize"_ASV].AsUInt64(MAX_uint64);
		if (!RawHash.IsZero() && RawSize != MAX_uint64)
		{
			return FValueWithId(Id, RawHash, RawSize);
		}
		else
		{
			return FValueWithId();
		}
	};

	if (FCbObjectView ValueObject = ObjectView["Value"_ASV].AsObjectView())
	{
		FValueWithId Value = LoadValue(ValueObject);
		if (!Value)
		{
			return FOptionalCacheRecord();
		}
		Builder.SetValue(Value);
	}

	for (FCbFieldView AttachmentField : ObjectView["Attachments"_ASV])
	{
		FValueWithId Attachment = LoadValue(AttachmentField.AsObjectView());
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
	const uint64 ValueSize = Record.GetValue().GetData().GetCompressedSize();
	return int64(Algo::TransformAccumulate(Record.GetAttachments(),
		[](const FValueWithId& Value) { return Value.GetData().GetCompressedSize(); }, ValueSize));
}

uint64 GetCacheRecordTotalRawSize(const FCacheRecord& Record)
{
	const uint64 ValueSize = Record.GetValue().GetRawSize();
	return int64(Algo::TransformAccumulate(Record.GetAttachments(), &FValueWithId::GetRawSize, ValueSize));
}

uint64 GetCacheRecordRawSize(const FCacheRecord& Record)
{
	const uint64 ValueSize = Record.GetValue().GetData().GetRawSize();
	return int64(Algo::TransformAccumulate(Record.GetAttachments(),
		[](const FValueWithId& Value) { return Value.GetData().GetRawSize(); }, ValueSize));
}

} // UE::DerivedData::Private

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DerivedDataCacheKey.h"
#include "DerivedDataPayload.h"
#include "DerivedDataRequest.h"
#include "Memory/SharedBuffer.h"
#include "Templates/RefCounting.h"
#include "Templates/UniquePtr.h"

class FCbObject;
template <typename FuncType> class TUniqueFunction;

namespace UE::DerivedData { class FCacheRecord; }
namespace UE::DerivedData { class FCacheRecordBuilder; }
namespace UE::DerivedData { using FOnCacheRecordComplete = TUniqueFunction<void (FCacheRecord&& Record)>; }

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace UE::DerivedData::Private
{

class ICacheRecordInternal
{
public:
	virtual ~ICacheRecordInternal() = default;
	virtual const FCacheKey& GetKey() const = 0;
	virtual const FCbObject& GetMeta() const = 0;
	virtual FSharedBuffer GetValue() const = 0;
	virtual const FPayload& GetValuePayload() const = 0;
	virtual FSharedBuffer GetAttachment(const FPayloadId& Id) const = 0;
	virtual const FPayload& GetAttachmentPayload(const FPayloadId& Id) const = 0;
	virtual TConstArrayView<FPayload> GetAttachmentPayloads() const = 0;
	virtual const FPayload& GetPayload(const FPayloadId& Id) const = 0;
	virtual void AddRef() const = 0;
	virtual void Release() const = 0;
};

FCacheRecord CreateCacheRecord(ICacheRecordInternal* Record);

class ICacheRecordBuilderInternal
{
public:
	virtual ~ICacheRecordBuilderInternal() = default;
	virtual void SetMeta(FCbObject&& Meta) = 0;
	virtual FPayloadId SetValue(const FSharedBuffer& Buffer, const FPayloadId& Id) = 0;
	virtual FPayloadId SetValue(FPayload&& Payload) = 0;
	virtual FPayloadId AddAttachment(const FSharedBuffer& Buffer, const FPayloadId& Id) = 0;
	virtual FPayloadId AddAttachment(FPayload&& Payload) = 0;
	virtual FCacheRecord Build() = 0;
	virtual FRequest BuildAsync(FOnCacheRecordComplete&& OnComplete, EPriority Priority) = 0;
};

FCacheRecordBuilder CreateCacheRecordBuilder(ICacheRecordBuilderInternal* RecordBuilder);

} // UE::DerivedData::Private

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace UE::DerivedData
{

/**
 * A cache record is a key, a value, attachments, and metadata.
 *
 * The key is expected to correspond uniquely to its value and attachments. The key should not be
 * used with any other value and attachments. The metadata does not have the same requirement and
 * may be used to persist details such as the machine that created the value or how much time was
 * spent creating the value.
 *
 * The value and its attachments are compressed, and both the compressed and uncompressed formats
 * of the payloads are exposed by the cache record.
 *
 * The value, attachments, and metadata are optional and can be skipped when requesting a record.
 * When the value or attachments have been skipped, the record will contain a payload with a null
 * buffer but will otherwise be populated.
 */
class FCacheRecord
{
public:
	/** Construct a null cache record. */
	explicit FCacheRecord() = default;

	/** Returns the key that identifies this record in the cache. Always available in a non-null cache record. */
	inline const FCacheKey& GetKey() const { return Record->GetKey(); }

	/** Returns the metadata. Null when requested with ECachePolicy::SkipMeta. */
	inline const FCbObject& GetMeta() const { return Record->GetMeta(); }

	/** Returns the value. Null if no value or requested with ECachePolicy::SkipValue. */
	inline FSharedBuffer GetValue() const { return Record->GetValue(); }

	/** Returns the value payload. Null if no value. Buffer is null if value was skipped. */
	inline const FPayload& GetValuePayload() const { return Record->GetValuePayload(); }

	/** Returns the attachment matching the ID. Null if no match or requested with ECachePolicy::SkipAttachments. */
	inline FSharedBuffer GetAttachment(const FPayloadId& Id) const { return Record->GetAttachment(Id); }

	/** Returns the attachment payload matching the ID. Null if no match. Buffer is null if attachments were skipped. */
	inline const FPayload& GetAttachmentPayload(const FPayloadId& Id) const { return Record->GetAttachmentPayload(Id); }

	/** Returns a view of the attachments. Always available in a non-null cache record, but buffer may be skipped. */
	inline TConstArrayView<FPayload> GetAttachmentPayloads() const { return Record->GetAttachmentPayloads(); }

	/** Returns the payload matching the ID, whether value or attachment. Null if no match. Buffer is null if skipped. */
	inline const FPayload& GetPayload(const FPayloadId& Id) const { return Record->GetPayload(Id); }

	/** Whether this is null. */
	inline bool IsNull() const { return !Record; }
	/** Whether this is not null. */
	inline bool IsValid() const { return !IsNull(); }
	/** Whether this is not null. */
	inline explicit operator bool() const { return IsValid(); }

	/** Reset this to null. */
	inline void Reset() { *this = FCacheRecord(); }

private:
	friend FCacheRecord Private::CreateCacheRecord(Private::ICacheRecordInternal* Record);

	/** Construct a cache record. Use Build() or BuildAsync() on a builder from ICache::CreateRecord(). */
	inline explicit FCacheRecord(Private::ICacheRecordInternal* InRecord)
		: Record(InRecord)
	{
	}

	TRefCountPtr<Private::ICacheRecordInternal> Record;
};

/**
 * A cache record builder is used to construct a cache record.
 *
 * Create using ICache::CreateRecord() which must be given a key that uniquely corresponds to the
 * value and attachments for the cache record. Metadata may vary between records of the same key.
 *
 * The value and attachments can be provided as buffers, which will be compressed, or as payloads
 * which were previously compressed and have an identifier assigned.
 *
 * @see FCacheRecord
 */
class FCacheRecordBuilder
{
public:
	/**
	 * Set the metadata for the cache record.
	 *
	 * @param Meta   The metadata, which is cloned if not owned.
	 */
	inline void SetMeta(FCbObject&& Meta)
	{
		return RecordBuilder->SetMeta(MoveTemp(Meta));
	}

	/**
	 * Set the value for the cache record.
	 *
	 * @param Buffer   The value, which is compressed by the builder, and cloned if not owned.
	 * @param Id       An ID for the value that is unique within this cache record. When omitted,
	 *                 the buffer will be hashed to create an ID.
	 * @return The ID that was provided or created.
	 */
	inline FPayloadId SetValue(const FSharedBuffer& Buffer, const FPayloadId& Id = FPayloadId())
	{
		return RecordBuilder->SetValue(Buffer, Id);
	}

	/**
	 * Set the value for the cache record.
	 *
	 * @param Payload   The value payload, which must have a buffer.
	 * @return The ID that was provided. Unique within the scope of the cache record.
	 */
	inline FPayloadId SetValue(FPayload&& Payload)
	{
		return RecordBuilder->SetValue(MoveTemp(Payload));
	}

	/**
	 * Add an attachment to the cache record.
	 *
	 * @param Buffer   The attachment, which is compressed by the builder, and cloned if not owned.
	 * @param Id       An ID for the attachment that is unique within this cache record. When omitted,
	 *                 the buffer will be hashed to create an ID.
	 * @return The ID that was provided or created.
	 */
	inline FPayloadId AddAttachment(const FSharedBuffer& Buffer, const FPayloadId& Id = FPayloadId())
	{
		return RecordBuilder->AddAttachment(Buffer, Id);
	}

	/**
	 * Add an attachment to the cache record.
	 *
	 * @param Payload   The attachment payload, which must have a buffer.
	 * @return The ID that was provided. Unique within the scope of the cache record.
	 */
	inline FPayloadId AddAttachment(FPayload&& Payload)
	{
		return RecordBuilder->AddAttachment(MoveTemp(Payload));
	}

	/**
	 * Build a cache record, which makes this builder subsequently unusable.
	 *
	 * Prefer BuildAsync() when the value or attachments are added from a buffer, as this must block
	 * on compression of those buffers before it can construct a cache record.
	 */
	inline FCacheRecord Build()
	{
		return RecordBuilder->Build();
	}

	/**
	 * Build a cache record asynchronously, which makes this builder subsequently unusable.
	 *
	 * Prefer Build() when the value and attachments are added by payload, as compression is already
	 * complete and BuildAsync() will complete immediately in that case.
	 */
	inline FRequest BuildAsync(FOnCacheRecordComplete&& OnComplete, EPriority Priority)
	{
		return RecordBuilder->BuildAsync(MoveTemp(OnComplete), Priority);
	}

private:
	friend FCacheRecordBuilder Private::CreateCacheRecordBuilder(Private::ICacheRecordBuilderInternal* RecordBuilder);

	/** Construct a cache record builder. Use ICache::CreateRecord(). */
	inline explicit FCacheRecordBuilder(Private::ICacheRecordBuilderInternal* InRecordBuilder)
		: RecordBuilder(InRecordBuilder)
	{
	}

	TUniquePtr<Private::ICacheRecordBuilderInternal> RecordBuilder;
};

} // UE::DerivedData

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace UE::DerivedData
{

/** An implementation of KeyFuncs to compare FCacheRecord by its FCacheKey. */
struct FCacheRecordKeyFuncs
{
	using KeyType = FCacheKey;
	using KeyInitType = const FCacheKey&;
	using ElementInitType = const FCacheRecord&;

	static constexpr bool bAllowDuplicateKeys = false;

	static inline KeyInitType GetSetKey(ElementInitType Record) { return Record.GetKey(); }
	static inline uint32 GetKeyHash(KeyInitType Key) { return GetTypeHash(Key); }
	static inline bool Matches(KeyInitType A, KeyInitType B) { return A == B; }
};

} // UE::DerivedData

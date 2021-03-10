// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "DerivedDataPayload.h"
#include "DerivedDataRequest.h"
#include "Memory/MemoryFwd.h"
#include "Templates/UniquePtr.h"

class FCbObject;
template <typename FuncType> class TUniqueFunction;

namespace UE
{
namespace DerivedData
{

class FCacheRecord;
struct FCacheKey;

using FOnCacheRecordComplete = TUniqueFunction<void (FCacheRecord&& Record)>;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace Private
{

class ICacheRecordInternal
{
public:
	virtual ~ICacheRecordInternal() = default;
	virtual FCacheRecord Clone() const = 0;
	virtual const FCacheKey& GetKey() const = 0;
	virtual const FCbObject& GetMeta() const = 0;
	virtual FSharedBuffer GetValue() const = 0;
	virtual const FPayload& GetValuePayload() const = 0;
	virtual FSharedBuffer GetAttachment(const FPayloadId& Id) const = 0;
	virtual const FPayload& GetAttachmentPayload(const FPayloadId& Id) const = 0;
	virtual TConstArrayView<FPayload> GetAttachmentPayloads() const = 0;
};

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
	virtual FRequest BuildAsync(FOnCacheRecordComplete&& Callback, EPriority Priority) = 0;
};

} // Private

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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
 * When the value or attachments have been skipped, the record will contain a payload with the ID
 * and hash, but no buffer containing the associated data.
 */
class FCacheRecord
{
public:
	/** Construct a null cache record. */
	explicit FCacheRecord() = default;

	/** Clone this cache record. Prefer to move records, and clone only when a copy is necessary. */
	inline FCacheRecord Clone() const { return Record->Clone(); }

	/** Returns the key that identifies this record in the cache. Always available in a non-null cache record. */
	inline const FCacheKey& GetKey() const { return Record->GetKey(); }

	/** Returns the metadata for the cache record unless it was skipped in the request. */
	inline const FCbObject& GetMeta() const { return Record->GetMeta(); }

	/** Returns the value. Null if the value was skipped in the request. */
	inline FSharedBuffer GetValue() const { return Record->GetValue(); }

	/** Returns the value payload. Always available in a non-null cache record. */
	inline const FPayload& GetValuePayload() const { return Record->GetValuePayload(); }

	/** Returns the attachment that matches the ID. Null if no match or attachments were skipped in the request. */
	inline FSharedBuffer GetAttachment(const FPayloadId& Id) const { return Record->GetAttachment(Id); }

	/** Returns the attachment payload that matches the ID. Null if no match or the cache record is null. */
	inline const FPayload& GetAttachmentPayload(const FPayloadId& Id) const { return Record->GetAttachmentPayload(Id); }

	/** Returns a view of the attachments. Always available in a non-null cache record, but buffer may be skipped. */
	inline TConstArrayView<FPayload> GetAttachmentPayloads() const { return Record->GetAttachmentPayloads(); }

	/** Whether this is null. */
	inline bool IsNull() const { return !Record; }
	/** Whether this is not null. */
	inline bool IsValid() const { return !IsNull(); }
	/** Whether this is not null. */
	inline explicit operator bool() const { return IsValid(); }

	/** Reset this to null. */
	inline void Reset() { *this = FCacheRecord(); }

public:
	// Internal API

	/** Construct a cache record. Use Record.Clone() or Builder.Build[Async](). */
	inline explicit FCacheRecord(Private::ICacheRecordInternal* InRecord)
		: Record(InRecord)
	{
	}

private:
	TUniquePtr<Private::ICacheRecordInternal> Record;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * A cache record builder is used to construct a cache record.
 *
 * Create using ICache::CreateRecord, which must be given a key uniquely corresponds to the value
 * and attachments for the cache record. Optional metadata may vary and may be non-deterministic.
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
	 * The metadata is cloned if not owned.
	 */
	inline void SetMeta(FCbObject&& Meta)
	{
		return Builder->SetMeta(MoveTemp(Meta));
	}

	/**
	 * Set the value for the cache record.
	 *
	 * @param Buffer The value, which is cloned if not owned, and is compressed by the builder.
	 * @param Id An ID for the value that is unique within the scope of the cache record. If omitted
	 *           the builder will create an ID by hashing the buffer.
	 * @return The ID that was provided or created.
	 */
	inline FPayloadId SetValue(const FSharedBuffer& Buffer, const FPayloadId& Id = FPayloadId())
	{
		return Builder->SetValue(Buffer, Id);
	}

	/**
	 * Set the value for the cache record.
	 *
	 * @param Payload The value payload, which is reset to null.
	 * @return The ID that was provided. Unique within the scope of the cache record.
	 */
	inline FPayloadId SetValue(FPayload&& Payload) { return Builder->SetValue(MoveTemp(Payload)); }

	/**
	 * Add an attachment to the cache record.
	 *
	 * @param Buffer The attachment, which is cloned if not owned, and is compressed by the builder.
	 * @param Id An ID for the attachment that is unique within the scope of the cache record. If it
	 *           is omitted the builder will create an ID by hashing the buffer.
	 * @return The ID that was provided or created.
	 */
	inline FPayloadId AddAttachment(const FSharedBuffer& Buffer, const FPayloadId& Id = FPayloadId())
	{
		return Builder->AddAttachment(Buffer, Id);
	}

	/**
	 * Add an attachment to the cache record.
	 *
	 * @param Payload The attachment payload, which is reset to null.
	 * @return The ID that was provided. Unique within the scope of the cache record.
	 */
	inline FPayloadId AddAttachment(FPayload&& Payload)
	{
		return Builder->AddAttachment(MoveTemp(Payload));
	}

	/**
	 * Build a cache record, which leaves this builder null.
	 *
	 * Prefer BuildAsync() when the value or attachments are added from a buffer, as this must block
	 * on compression of those buffers before it can construct a cache record.
	 */
	inline FCacheRecord Build()
	{
		return Builder->Build();
	}

	/**
	 * Build a cache record asynchronously, which leaves this builder null.
	 *
	 * Prefer Build() when the value and attachments are added by payload, as compression is already
	 * complete and BuildAsync() will complete immediately in that case.
	 */
	inline FRequest BuildAsync(FOnCacheRecordComplete&& Callback, EPriority Priority)
	{
		return Builder->BuildAsync(MoveTemp(Callback), Priority);
	}

	/** Whether this is null. */
	inline bool IsNull() const { return !Builder; }
	/** Whether this is not null. */
	inline bool IsValid() const { return !IsNull(); }
	/** Whether this is not null. */
	inline explicit operator bool() const { return IsValid(); }

public:
	// Internal API

	inline explicit FCacheRecordBuilder(Private::ICacheRecordBuilderInternal* InBuilder)
		: Builder(InBuilder)
	{
	}

private:
	TUniquePtr<Private::ICacheRecordBuilderInternal> Builder;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

} // DerivedData
} // UE

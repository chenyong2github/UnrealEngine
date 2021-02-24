// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "DerivedDataCacheKey.h"
#include "DerivedDataPayload.h"
#include "Memory/MemoryFwd.h"
#include "Templates/PimplPtr.h"

#define UE_API DERIVEDDATACACHE_API

class FCbObject;
template <typename ResultType> class TFuture;

namespace UE
{
namespace DerivedData
{

class FCacheRecordBuilder;
struct FCacheRecordData;
struct FCacheRecordBuilderData;

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
	FCacheRecord() = default;

	/** Clone this cache record. Prefer to move records, and clone only when a copy is necessary. */
	UE_API FCacheRecord Clone() const;

	/** Returns the key that identifies this record in the cache. Always available in a non-null cache record. */
	UE_API const FCacheKey& GetKey() const;

	/** Returns the metadata for the cache record unless it was skipped in the request. */
	UE_API const FCbObject& GetMeta() const;

	/** Returns the value. Null if the value was skipped in the request. */
	UE_API FSharedBuffer GetValue() const;

	/** Returns the value payload. Always available in a non-null cache record. */
	UE_API const FPayload& GetValuePayload() const;

	/** Returns the attachment that matches the ID. Null if no match or attachments were skipped in the request. */
	UE_API FSharedBuffer GetAttachment(const FPayloadId& Id) const;

	/** Returns the attachment payload that matches the ID. Null if no match or the cache record is null. */
	UE_API const FPayload& GetAttachmentPayload(const FPayloadId& Id) const;

	/** Returns a view of the attachments. Always available in a non-null cache record, but buffer may be skipped. */
	UE_API TConstArrayView<FPayload> GetAttachmentPayloads() const;

	/** Whether this is null. */
	inline bool IsNull() const { return !Data; }
	/** Whether this is not null. */
	inline bool IsValid() const { return !IsNull(); }
	/** Whether this is not null. */
	inline explicit operator bool() const { return IsValid(); }

	/** Reset this to null. */
	inline void Reset() { *this = FCacheRecord(); }

public:
	// Internal API

	/** Construct a cache record by cloning a cache record. Use Clone(). */
	explicit FCacheRecord(const FCacheRecordData& Data);

	/** Construct a cache record from a builder. Use Builder.Build() or Builder.BuildAsync(). */
	explicit FCacheRecord(FCacheRecordBuilderData&& Builder);

private:
	TPimplPtr<FCacheRecordData> Data;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * A cache record builder is used to construct a cache record.
 *
 * A key must be set before using the this to construct a cache record. The value and attachments
 * can be provided as buffers or as payloads which are already compressed and have an identifier.
 *
 * @see FCacheRecord
 */
class FCacheRecordBuilder
{
public:
	/** Construct an empty cache record builder. */
	UE_API FCacheRecordBuilder();

	/**
	 * Set the key for the cache record.
	 *
	 * The key must uniquely correspond to the value and attachments for the cache record, while the
	 * optional metadata may vary and may be non-deterministic.
	 */
	UE_API void SetKey(const FCacheKey& Key);

	/**
	 * Set the metadata for the cache record.
	 *
	 * The metadata is cloned if not owned.
	 */
	UE_API void SetMeta(FCbObject Meta);

	/**
	 * Set the value for the cache record.
	 *
	 * @param Buffer The value, which is cloned if not owned, and is compressed by the builder.
	 * @param Id An ID for the value that is unique within the scope of the cache record. If omitted
	 *           the builder will create an ID by hashing the buffer.
	 * @return The ID that was provided or created.
	 */
	UE_API FPayloadId SetValue(const FSharedBuffer& Buffer, FPayloadId Id = FPayloadId());

	/**
	 * Set the value for the cache record.
	 *
	 * @param Payload The value payload, which is reset to null.
	 * @return The ID that was provided. Unique within the scope of the cache record.
	 */
	UE_API FPayloadId SetValue(FPayload&& Payload);

	/**
	 * Add an attachment to the cache record.
	 *
	 * @param Buffer The attachment, which is cloned if not owned, and is compressed by the builder.
	 * @param Id An ID for the attachment that is unique within the scope of the cache record. If it
	 *           is omitted the builder will create an ID by hashing the buffer.
	 * @return The ID that was provided or created.
	 */
	UE_API FPayloadId AddAttachment(const FSharedBuffer& Buffer, FPayloadId Id = FPayloadId());

	/**
	 * Add an attachment to the cache record.
	 *
	 * @param Payload The attachment payload, which is reset to null.
	 * @return The ID that was provided. Unique within the scope of the cache record.
	 */
	UE_API FPayloadId AddAttachment(FPayload&& Payload);

	/**
	 * Build a cache record, resetting this builder.
	 *
	 * Prefer BuildAsync() when the value or attachments are added from a buffer, as this must block
	 * on compression of those buffers before it can construct a cache record.
	 */
	UE_API FCacheRecord Build();

	/**
	 * Build a cache record asynchronously, resetting this builder.
	 *
	 * Prefer Build() when the value and attachments are added by payload, as compression is already
	 * complete and BuildAsync() will complete immediately in that case.
	 */
	UE_API TFuture<FCacheRecord> BuildAsync();

	/** Whether this is null. */
	inline bool IsNull() const { return !Data; }
	/** Whether this is not null. */
	inline bool IsValid() const { return !IsNull(); }
	/** Whether this is not null. */
	inline explicit operator bool() const { return IsValid(); }

	/** Reset this to empty. */
	inline void Reset() { *this = FCacheRecordBuilder(); }

private:
	TPimplPtr<FCacheRecordBuilderData> Data;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

} // DerivedData
} // UE

#undef UE_API

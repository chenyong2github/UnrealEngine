// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "DerivedDataCacheKey.h"
#include "Memory/SharedBuffer.h"
#include "Serialization/CompactBinary.h"
#include "Templates/PimplPtr.h"

#define UE_API DERIVEDDATACACHE_API

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
 * A cache payload is a compressed value or attachment for a cache record.
 */
class FCachePayload
{
public:
	/** Construct a null cache payload. */
	FCachePayload() = default;

	/** Construct a cache payload from the compressed data hash. */
	UE_API FCachePayload(const FCachePayloadId& Id, const FIoHash& CompressedDataHash);
	/** Construct a cache payload from the compressed data. See FCachePayload::Compress. */
	UE_API FCachePayload(const FCachePayloadId& Id, FSharedBuffer CompressedData);
	/** Construct a cache payload from the compressed data its hash. See FCachePayload::Compress. */
	UE_API FCachePayload(const FCachePayloadId& Id, FSharedBuffer CompressedData, const FIoHash& CompressedDataHash);

	/** Returns a payload ID that uniquely identifies it within the scope of its cache record. */
	inline const FCachePayloadId& GetId() const { return Id; }

	/**
	 * Returns the compressed data for the payload.
	 *
	 * This will be null when the originating cache request skips the payload.
	 */
	inline const FSharedBuffer& GetCompressedData() const { return CompressedData; }

	/** Returns the hash of the compressed data for the payload. */
	inline const FIoHash& GetCompressedDataHash() const { return CompressedDataHash; }

	inline FSharedBuffer Decompress() const { return Decompress(CompressedData); }

	/** Whether this is null. */
	inline bool IsNull() const { return Id.IsNull(); }
	/** Whether this is not null. */
	inline bool IsValid() const { return !IsNull(); }
	/** Whether this is not null. */
	inline explicit operator bool() const { return IsValid(); }

	/** Reset this to null. */
	inline void Reset() { *this = FCachePayload(); }

	/**
	 * Compress the input buffer with the default settings for a cache payload.
	 *
	 * FIoCompression::Compress and its variants may be used instead to access more options.
	 */
	UE_API static FSharedBuffer Compress(FSharedBuffer Data);

	/**
	 * Decompress the input buffer.
	 *
	 * FIoCompression::Decompress and its variants may be used instead to access more options.
	 */
	UE_API static FSharedBuffer Decompress(FSharedBuffer CompressedData);

private:
	FCachePayloadId Id;
	FSharedBuffer CompressedData;
	FIoHash CompressedDataHash;
};

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
	UE_API const FCbObjectRef& GetMeta() const;

	/** Returns the value. Null if the value was skipped in the request. */
	UE_API FSharedBuffer GetValue() const;

	/** Returns the value payload. Always available in a non-null cache record. */
	UE_API const FCachePayload& GetValuePayload() const;

	/** Returns the attachment that matches the ID. Null if no match or attachments were skipped in the request. */
	UE_API FSharedBuffer GetAttachment(const FCachePayloadId& Id) const;

	/** Returns the attachment payload that matches the ID. Null if no match or the cache record is null. */
	UE_API const FCachePayload& GetAttachmentPayload(const FCachePayloadId& Id) const;

	/** Returns a view of the attachments. Always available in a non-null cache record, but data may be skipped. */
	UE_API TConstArrayView<FCachePayload> GetAttachmentPayloads() const;

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
	UE_API void SetMeta(FCbObjectRef Meta);

	/**
	 * Set the value for the cache record.
	 *
	 * @param Buffer The value, which is cloned if not owned, and is compressed by the builder.
	 * @param Id An ID for the value that is unique within the scope of the cache record. If omitted
	 *           the builder will create an ID by hashing the buffer.
	 * @return The payload ID that was provided or created.
	 */
	UE_API FCachePayloadId SetValue(FSharedBuffer Buffer, FCachePayloadId Id = FCachePayloadId());

	/**
	 * Set the value for the cache record.
	 *
	 * @param Payload The value payload, which is reset to null.
	 * @return The payload ID that was provided. Unique within the scope of the cache record.
	 */
	UE_API FCachePayloadId SetValue(FCachePayload&& Payload);

	/**
	 * Add an attachment to the cache record.
	 *
	 * @param Buffer The attachment, which is cloned if not owned, and is compressed by the builder.
	 * @param Id An ID for the attachment that is unique within the scope of the cache record. If it
	 *           is omitted the builder will create an ID by hashing the buffer.
	 * @return The payload ID that was provided or created.
	 */
	UE_API FCachePayloadId AddAttachment(FSharedBuffer Buffer, FCachePayloadId Id = FCachePayloadId());

	/**
	 * Add an attachment to the cache record.
	 *
	 * @param Payload The attachment payload, which is reset to null.
	 * @return The payload ID that was provided. Unique within the scope of the cache record.
	 */
	UE_API FCachePayloadId AddAttachment(FCachePayload&& Payload);

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

/** Compare cache payloads by their ID. */
struct FCachePayloadEqualById
{
	inline bool operator()(const FCachePayload& Payload1, const FCachePayload& Payload2) const
	{
		return Payload1.GetId() == Payload2.GetId();
	}

	inline bool operator()(const FCachePayload& Payload1, const FCachePayloadId& PayloadId2) const
	{
		return Payload1.GetId() == PayloadId2;
	}

	inline bool operator()(const FCachePayloadId& PayloadId1, const FCachePayload& Payload2) const
	{
		return PayloadId1 == Payload2.GetId();
	}
};

/** Compare cache payloads by their ID. */
struct FCachePayloadLessById
{
	inline bool operator()(const FCachePayload& Payload1, const FCachePayload& Payload2) const
	{
		return Payload1.GetId() < Payload2.GetId();
	}

	inline bool operator()(const FCachePayload& Payload1, const FCachePayloadId& PayloadId2) const
	{
		return Payload1.GetId() < PayloadId2;
	}

	inline bool operator()(const FCachePayloadId& PayloadId1, const FCachePayload& Payload2) const
	{
		return PayloadId1 < Payload2.GetId();
	}
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

} // DerivedData
} // UE

#undef UE_API

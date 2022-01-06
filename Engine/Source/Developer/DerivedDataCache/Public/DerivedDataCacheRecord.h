// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DerivedDataCacheKey.h"
#include "DerivedDataValueId.h"
#include "Memory/MemoryFwd.h"
#include "Misc/ScopeExit.h"
#include "Templates/Function.h"
#include "Templates/RefCounting.h"
#include "Templates/UniquePtr.h"

#define UE_API DERIVEDDATACACHE_API

class FCbObject;
class FCbPackage;

namespace UE::DerivedData { class FCacheRecord; }
namespace UE::DerivedData { class FOptionalCacheRecord; }
namespace UE::DerivedData { class FValue; }
namespace UE::DerivedData { class FValueWithId; }
namespace UE::DerivedData { class IRequestOwner; }
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
	virtual const FValueWithId& GetValue() const = 0;
	virtual const FValueWithId& GetAttachment(const FValueId& Id) const = 0;
	virtual TConstArrayView<FValueWithId> GetAttachments() const = 0;
	virtual const FValueWithId& GetValue(const FValueId& Id) const = 0;
	virtual void AddRef() const = 0;
	virtual void Release() const = 0;
};

FCacheRecord CreateCacheRecord(ICacheRecordInternal* Record);

class ICacheRecordBuilderInternal
{
public:
	virtual ~ICacheRecordBuilderInternal() = default;
	virtual void SetMeta(FCbObject&& Meta) = 0;
	virtual void SetValue(const FCompositeBuffer& Buffer, const FValueId& Id, uint64 BlockSize) = 0;
	virtual void SetValue(const FSharedBuffer& Buffer, const FValueId& Id, uint64 BlockSize) = 0;
	virtual void SetValue(const FValue& Value, const FValueId& Id) = 0;
	virtual void SetValue(const FValueWithId& Value) = 0;
	virtual void AddAttachment(const FCompositeBuffer& Buffer, const FValueId& Id, uint64 BlockSize) = 0;
	virtual void AddAttachment(const FSharedBuffer& Buffer, const FValueId& Id, uint64 BlockSize) = 0;
	virtual void AddAttachment(const FValue& Value, const FValueId& Id) = 0;
	virtual void AddAttachment(const FValueWithId& Value) = 0;
	virtual FCacheRecord Build() = 0;
	virtual void BuildAsync(IRequestOwner& Owner, FOnCacheRecordComplete&& OnComplete) = 0;
};

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
 * of the values are exposed by the cache record.
 *
 * The value, attachments, and metadata are optional and can be skipped when requesting a record.
 * When the value or attachments have been skipped, the record will contain a value with the hash
 * and size, but with null data.
 */
class FCacheRecord
{
public:
	/** Returns the key that identifies this record in the cache. */
	inline const FCacheKey& GetKey() const { return Record->GetKey(); }

	/** Returns the metadata. Null when requested with ECachePolicy::SkipMeta. */
	inline const FCbObject& GetMeta() const { return Record->GetMeta(); }

	/** Returns the value. Data is null if skipped by the policy on request. */
	inline const FValueWithId& GetValue() const { return Record->GetValue(); }

	/** Returns the attachment matching the ID. Data is null if skipped by the policy on request. */
	inline const FValueWithId& GetAttachment(const FValueId& Id) const { return Record->GetAttachment(Id); }

	/** Returns a view of the attachments. Always available, but data may be skipped if skipped. */
	inline TConstArrayView<FValueWithId> GetAttachments() const { return Record->GetAttachments(); }

	/** Returns the value matching the ID. Null if no match. Data is null if skipped. */
	inline const FValueWithId& GetValue(const FValueId& Id) const { return Record->GetValue(Id); }

	/** Save the cache record to a compact binary package. */
	UE_API FCbPackage Save() const;

	/** Load a cache record from a compact binary package. Null on error. */
	UE_API static FOptionalCacheRecord Load(const FCbPackage& Package);
	UE_API static FOptionalCacheRecord Load(const FCbPackage& Attachments, const FCbObject& Object);

private:
	friend class FOptionalCacheRecord;
	friend FCacheRecord Private::CreateCacheRecord(Private::ICacheRecordInternal* Record);

	/** Construct a cache record. Use Build() or BuildAsync() on FCacheRecordBuilder. */
	inline explicit FCacheRecord(Private::ICacheRecordInternal* InRecord)
		: Record(InRecord)
	{
	}

	TRefCountPtr<Private::ICacheRecordInternal> Record;
};

/**
 * A cache record builder is used to construct a cache record.
 *
 * Create using a key that uniquely corresponds to the value and attachments for the cache record.
 * Metadata may vary between records of the same key.
 *
 * @see FCacheRecord
 */
class FCacheRecordBuilder
{
public:
	/**
	 * Create a cache record builder from a cache key.
	 */
	UE_API explicit FCacheRecordBuilder(const FCacheKey& Key);

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
	 * @param Buffer      The value, which is compressed by the builder, and cloned if not owned.
	 * @param Id          An ID for the value that is unique within this cache record.
	 *                    When omitted, the hash of the buffer will be used as the ID.
	 * @param BlockSize   The power-of-two block size to encode raw data in. 0 is default.
	 */
	inline void SetValue(const FCompositeBuffer& Buffer, const FValueId& Id = FValueId(), const uint64 BlockSize = 0)
	{
		return RecordBuilder->SetValue(Buffer, Id, BlockSize);
	}
	inline void SetValue(const FSharedBuffer& Buffer, const FValueId& Id = FValueId(), const uint64 BlockSize = 0)
	{
		return RecordBuilder->SetValue(Buffer, Id, BlockSize);
	}

	/**
	 * Set the value for the cache record.
	 *
	 * @param Value   The value, which must have data unless it is known to be in the cache.
	 */
	inline void SetValue(const FValue& Value, const FValueId& Id = FValueId())
	{
		return RecordBuilder->SetValue(Value, Id);
	}
	inline void SetValue(const FValueWithId& Value)
	{
		return RecordBuilder->SetValue(Value);
	}

	/**
	 * Add an attachment to the cache record.
	 *
	 * @param Buffer      The attachment, which is compressed by the builder, and cloned if not owned.
	 * @param Id          An ID for the attachment that is unique within this cache record.
	 *                    When omitted, the hash of the buffer will be used as the ID.
	 * @param BlockSize   The power-of-two block size to encode raw data in. 0 is default.
	 */
	inline void AddAttachment(const FCompositeBuffer& Buffer, const FValueId& Id = FValueId(), const uint64 BlockSize = 0)
	{
		return RecordBuilder->AddAttachment(Buffer, Id, BlockSize);
	}
	inline void AddAttachment(const FSharedBuffer& Buffer, const FValueId& Id = FValueId(), const uint64 BlockSize = 0)
	{
		return RecordBuilder->AddAttachment(Buffer, Id, BlockSize);
	}

	/**
	 * Add an attachment to the cache record.
	 *
	 * @param Value   The value, which must have data unless it is known to be in the cache.
	 */
	inline void AddAttachment(const FValue& Value, const FValueId& Id = FValueId())
	{
		return RecordBuilder->AddAttachment(Value, Id);
	}
	inline void AddAttachment(const FValueWithId& Value)
	{
		return RecordBuilder->AddAttachment(Value);
	}

	/**
	 * Build a cache record, which makes this builder subsequently unusable.
	 *
	 * Prefer BuildAsync() when the value or attachments are added from a buffer, as this must block
	 * on compression of those buffers before it can construct a cache record.
	 */
	inline FCacheRecord Build()
	{
		ON_SCOPE_EXIT { RecordBuilder = nullptr; };
		return RecordBuilder->Build();
	}

	/**
	 * Build a cache record asynchronously, which makes this builder subsequently unusable.
	 *
	 * Prefer Build() when the value and attachments are added by value, as compression is already
	 * complete and BuildAsync() will complete immediately in that case.
	 */
	inline void BuildAsync(IRequestOwner& Owner, FOnCacheRecordComplete&& OnComplete)
	{
		return RecordBuilder.Release()->BuildAsync(Owner, MoveTemp(OnComplete));
	}

private:
	TUniquePtr<Private::ICacheRecordBuilderInternal> RecordBuilder;
};

/**
 * A cache record that can be null.
 *
 * @see FCacheRecord
 */
class FOptionalCacheRecord : private FCacheRecord
{
public:
	inline FOptionalCacheRecord() : FCacheRecord(nullptr) {}

	inline FOptionalCacheRecord(FCacheRecord&& InRecord) : FCacheRecord(MoveTemp(InRecord)) {}
	inline FOptionalCacheRecord(const FCacheRecord& InRecord) : FCacheRecord(InRecord) {}
	inline FOptionalCacheRecord& operator=(FCacheRecord&& InRecord) { FCacheRecord::operator=(MoveTemp(InRecord)); return *this; }
	inline FOptionalCacheRecord& operator=(const FCacheRecord& InRecord) { FCacheRecord::operator=(InRecord); return *this; }

	/** Returns the cache record. The caller must check for null before using this accessor. */
	inline const FCacheRecord& Get() const & { return *this; }
	inline FCacheRecord&& Get() && { return MoveTemp(*this); }

	inline bool IsNull() const { return !IsValid(); }
	inline bool IsValid() const { return Record.IsValid(); }
	inline explicit operator bool() const { return IsValid(); }

	inline void Reset() { *this = FOptionalCacheRecord(); }
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

#undef UE_API

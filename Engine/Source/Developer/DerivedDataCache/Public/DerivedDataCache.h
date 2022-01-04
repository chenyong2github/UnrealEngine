// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/StringFwd.h"
#include "DerivedDataCacheKey.h"
#include "DerivedDataCacheRecord.h"
#include "DerivedDataPayloadId.h"
#include "DerivedDataRequestTypes.h"
#include "DerivedDataSharedString.h"
#include "Math/NumericLimits.h"
#include "Misc/EnumClassFlags.h"
#include "Templates/Function.h"
#include "Templates/RefCounting.h"

#define UE_API DERIVEDDATACACHE_API

namespace UE::DerivedData { class ICacheStoreMaintainer; }
namespace UE::DerivedData { class IRequestOwner; }
namespace UE::DerivedData { struct FCacheChunkCompleteParams; }
namespace UE::DerivedData { struct FCacheChunkRequest; }
namespace UE::DerivedData { struct FCacheGetCompleteParams; }
namespace UE::DerivedData { struct FCacheGetRequest; }
namespace UE::DerivedData { struct FCachePutCompleteParams; }
namespace UE::DerivedData { struct FCachePutRequest; }
namespace UE::DerivedData::Private { class ICacheRecordPolicyShared; }

namespace UE::DerivedData
{

using FCacheRequestName = FSharedString;

using FOnCachePutComplete = TUniqueFunction<void (FCachePutCompleteParams&& Params)>;
using FOnCacheGetComplete = TUniqueFunction<void (FCacheGetCompleteParams&& Params)>;
using FOnCacheChunkComplete = TUniqueFunction<void (FCacheChunkCompleteParams&& Params)>;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Flags to control the behavior of cache requests.
 *
 * The cache policy flags can be combined to support a variety of usage patterns. Examples:
 *
 * Get(Default): read from any cache; put to writable caches if missing.
 * Get(Remote): read any remote cache; put to writable remote caches if missing.
 * Get(Local): read any local cache; put to writable local caches if missing.
 * Get(Query | StoreRemote): read from any cache; put to writable remote caches if missing.
 * Get(Query | StoreLocal): read from any cache; put to writable local caches if missing.
 * Get(Query | SkipData): check for existence in any cache; do not modify any cache.
 * Get(Default | SkipData): check for existence in any cache; put to writable caches if missing.
 *
 * Put(Default): write to every writable cache.
 * Put(Remote): write to every writable remote cache, skipping local caches.
 * Put(Local): write to every writable local cache, skipping remote caches.
 */
enum class ECachePolicy : uint32
{
	/** A value without any flags set. */
	None            = 0,

	/** Allow a cache request to query local caches. */
	QueryLocal      = 1 << 0,
	/** Allow a cache request to query remote caches. */
	QueryRemote     = 1 << 1,
	/** Allow a cache request to query any caches. */
	Query           = QueryLocal | QueryRemote,

	/** Allow cache records and values to be stored in local caches. */
	StoreLocal      = 1 << 2,
	/** Allow cache records and values to be stored in remote caches. */
	StoreRemote     = 1 << 3,
	/** Allow cache records and values to be stored in any caches. */
	Store           = StoreLocal | StoreRemote,

	/** Skip fetching the metadata for record requests. */
	SkipMeta        = 1 << 4,
	/** Skip fetching the value for record, chunk, or value requests. */
	SkipValue       = 1 << 5,
	/** Skip fetching the attachments for record requests. */
	SkipAttachments = 1 << 6,
	/**
	 * Skip fetching the data for any requests.
	 *
	 * Put requests with skip flags may assume that record existence implies payload existence.
	 */
	SkipData        = SkipMeta | SkipValue | SkipAttachments,

	/**
	 * Keep records in the cache for at least the duration of the session.
	 *
	 * This is a hint that the record may be accessed again in this session. This is mainly meant
	 * to be used when subsequent accesses will not tolerate a cache miss.
	 */
	KeepAlive       = 1 << 7,

	/**
	 * Partial output will be provided with the error status when a required payload is missing.
	 *
	 * This is meant for cases when the missing payloads can be individually recovered or rebuilt
	 * without rebuilding the whole record. The cache automatically adds this flag when there are
	 * other cache stores that it may be able to recover missing payloads from.
	 *
	 * Requests for records would return records where the missing payloads have a hash and size,
	 * but no data. Requests for chunks or values would return the hash and size, but no data.
	 */
	PartialOnError  = 1 << 8,

	/** Allow cache requests to query and store records and values in local caches. */
	Local           = QueryLocal | StoreLocal,
	/** Allow cache requests to query and store records and values in remote caches. */
	Remote          = QueryRemote | StoreRemote,

	/** Allow cache requests to query and store records and values in any caches. */
	Default         = Query | Store,

	/** Do not allow cache requests to query or store records and values in any caches. */
	Disable         = None,
};

ENUM_CLASS_FLAGS(ECachePolicy);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** A payload ID and the cache policy to use for that payload. */
struct FCachePayloadPolicy
{
	FPayloadId Id;
	ECachePolicy Policy = ECachePolicy::Default;
};

/** Interface for the private implementation of the cache record policy. */
class Private::ICacheRecordPolicyShared
{
public:
	virtual ~ICacheRecordPolicyShared() = default;
	virtual void AddRef() const = 0;
	virtual void Release() const = 0;
	virtual TConstArrayView<FCachePayloadPolicy> GetPayloadPolicies() const = 0;
	virtual void AddPayloadPolicy(const FCachePayloadPolicy& Policy) = 0;
	virtual void Build() = 0;
};

/**
 * Flags to control the behavior of cache requests, with optional overrides by payload.
 *
 * Examples:
 * - A base policy of Disable with payload policy overrides of Default will fetch those payloads,
 *   if they exist in the record, and skip data for any other payloads.
 * - A base policy of Default with payload policy overrides of (Query | SkipData) will skip those
 *   payloads, but still check if they exist, and will load any other payloads.
 */
class FCacheRecordPolicy
{
public:
	/** Construct a cache record policy that uses the default policy. */
	FCacheRecordPolicy() = default;

	/** Construct a cache record policy with a uniform policy for the record and every payload. */
	inline FCacheRecordPolicy(ECachePolicy Policy)
		: RecordPolicy(Policy)
		, DefaultPayloadPolicy(Policy)
	{
	}

	/** Returns true if the record and every payload use the same cache policy. */
	inline bool IsUniform() const { return !Shared && RecordPolicy == DefaultPayloadPolicy; }

	/** Returns the cache policy to use for the record. */
	inline ECachePolicy GetRecordPolicy() const { return RecordPolicy; }

	/** Returns the cache policy to use for the payload. */
	UE_API ECachePolicy GetPayloadPolicy(const FPayloadId& Id) const;

	/** Returns the cache policy to use for payloads with no override. */
	inline ECachePolicy GetDefaultPayloadPolicy() const { return DefaultPayloadPolicy; }

	/** Returns the array of cache policy overrides for payloads, sorted by ID. */
	inline TConstArrayView<FCachePayloadPolicy> GetPayloadPolicies() const
	{
		return Shared ? Shared->GetPayloadPolicies() : TConstArrayView<FCachePayloadPolicy>();
	}

private:
	friend class FCacheRecordPolicyBuilder;

	ECachePolicy RecordPolicy = ECachePolicy::Default;
	ECachePolicy DefaultPayloadPolicy = ECachePolicy::Default;
	TRefCountPtr<const Private::ICacheRecordPolicyShared> Shared;
};

/** A cache record policy builder is used to construct a cache record policy. */
class FCacheRecordPolicyBuilder
{
public:
	/** Construct a policy builder that uses the default policy as its base policy. */
	FCacheRecordPolicyBuilder() = default;

	/** Construct a policy builder that uses the provided policy for the record and payloads with no override. */
	inline explicit FCacheRecordPolicyBuilder(ECachePolicy Policy)
		: BasePolicy(Policy)
	{
	}

	/** Adds a cache policy override for a payload. */
	UE_API void AddPayloadPolicy(const FCachePayloadPolicy& Policy);
	inline void AddPayloadPolicy(const FPayloadId& Id, ECachePolicy Policy) { AddPayloadPolicy({Id, Policy}); }

	/** Build a cache record policy, which makes this builder subsequently unusable. */
	UE_API FCacheRecordPolicy Build();

private:
	ECachePolicy BasePolicy = ECachePolicy::Default;
	TRefCountPtr<Private::ICacheRecordPolicyShared> Shared;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Interface to a store of cache records.
 *
 * Functions on this interface may be called from any thread. When a callback is provided, it may
 * be invoked from a different thread than the request was made on, and may be invoked before the
 * request function returns, both of which must be supported by the caller and the callback.
 */
class ICacheStore
{
public:
	virtual ~ICacheStore() = default;

	/**
	 * Asynchronous request to put cache records according to the policy.
	 *
	 * The callback will always be called for every key, and may be called from an arbitrary thread.
	 * Records may finish storing in any order, and from multiple threads concurrently.
	 *
	 * A cache store is free to interpret a record containing only a key as a request to delete that
	 * record from the store. Records may contain payloads that do not have data, and these payloads
	 * must reference an existing copy of the payload in the store, if available, and must otherwise
	 * be stored as a partial record that can attempt recovery of missing payloads when fetched.
	 *
	 * @param Requests     Requests with the cache records to store. Records must have a key.
	 * @param Owner        The owner to execute the request within.
	 * @param OnComplete   A callback invoked for every record as it completes or is canceled.
	 */
	virtual void Put(
		TConstArrayView<FCachePutRequest> Requests,
		IRequestOwner& Owner,
		FOnCachePutComplete&& OnComplete = FOnCachePutComplete()) = 0;

	/**
	 * Asynchronous request to get cache records according to the policy.
	 *
	 * The callback will always be called for every key, and may be called from an arbitrary thread.
	 * Records may become available in any order, and from multiple threads concurrently.
	 *
	 * Records may propagate into other cache stores, in accordance with the policy. A propagated
	 * record may be a partial record, with some payloads missing data, depending on the policy.
	 *
	 * When payloads are required by the policy, but not available, the status must be Error. The
	 * cache store must produce a partial record with the available payloads when the policy flag
	 * PartialOnError is set for the missing payloads.
	 *
	 * @param Requests     Requests with the keys identifying the cache records to query.
	 * @param Owner        The owner to execute the request within.
	 * @param OnComplete   A callback invoked for every key as it completes or is canceled.
	 */
	virtual void Get(
		TConstArrayView<FCacheGetRequest> Requests,
		IRequestOwner& Owner,
		FOnCacheGetComplete&& OnComplete) = 0;

	/**
	 * Asynchronous request to get chunks, which are subsets of payloads, from cache records.
	 *
	 * The callback will always be called for every chunk, and may be called from an arbitrary thread.
	 * Chunks may become available in any order, and from multiple threads concurrently.
	 *
	 * There is no requirement that the cache store validate the existence of other payloads from
	 * the requested records. Requests for whole payloads may propagate those payloads into other
	 * cache stores, in accordance with the policy, through a put of a partial record.
	 *
	 * @param Requests     The keys, IDs, offsets, and sizes of the chunks to query.
	 * @param Owner        The owner to execute the request within.
	 * @param OnComplete   A callback invoked for every chunk as it completes or is canceled.
	 */
	virtual void GetChunks(
		TConstArrayView<FCacheChunkRequest> Requests,
		IRequestOwner& Owner,
		FOnCacheChunkComplete&& OnComplete) = 0;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Interface to the cache.
 *
 * @see ICacheStore for cache record storage.
 */
class ICache : public ICacheStore
{
public:
	virtual ~ICache() = default;

	/**
	 * Cancel all queued and active cache requests and invoke their callbacks.
	 *
	 * This is meant to be called before destroying the cache. Any new requests that are made during
	 * execution of this function will also be canceled, because callbacks might queue requests when
	 * they are invoked. This does not block any future cache requests, which must be handled by the
	 * owner of the cache if future requests are meant to be avoided.
	 */
	virtual void CancelAll() = 0;

	/** Returns the interface to the background cache store maintenance. */
	virtual ICacheStoreMaintainer& GetMaintainer() = 0;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** Parameters to request to put a cache record. */
struct FCachePutRequest
{
	/** A name to identify this request for logging and profiling. An object path is typically sufficient. */
	FCacheRequestName Name;

	/** The cache record to put. */
	FCacheRecord Record;

	/** Flags to control the behavior of the request. See FCacheRecordPolicy. */
	FCacheRecordPolicy Policy;

	/** A value that will be returned in the completion callback. */
	uint64 UserData = 0;
};

/** Parameters for the completion callback for cache put requests. */
struct FCachePutCompleteParams
{
	/** A copy of the name from the request. */
	FCacheRequestName Name;

	/** Key for the part of the put request that completed or was canceled. */
	FCacheKey Key;

	/** A copy of the value from the request. */
	uint64 UserData = 0;

	/** Status of the cache request. */
	EStatus Status = EStatus::Error;
};

/** Parameters to request a cache record. */
struct FCacheGetRequest
{
	/** A name to identify this request for logging and profiling. An object path is typically sufficient. */
	FCacheRequestName Name;

	/** The key identifying the cache record to fetch. */
	FCacheKey Key;

	/** Flags to control the behavior of the request. See FCacheRecordPolicy. */
	FCacheRecordPolicy Policy;

	/** A value that will be returned in the completion callback. */
	uint64 UserData = 0;
};

/** Parameters for the completion callback for cache get requests. */
struct FCacheGetCompleteParams
{
	/** A copy of the name from the request. */
	FCacheRequestName Name;

	/**
	 * Record for the part of the get request that completed or was canceled.
	 *
	 * The key is always populated. The remainder of the record is populated when Status is Ok.
	 *
	 * The value, attachments, and metadata may be skipped based on cache policy flags. When a value
	 * or attachment has been skipped, it will have a payload but its data will be null.
	 */
	FCacheRecord&& Record;

	/** A copy of the value from the request. */
	uint64 UserData = 0;

	/** Status of the cache request. */
	EStatus Status = EStatus::Error;
};

/** Parameters to request a chunk, which is a subset of a payload, from a cache record. */
struct FCacheChunkRequest
{
	/** A name to identify this request for logging and profiling. An object path is typically sufficient. */
	FCacheRequestName Name;

	/** The key identifying the cache record to fetch the payload from. */
	FCacheKey Key;

	/** The ID of the payload to fetch from the cache record. */
	FPayloadId Id;

	/** The offset into the raw bytes of the payload at which to start fetching. */
	uint64 RawOffset = 0;

	/** The maximum number of raw bytes of the payload to fetch, starting from the offset. */
	uint64 RawSize = MAX_uint64;

	/** The raw hash of the entire payload to fetch, if available, otherwise zero. */
	FIoHash RawHash;

	/** Flags to control the behavior of the request. See ECachePolicy. */
	ECachePolicy Policy = ECachePolicy::Default;

	/** A value that will be returned in the completion callback. */
	uint64 UserData = 0;
};

/** Parameters for the completion callback for cache chunk requests. */
struct FCacheChunkCompleteParams
{
	/** A copy of the name from the request. */
	FCacheRequestName Name;

	/** Key from the chunk request that completed or was canceled. */
	FCacheKey Key;

	/** ID from the chunk request that completed or was canceled. */
	FPayloadId Id;

	/** Offset from the chunk request that completed or was canceled. */
	uint64 RawOffset = 0;

	/** Size, in bytes, of the subset of the payload that was fetched, if any. */
	uint64 RawSize = 0;

	/** Hash of the entire payload, even if only a subset was fetched. */
	const FIoHash& RawHash;

	/** Data for the subset of the payload that was fetched when Status is Ok, otherwise null. */
	FSharedBuffer&& RawData;

	/** A copy of the value from the request. */
	uint64 UserData = 0;

	/** Status of the cache request. */
	EStatus Status = EStatus::Error;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** Returns a reference to the cache. Asserts if not available. */
UE_API ICache& GetCache();

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

} // UE::DerivedData

#undef UE_API

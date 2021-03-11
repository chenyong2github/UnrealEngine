// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "DerivedDataCacheKey.h"
#include "DerivedDataRequest.h"
#include "Misc/EnumClassFlags.h"

namespace UE
{
namespace DerivedData
{

class FCacheBucket;
class FCacheRecord;
class FCacheRecordBuilder;
class FPayload;
struct FCacheGetCompleteParams;
struct FCacheGetPayloadCompleteParams;
struct FCachePutCompleteParams;

using FOnCachePutComplete = TUniqueFunction<void (FCachePutCompleteParams&& Params)>;
using FOnCacheGetComplete = TUniqueFunction<void (FCacheGetCompleteParams&& Params)>;
using FOnCacheGetPayloadComplete = TUniqueFunction<void (FCacheGetPayloadCompleteParams&& Params)>;

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
 * Get(Default | SkipLocalCopy): read from any cache; put to writable remote caches if missing,
 *                               and to writable local caches if not present in any local cache.
 *
 * Put(Default): write to every writable cache.
 * Put(Remote): write to every writable remote cache, skipping local caches.
 * Put(Local): write to every writable local cache, skipping remote caches.
 */
enum class ECachePolicy : uint8
{
	/** A value without any flags set. */
	None            = 0,

	/** Allow a cache get request or payload request to query local caches. */
	QueryLocal      = 1 << 0,
	/** Allow a cache get request or payload request to query remote caches. */
	QueryRemote     = 1 << 1,
	/** Allow a cache get request or payload request to query local and remote caches. */
	Query           = QueryLocal | QueryRemote,

	/** Allow cache records to be stored in local caches by any cache request. */
	StoreLocal      = 1 << 2,
	/** Allow cache records to be stored in remote caches by any cache request. */
	StoreRemote     = 1 << 3,
	/** Allow cache records to be stored in local and remote caches by any cache request. */
	Store           = StoreLocal | StoreRemote,

	/** Skip fetching the metadata for get requests. */
	SkipMeta        = 1 << 4,
	/** Skip fetching the value for get requests and the payload for payload requests. */
	SkipValue       = 1 << 5,
	/** Skip fetching the attachments for get requests. */
	SkipAttachments = 1 << 6,
	/** Skip fetching the metadata or payloads for get requests or payload requests. */
	SkipData        = SkipMeta | SkipValue | SkipAttachments,

	/** Skip copying records to local caches for get requests if they already exist locally. */
	SkipLocalCopy   = 1 << 7,

	/** Allow cache requests to query and store records in local caches. */
	Local           = QueryLocal | StoreLocal,
	/** Allow cache requests to query and store records in remote caches. */
	Remote          = QueryRemote | StoreRemote,

	/** Allow cache requests to query and store records in local and remote caches. */
	Default         = Query | Store,

	/** Do not allow cache requests to query or store records in local or remote caches. */
	Disable         = None,
};

ENUM_CLASS_FLAGS(ECachePolicy);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Interface to the cache.
 *
 * Functions on this interface may be called from any thread. When a callback is provided, it may
 * be invoked from a different thread than the request was made on, and may be invoked before the
 * request function returns, both of which must be supported by the callback.
 */
class ICache
{
public:
	virtual ~ICache() = default;

	/**
	 * Create a cache bucket from a name.
	 *
	 * A cache bucket name must be alphanumeric, non-empty, and contain fewer than 256 code units.
	 */
	virtual FCacheBucket CreateBucket(FStringView Name) = 0;

	/**
	 * Create a cache record builder from a cache key.
	 */
	virtual FCacheRecordBuilder CreateRecord(const FCacheKey& Key) = 0;

	/**
	 * Asynchronous request to put cache records according to the policy.
	 *
	 * The callback will always be called for every key, and may be called from an arbitrary thread.
	 * Records may finish storing in any order, and from multiple threads concurrently.
	 *
	 * @param Records The cache records to store. Must have a key. Records are reset to null.
	 * @param Context A description of the request. An object path is typically sufficient.
	 * @param Policy Flags to control the behavior of the request. See ECachePolicy.
	 * @param Priority A priority to consider when scheduling the request. See EPriority.
	 * @param Callback A callback invoked for every key in the batch as it completes or is canceled.
	 */
	virtual FRequest Put(
		TArrayView<FCacheRecord> Records,
		FStringView Context,
		ECachePolicy Policy = ECachePolicy::Default,
		EPriority Priority = EPriority::Normal,
		FOnCachePutComplete&& Callback = FOnCachePutComplete()) = 0;

	/**
	 * Asynchronous request to get cache records according to the policy.
	 *
	 * The callback will always be called for every key, and may be called from an arbitrary thread.
	 * Records may become available in any order, and from multiple threads concurrently.
	 *
	 * @param Keys The keys identifying the cache records to query.
	 * @param Context A description of the request. An object path is typically sufficient.
	 * @param Policy Flags to control the behavior of the request. See ECachePolicy.
	 * @param Priority A priority to consider when scheduling the request. See EPriority.
	 * @param Callback A callback invoked for every key in the batch as it completes or is canceled.
	 */
	virtual FRequest Get(
		TConstArrayView<FCacheKey> Keys,
		FStringView Context,
		ECachePolicy Policy,
		EPriority Priority,
		FOnCacheGetComplete&& Callback) = 0;

	/**
	 * Asynchronous request to get cache record payloads according to the policy.
	 *
	 * The callback will always be called for every key, and may be called from an arbitrary thread.
	 * Payloads may become available in any order, and from multiple threads concurrently.
	 *
	 * @param Keys The keys identifying the cache record payloads to query.
	 * @param Context A description of the request. An object path is typically sufficient.
	 * @param Policy Flags to control the behavior of the request. See ECachePolicy.
	 * @param Priority A priority to consider when scheduling the request. See EPriority.
	 * @param Callback A callback invoked for every key in the batch as it completes or is canceled.
	 */
	virtual FRequest GetPayload(
		TConstArrayView<FCachePayloadKey> Keys,
		FStringView Context,
		ECachePolicy Policy,
		EPriority Priority,
		FOnCacheGetPayloadComplete&& Callback) = 0;

	/**
	 * Cancel all queued and active cache requests and invoke their callbacks.
	 *
	 * This is meant to be called before destroying the cache. Any new requests that are made during
	 * execution of this function will also be canceled, because callbacks might queue requests when
	 * they are invoked. This does not block any future cache requests, which must be handled by the
	 * owner of the cache if future requests are meant to be avoided.
	 */
	virtual void CancelAll() = 0;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** Parameters for the completion callback for cache put requests. */
struct FCachePutCompleteParams
{
	/** Key for the part of the put request that completed or was canceled. */
	FCacheKey Key;

	/** Status of the cache request. */
	EStatus Status = EStatus::Error;
};

/** Parameters for the completion callback for cache get requests. */
struct FCacheGetCompleteParams
{
	/**
	 * Record for the part of the get request that completed or was canceled.
	 *
	 * The key is always populated. The remainder of the record is populated when Status is Cached.
	 *
	 * The value, attachments, and metadata may be skipped based on cache policy flags. When a value
	 * or attachment has been skipped, it will have a payload but its buffers will be null.
	 */
	FCacheRecord&& Record;

	/** Status of the cache request. */
	EStatus Status = EStatus::Error;
};

/** Parameters for the completion callback for cache payload requests. */
struct FCacheGetPayloadCompleteParams
{
	/** Key for the part of the payload request that completed or was canceled. See Payload for ID. */
	FCacheKey Key;

	/**
	 * Payload for the part of the payload request that completed or was canceled.
	 *
	 * The ID is always populated. The hash and buffer are populated when Status is Cached.
	 */
	FPayload&& Payload;

	/** Status of the cache request. */
	EStatus Status = EStatus::Error;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

} // DerivedData
} // UE

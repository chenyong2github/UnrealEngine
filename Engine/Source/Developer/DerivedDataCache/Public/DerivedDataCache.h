// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "DerivedDataCacheKey.h"
#include "DerivedDataCacheRecord.h"
#include "Memory/MemoryFwd.h"
#include "Misc/EnumClassFlags.h"
#include "Templates/RefCounting.h"

namespace UE
{
namespace DerivedData
{

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

/** Priority for scheduling cache requests. */
enum class ECachePriority : uint8
{
	/**
	 * Blocking is to be used only when the thread making the request will wait on completion of the
	 * request before doing any other work. Requests at this priority level will be processed before
	 * any request at a lower priority level. The cache may choose to process part of the request on
	 * the thread making the request. Waiting on a request may increase its priority to this level.
	 */
	Blocking,
	/**
	 * Highest is the maximum priority for asynchronous cache requests, and is intended for requests
	 * that are required to maintain interactivity of the program.
	 */
	Highest,
	/**
	 * High is intended for requests that are on the critical path, but are not required to maintain
	 * interactivity of the program.
	 */
	High,
	/**
	 * Normal is intended as the default request priority.
	 */
	Normal,
	/**
	 * Low is intended for requests that are below the default priority, but are used for operations
	 * that the program will execute now rather than at an unknown time in the future.
	 */
	Low,
	/**
	 * Lowest is the minimum priority for asynchronous cache requests, and is primarily intended for
	 * requests that prefetch from the cache to optimize a future operation, while minimizing impact
	 * on other cache requests that are in flight.
	 */
	Lowest,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** Status of a cache operation. */
enum class ECacheStatus : uint8
{
	/** A cache miss for a get operation, or a failure for a put operation. */
	NotCached,
	/** A cache hit for a get operation, or a success for a put operation. */
	Cached,
	/** The operation was canceled before it completed. */
	Canceled,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** Interface for a cache request. Provides functionality common to every type of request. */
class ICacheRequest : public IRefCountedObject
{
public:
	virtual ~ICacheRequest() = default;

	/** Set the priority of the request. */
	virtual void SetPriority(ECachePriority Priority) = 0;

	/** Cancel the request and invoke its callback. */
	virtual void Cancel() = 0;

	/** Block the calling thread until the request and its callback complete. */
	virtual void Wait() = 0;

	/** Poll the request and return true if complete and false otherwise. */
	virtual bool Poll() = 0;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** A wrapper for the cache request interface that manages lifetime with reference counting. */
class FCacheRequest
{
public:
	/** Construct a null cache request. */
	FCacheRequest() = default;

	/** Construct a cache request from . */
	inline explicit FCacheRequest(ICacheRequest* InRequest)
		: Request(InRequest)
	{
	}

	/** Reset this to null. */
	inline void Reset() { Request.SafeRelease(); }

	/** Whether the cache request is null. */
	inline bool IsNull() const { return !Request; }
	/** Whether the cache request is not null. */
	inline explicit operator bool() const { return !IsNull(); }

	/**
	 * Set the priority of the request.
	 *
	 * @param Priority The new priority for the request. May be higher or lower than the original.
	 */
	inline void SetPriority(ECachePriority Priority)
	{
		if (Request)
		{
			Request->SetPriority(Priority);
		}
	}

	/**
	 * Cancel the request and invoke its callback.
	 *
	 * Does not return until the callback for the request has finished executing.
	 */
	inline void Cancel() const
	{
		if (Request)
		{
			Request->Cancel();
		}
	}

	/**
	 * Block the calling thread until the request is complete.
	 *
	 * Avoid waiting if possible and use callbacks to manage control flow instead.
	 * Does not return until the callback for the request has finished executing.
	 */
	inline void Wait() const
	{
		if (Request)
		{
			Request->Wait();
		}
	}

	/**
	 * Poll the request and return true if complete and false otherwise.
	 *
	 * Avoid polling if possible and use callbacks to manage control flow instead.
	 * A request is not considered to be complete until its callback has finished executing.
	 */
	inline bool Poll() const
	{
		return !Request || Request->Poll();
	}

private:
	TRefCountPtr<ICacheRequest> Request;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** Parameters for the completion callback for cache put requests. */
struct FCachePutCompleteParams
{
	/** Key for the part of the put request that completed or was canceled. */
	FCacheKey Key;

	/** Status of the cache request. */
	ECacheStatus Status = ECacheStatus::NotCached;
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
	FCacheRecord Record;

	/** Status of the cache request. */
	ECacheStatus Status = ECacheStatus::NotCached;
};

/** Parameters for the completion callback for cache payload requests. */
struct FCacheGetPayloadCompleteParams
{
	/** Key for the part of the payload request that completed or was canceled. See Payload for ID. */
	FCacheKey Key;

	/**
	 * Payload for the part of the payload request that completed or was canceled.
	 *
	 * The ID is always populated. The remainder of the payload is populated when Status is Cached.
	 */
	FCachePayload Payload;

	/** Status of the cache request. */
	ECacheStatus Status = ECacheStatus::NotCached;
};

/** Callback for completion of a put request. */
using FOnCachePutComplete = TUniqueFunction<void (FCachePutCompleteParams&& Params)>;

/** Callback for completion of a get request. */
using FOnCacheGetComplete = TUniqueFunction<void (FCacheGetCompleteParams&& Params)>;

/** Callback for completion of a payload request. */
using FOnCacheGetPayloadComplete = TUniqueFunction<void (FCacheGetPayloadCompleteParams&& Params)>;

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
	 * Asynchronous request to put cache records according to the policy.
	 *
	 * The callback will always be called for every key, and may be called from an arbitrary thread.
	 * Records may finish storing in any order, and from multiple threads concurrently.
	 *
	 * @param Records The cache records to store. Must have a key. Records are reset to null.
	 * @param Context A description of the request. An object path is typically sufficient.
	 * @param Policy Flags to control the behavior of the request. See ECachePolicy.
	 * @param Priority A priority to consider when scheduling the request. See ECachePriority.
	 * @param Callback A callback invoked for every key in the batch as it completes or is canceled.
	 */
	virtual FCacheRequest Put(
		TArrayView<FCacheRecord> Records,
		FStringView Context,
		ECachePolicy Policy = ECachePolicy::Default,
		ECachePriority Priority = ECachePriority::Normal,
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
	 * @param Priority A priority to consider when scheduling the request. See ECachePriority.
	 * @param Callback A callback invoked for every key in the batch as it completes or is canceled.
	 */
	virtual FCacheRequest Get(
		TConstArrayView<FCacheKey> Keys,
		FStringView Context,
		ECachePolicy Policy,
		ECachePriority Priority,
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
	 * @param Priority A priority to consider when scheduling the request. See ECachePriority.
	 * @param Callback A callback invoked for every key in the batch as it completes or is canceled.
	 */
	virtual FCacheRequest GetPayloads(
		TConstArrayView<FCachePayloadKey> Keys,
		FStringView Context,
		ECachePolicy Policy,
		ECachePriority Priority,
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

} // DerivedData
} // UE

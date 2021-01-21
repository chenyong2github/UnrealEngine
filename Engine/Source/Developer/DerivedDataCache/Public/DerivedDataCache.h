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

/** Flags to control the behavior of cache requests. */
enum class ECachePolicy : uint8
{
	/** A value without any flags set. */
	None            = 0,

	/** Allow a cache request to query local caches. */
	QueryLocal      = 1 << 0,
	/** Allow a cache request to query remote caches. */
	QueryRemote     = 1 << 1,
	/** Allow requests to query available local and remote caches. */
	Query           = QueryLocal | QueryRemote,

	/** Allow cache records to be stored in local caches. */
	StoreLocal      = 1 << 2,
	/** Allow cache records to be stored in remote caches. */
	StoreRemote     = 1 << 3,
	/** Allow cache records to be stored in local and remote caches. */
	Store           = StoreLocal | StoreRemote,

	/** Skip fetching the metadata for get requests. */
	SkipMeta        = 1 << 4,
	/** Skip fetching the value (binary, object, or the object of the package) for get requests. */
	SkipValue       = 1 << 5,
	/** Skip fetching the attachments for get requests. */
	SkipAttachments = 1 << 6,
	/** Skip fetching any data for get requests, which will only check for existence. */
	SkipAll         = SkipMeta | SkipValue | SkipAttachments,

	/** Allow requests to query and store records in local caches. */
	Local           = QueryLocal | StoreLocal,
	/** Allow requests to query and store records in remote caches. */
	Remote          = QueryRemote | StoreRemote,

	/** Allow requests to query and store records in local and remote caches. */
	Default         = Query | Store,

	/** Do not query or store records in the cache. */
	Disable         = None,
};

ENUM_CLASS_FLAGS(ECachePolicy);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** Priority for scheduling cache requests. */
enum class ECachePriority : uint8
{
	Highest,
	High,
	Normal,
	Low,
	Lowest,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** The status of a cache operation. */
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

/** Interface for a cache request. Provides functionality common to every request type. */
class ICacheRequest : public IRefCountedObject
{
public:
	virtual ~ICacheRequest() = default;

	/** Set the priority of the request. */
	virtual void SetPriority(ECachePriority Priority) = 0;

	/** Cancel the request and invoke its callback. */
	virtual void Cancel() = 0;

	/** Block the calling thread until the request is complete. */
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

/** Parameters for the completion callback for cache get requests. */
struct FCacheGetCompleteParams
{
	/**
	 * A cache record produced by the cache request.
	 *
	 * The key is always set, and the other fields of the record are set according to the flags used
	 * when the cache request was made.
	 */
	FCacheRecord Record;

	/** Status of the cache request. */
	ECacheStatus Status = ECacheStatus::NotCached;
};

/** Parameters for the completion callback for cache put requests. */
struct FCachePutCompleteParams
{
	/** Key used by the put request that completed. */
	FCacheKey Key;

	/** Status of the cache request. */
	ECacheStatus Status = ECacheStatus::NotCached;
};

/** Parameters for the completion callback for cache attachment requests. */
struct FCacheGetAttachmentCompleteParams
{
	/** Key used by the attachment request that completed. */
	FCacheAttachmentKey Key;

	/** Value of the attachment that was requested, if it was available. */
	FSharedBuffer Value;

	/** Status of the cache request. */
	ECacheStatus Status = ECacheStatus::NotCached;
};

/** Callback for completion of a get request. */
using FOnCacheGetComplete = TUniqueFunction<void (FCacheGetCompleteParams&& Params)>;

/** Callback for completion of a put request. */
using FOnCachePutComplete = TUniqueFunction<void (FCachePutCompleteParams&& Params)>;

/** Callback for completion of an attachment request. */
using FOnCacheGetAttachmentComplete = TUniqueFunction<void (FCacheGetAttachmentCompleteParams&& Params)>;

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
	 * Asynchronous request to put cache records according to the policy.
	 *
	 * The callback will always be called for every key, and may be called from an arbitrary thread.
	 * Records may finish storing in any order, and from multiple threads concurrently.
	 *
	 * @param Records The cache records to store. Must have a key and value.
	 * @param Context A description of the request. An object path is typically sufficient.
	 * @param Policy Flags to control the behavior of the request. See ECachePolicy.
	 * @param Priority A priority to consider when scheduling the request. See ECachePriority.
	 * @param Callback A callback invoked for every key in the batch as it completes or is canceled.
	 */
	virtual FCacheRequest Put(
		TConstArrayView<FCacheRecord> Records,
		FStringView Context,
		ECachePolicy Policy = ECachePolicy::Default,
		ECachePriority Priority = ECachePriority::Normal,
		FOnCachePutComplete&& Callback = FOnCachePutComplete()) = 0;

	/**
	 * Asynchronous request to get cache record attachments according to the policy.
	 *
	 * The callback will always be called for every key, and may be called from an arbitrary thread.
	 * Attachments may become available in any order, and from multiple threads concurrently.
	 *
	 * @param Keys The keys identifying the cache record attachments to query.
	 * @param Context A description of the request. An object path is typically sufficient.
	 * @param Policy Flags to control the behavior of the request. See ECachePolicy.
	 * @param Priority A priority to consider when scheduling the request. See ECachePriority.
	 * @param Callback A callback invoked for every key in the batch as it completes or is canceled.
	 */
	virtual FCacheRequest GetAttachments(
		TConstArrayView<FCacheAttachmentKey> Keys,
		FStringView Context,
		ECachePolicy Policy,
		ECachePriority Priority,
		FOnCacheGetAttachmentComplete&& Callback) = 0;

	/**
	 * Cancel all queued and active cache requests and invoke their callbacks.
	 *
	 * This is used to finish requests before destroying the cache.
	 */
	virtual void CancelAll() = 0;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

} // DerivedData
} // UE

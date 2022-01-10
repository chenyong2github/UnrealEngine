// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/StringFwd.h"
#include "DerivedDataCacheKey.h"
#include "DerivedDataCacheRecord.h"
#include "DerivedDataRequestTypes.h"
#include "DerivedDataSharedString.h"
#include "DerivedDataValue.h"
#include "DerivedDataValueId.h"
#include "Math/NumericLimits.h"
#include "Memory/SharedBuffer.h"
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
namespace UE::DerivedData { struct FCacheGetValueCompleteParams; }
namespace UE::DerivedData { struct FCacheGetValueRequest; }
namespace UE::DerivedData { struct FCachePutCompleteParams; }
namespace UE::DerivedData { struct FCachePutRequest; }
namespace UE::DerivedData { struct FCachePutValueCompleteParams; }
namespace UE::DerivedData { struct FCachePutValueRequest; }
namespace UE::DerivedData::Private { class ICacheRecordPolicyShared; }

namespace UE::DerivedData
{

using FCacheRequestName = FSharedString;

using FOnCachePutComplete = TUniqueFunction<void (FCachePutCompleteParams&& Params)>;
using FOnCacheGetComplete = TUniqueFunction<void (FCacheGetCompleteParams&& Params)>;
using FOnCachePutValueComplete = TUniqueFunction<void (FCachePutValueCompleteParams&& Params)>;
using FOnCacheGetValueComplete = TUniqueFunction<void (FCacheGetValueCompleteParams&& Params)>;
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
	/** Skip fetching the data for values. */
	SkipData        = 1 << 5,

	/**
	 * Partial output will be provided with the error status when a required value is missing.
	 *
	 * This is meant for cases when the missing values can be individually recovered, or rebuilt,
	 * without rebuilding the whole record. The cache automatically adds this flag when there are
	 * other cache stores that it may be able to recover missing values from.
	 *
	 * Missing values will be returned in the records or chunks, but with only the hash and size.
	 *
	 * Applying this flag for a put of a record allows a partial record to be stored.
	 */
	PartialOnError  = 1 << 6,

	/**
	 * Keep records in the cache for at least the duration of the session.
	 *
	 * This is a hint that the record may be accessed again in this session. This is mainly meant
	 * to be used when subsequent accesses will not tolerate a cache miss.
	 */
	KeepAlive       = 1 << 7,

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

/** A value ID and the cache policy to use for that value. */
struct FCacheValuePolicy
{
	FValueId Id;
	ECachePolicy Policy = ECachePolicy::Default;
};

/** Interface for the private implementation of the cache record policy. */
class Private::ICacheRecordPolicyShared
{
public:
	virtual ~ICacheRecordPolicyShared() = default;
	virtual void AddRef() const = 0;
	virtual void Release() const = 0;
	virtual TConstArrayView<FCacheValuePolicy> GetValuePolicies() const = 0;
	virtual void AddValuePolicy(const FCacheValuePolicy& Policy) = 0;
	virtual void Build() = 0;
};

/**
 * Flags to control the behavior of cache record requests, with optional overrides by value.
 *
 * Examples:
 * - A base policy of Disable, with value policy overrides of Default, will fetch those values if
 *   they exist in the record, and skip data for any other values.
 * - A base policy of Default, with value policy overrides of (Query | SkipData), will skip those
 *   values, but still check if they exist, and will load any other values.
 */
class FCacheRecordPolicy
{
public:
	/** Construct a cache record policy that uses the default policy. */
	FCacheRecordPolicy() = default;

	/** Construct a cache record policy with a uniform policy for the record and every value. */
	inline FCacheRecordPolicy(ECachePolicy Policy)
		: RecordPolicy(Policy)
		, DefaultValuePolicy(Policy)
	{
	}

	/** Returns true if the record and every value use the same cache policy. */
	inline bool IsUniform() const { return !Shared && RecordPolicy == DefaultValuePolicy; }

	/** Returns the cache policy to use for the record. */
	inline ECachePolicy GetRecordPolicy() const { return RecordPolicy; }

	/** Returns the cache policy to use for the value. */
	UE_API ECachePolicy GetValuePolicy(const FValueId& Id) const;

	/** Returns the cache policy to use for values with no override. */
	inline ECachePolicy GetDefaultValuePolicy() const { return DefaultValuePolicy; }

	/** Returns the array of cache policy overrides for values, sorted by ID. */
	inline TConstArrayView<FCacheValuePolicy> GetValuePolicies() const
	{
		return Shared ? Shared->GetValuePolicies() : TConstArrayView<FCacheValuePolicy>();
	}

private:
	friend class FCacheRecordPolicyBuilder;

	ECachePolicy RecordPolicy = ECachePolicy::Default;
	ECachePolicy DefaultValuePolicy = ECachePolicy::Default;
	TRefCountPtr<const Private::ICacheRecordPolicyShared> Shared;
};

/** A cache record policy builder is used to construct a cache record policy. */
class FCacheRecordPolicyBuilder
{
public:
	/** Construct a policy builder that uses the default policy as its base policy. */
	FCacheRecordPolicyBuilder() = default;

	/** Construct a policy builder that uses the provided policy for the record and values with no override. */
	inline explicit FCacheRecordPolicyBuilder(ECachePolicy Policy)
		: BasePolicy(Policy)
	{
	}

	/** Adds a cache policy override for a value. */
	UE_API void AddValuePolicy(const FCacheValuePolicy& Policy);
	inline void AddValuePolicy(const FValueId& Id, ECachePolicy Policy) { AddValuePolicy({Id, Policy}); }

	/** Build a cache record policy, which makes this builder subsequently unusable. */
	UE_API FCacheRecordPolicy Build();

private:
	ECachePolicy BasePolicy = ECachePolicy::Default;
	TRefCountPtr<Private::ICacheRecordPolicyShared> Shared;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Interface to a store of cache records and cache values.
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
	 * record from the store. Records may contain values that do not have data, and such values must
	 * reference an existing value in the store, if available. The PartialOnError policy may be used
	 * to request that a partial record be stored when a value is missing.
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
	 * record may be a partial record, containing values without data, depending on the policy. A
	 * propagated put of a partial record will include the PartialOnError policy.
	 *
	 * When values are required by the policy but not available, the status must be Error. If the
	 * PartialOnError policy applies to the missing values, the cache store must return a partial
	 * record containing the available values.
	 *
	 * @param Requests     Requests with the keys identifying the cache records to fetch.
	 * @param Owner        The owner to execute the request within.
	 * @param OnComplete   A callback invoked for every key as it completes or is canceled.
	 */
	virtual void Get(
		TConstArrayView<FCacheGetRequest> Requests,
		IRequestOwner& Owner,
		FOnCacheGetComplete&& OnComplete) = 0;

	UE_API virtual void PutValue(
		TConstArrayView<FCachePutValueRequest> Requests,
		IRequestOwner& Owner,
		FOnCachePutValueComplete&& OnComplete = FOnCachePutValueComplete());

	UE_API virtual void GetValue(
		TConstArrayView<FCacheGetValueRequest> Requests,
		IRequestOwner& Owner,
		FOnCacheGetValueComplete&& OnComplete);

	/**
	 * Asynchronous request to get chunks, which are subsets of values, from records or values.
	 *
	 * The callback will always be called for every chunk, and may be called from an arbitrary thread.
	 * Chunks may become available in any order, and from multiple threads concurrently.
	 *
	 * Values may propagate into other cache stores, in accordance with the policy, if the entire
	 * value is requested, through a put of a partial record or a value. The cache store does not
	 * need to verify the existence of unrequested values within requested records.
	 *
	 * @param Requests     The keys, IDs, offsets, and sizes of the chunks to fetch.
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
 * @see ICacheStore for cache record and cache value storage.
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

	/** A record to store. */
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

	/** A copy of the key from the request. */
	FCacheKey Key;

	/** A copy of the value from the request. */
	uint64 UserData = 0;

	/** The status of the request. */
	EStatus Status = EStatus::Error;
};

/** Parameters to request to get a cache record. */
struct FCacheGetRequest
{
	/** A name to identify this request for logging and profiling. An object path is typically sufficient. */
	FCacheRequestName Name;

	/** A key identifying the record to fetch. */
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
	 * Record for the request that completed or was canceled.
	 *
	 * The key is always populated. The remainder of the record is populated when Status is Ok.
	 *
	 * The metadata or the data for values may be skipped based on cache policy flags. Values for
	 * which data has been skipped will have a hash and size but null data.
	 */
	FCacheRecord Record;

	/** A copy of the value from the request. */
	uint64 UserData = 0;

	/** The status of the request. */
	EStatus Status = EStatus::Error;
};

/** Parameters to request to put a cache value. */
struct FCachePutValueRequest
{
	/** A name to identify this request for logging and profiling. An object path is typically sufficient. */
	FCacheRequestName Name;

	/** A key that will uniquely identify the value in the cache. */
	FCacheKey Key;

	/** A value to store. */
	FValue Value;

	/** Flags to control the behavior of the request. See ECachePolicy. */
	ECachePolicy Policy = ECachePolicy::Default;

	/** A value that will be returned in the completion callback. */
	uint64 UserData = 0;
};

/** Parameters for the completion callback for cache value put requests. */
struct FCachePutValueCompleteParams
{
	/** A copy of the name from the request. */
	FCacheRequestName Name;

	/** A copy of the key from the request. */
	FCacheKey Key;

	/** A copy of the value from the request. */
	uint64 UserData = 0;

	/** The status of the request. */
	EStatus Status = EStatus::Error;
};

/** Parameters to request to get a cache value. */
struct FCacheGetValueRequest
{
	/** A name to identify this request for logging and profiling. An object path is typically sufficient. */
	FCacheRequestName Name;

	/** A key identifying the value to fetch. */
	FCacheKey Key;

	/** Flags to control the behavior of the request. See ECachePolicy. */
	ECachePolicy Policy = ECachePolicy::Default;

	/** A value that will be returned in the completion callback. */
	uint64 UserData = 0;
};

/** Parameters for the completion callback for cache value get requests. */
struct FCacheGetValueCompleteParams
{
	/** A copy of the name from the request. */
	FCacheRequestName Name;

	/** A copy of the key from the request. */
	FCacheKey Key;

	/**
	 * Value for the request that completed or was canceled.
	 *
	 * The data may be skipped based on cache policy flags. A value for which data has been skipped
	 * will have a hash and size but null data.
	 */
	FValue Value;

	/** A copy of the value from the request. */
	uint64 UserData = 0;

	/** The status of the request. */
	EStatus Status = EStatus::Error;
};

/** Parameters to request a chunk, which is a subset of a value, from a cache record or cache value. */
struct FCacheChunkRequest
{
	/** A name to identify this request for logging and profiling. An object path is typically sufficient. */
	FCacheRequestName Name;

	/** A key identifying the record or value to fetch the chunk from. */
	FCacheKey Key;

	/** An ID identifying the value to fetch, if fetching from a record, otherwise null. */
	FValueId Id;

	/** The offset into the raw bytes of the value at which to start fetching. */
	uint64 RawOffset = 0;

	/** The maximum number of raw bytes of the value to fetch, starting from the offset. */
	uint64 RawSize = MAX_uint64;

	/** The raw hash of the entire value to fetch, if available, otherwise zero. */
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

	/** A copy of the key from the request. */
	FCacheKey Key;

	/** A copy of the ID from the request. */
	FValueId Id;

	/** A copy of the offset from the request. */
	uint64 RawOffset = 0;

	/** The size, in bytes, of the subset of the value that was fetched, if any. */
	uint64 RawSize = 0;

	/** The hash of the entire value, even if only a subset was fetched. */
	const FIoHash& RawHash;

	/** Data for the subset of the value that was fetched when Status is Ok, otherwise null. */
	FSharedBuffer RawData;

	/** A copy of the value from the request. */
	uint64 UserData = 0;

	/** The status of the request. */
	EStatus Status = EStatus::Error;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** Returns a reference to the cache. Asserts if not available. */
UE_API ICache& GetCache();

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

} // UE::DerivedData

#undef UE_API

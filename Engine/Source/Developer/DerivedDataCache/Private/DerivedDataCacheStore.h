// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DerivedDataCache.h"

namespace UE::DerivedData
{

/**
 * Interface to a store of cache records and cache values.
 *
 * Functions on this interface may be called from any thread.
 */
class ICacheStore
{
public:
	virtual ~ICacheStore() = default;

	/**
	 * Asynchronous request to put records in the cache.
	 *
	 * Behavior must match the cache record policy in each request. A few of those requirements:
	 * - Store with Query is expected *not* to overwrite an existing record.
	 * - Store without Query is permitted to overwrite an existing record.
	 * - Response must be Error if any value has unrecoverable missing data, unless the policy has PartialRecord.
	 *
	 * @param Requests     Requests with the cache records to store. Records must have a key.
	 * @param Owner        The owner to execute the request within. See IRequestOwner.
	 * @param OnComplete   A callback invoked for every request as it completes or is canceled.
	 */
	virtual void Put(
		TConstArrayView<FCachePutRequest> Requests,
		IRequestOwner& Owner,
		FOnCachePutComplete&& OnComplete = {}) = 0;

	/**
	 * Asynchronous request to get records from the cache.
	 *
	 * Behavior must match the cache record policy in each request. A few of those requirements:
	 * - Data is required if the value policy has Query.
	 * - Data must not be in the response if the value policy has SkipData.
	 * - Response must contain values and available data if the record policy has PartialRecord.
	 * - Response must be Error if any required data is missing, even if the record policy has PartialRecord.
	 *
	 * @param Requests     Requests with the keys of the cache records to fetch.
	 * @param Owner        The owner to execute the request within. See IRequestOwner.
	 * @param OnComplete   A callback invoked for every request as it completes or is canceled.
	 */
	virtual void Get(
		TConstArrayView<FCacheGetRequest> Requests,
		IRequestOwner& Owner,
		FOnCacheGetComplete&& OnComplete) = 0;

	/**
	 * Asynchronous request to put values in the cache.
	 *
	 * @param Requests     Requests with the cache values to store. Requests must have a key.
	 * @param Owner        The owner to execute the request within. See IRequestOwner.
	 * @param OnComplete   A callback invoked for every request as it completes or is canceled.
	 */
	virtual void PutValue(
		TConstArrayView<FCachePutValueRequest> Requests,
		IRequestOwner& Owner,
		FOnCachePutValueComplete&& OnComplete = {});

	/**
	 * Asynchronous request to get values from the cache.
	 *
	 * @param Requests     Requests with the keys of the cache values to fetch.
	 * @param Owner        The owner to execute the request within. See IRequestOwner.
	 * @param OnComplete   A callback invoked for every request as it completes or is canceled.
	 */
	virtual void GetValue(
		TConstArrayView<FCacheGetValueRequest> Requests,
		IRequestOwner& Owner,
		FOnCacheGetValueComplete&& OnComplete);

	/**
	 * Asynchronous request to get chunks, which are subsets of values, from records or values.
	 *
	 * @param Requests     Requests with the key, ID, offset, and size of each chunk to fetch.
	 * @param Owner        The owner to execute the request within. See IRequestOwner.
	 * @param OnComplete   A callback invoked for every request as it completes or is canceled.
	 */
	virtual void GetChunks(
		TConstArrayView<FCacheGetChunkRequest> Requests,
		IRequestOwner& Owner,
		FOnCacheGetChunkComplete&& OnComplete) = 0;
};

} // UE::DerivedData

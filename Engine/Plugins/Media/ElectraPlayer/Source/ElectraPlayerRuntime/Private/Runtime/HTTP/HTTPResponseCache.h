// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PlayerCore.h"
#include "PlayerTime.h"
#include "ParameterDictionary.h"
#include "HTTP/HTTPManager.h"
#include "Interfaces/IHttpRequest.h"

namespace Electra
{
class IPlayerSessionServices;

class IHTTPResponseCache
{
public:
	struct FCacheItem
	{
		FString URL;
		IElectraHttpManager::FParams::FRange Range;
		FTimeValue ExpiresAtUTC;
		FHttpResponsePtr Response;
	};

	static TSharedPtrTS<IHTTPResponseCache> Create(IPlayerSessionServices* SessionServices, const FParamDict& Options);

	virtual ~IHTTPResponseCache() = default;

	/**
	 * Call this periodically to handle expiration times of cached entities.
	 */
	virtual void HandleEntityExpiration() = 0;

	/**
	 * Add an entity to the cache.
	 */
	virtual void CacheEntity(TSharedPtrTS<FCacheItem> EntityToAdd) = 0;
	/**
	 * Return a cached entity.
	 * Returns true if found, false if not.
	 */
	virtual bool GetCachedEntity(TSharedPtrTS<FCacheItem>& OutCachedEntity, const FString& URL, const IElectraHttpManager::FParams::FRange& Range) = 0;

protected:
	IHTTPResponseCache() = default;
	IHTTPResponseCache(const IHTTPResponseCache& other) = delete;
	IHTTPResponseCache& operator = (const IHTTPResponseCache& other) = delete;
};


} // namespace Electra


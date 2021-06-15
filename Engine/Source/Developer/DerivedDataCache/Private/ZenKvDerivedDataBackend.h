// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DerivedDataBackendInterface.h"
#include "DerivedDataCacheUsageStats.h"

// Macro for whether to enable the S3 backend. libcurl is not currently available on Mac.
#if PLATFORM_WINDOWS
#	define WITH_ZEN_DDC_BACKEND 1
#else
#	define WITH_ZEN_DDC_BACKEND 0
#endif

#if WITH_ZEN_DDC_BACKEND

typedef TSharedPtr<class IHttpRequest> FHttpRequestPtr;
typedef TSharedPtr<class IHttpResponse, ESPMode::ThreadSafe> FHttpResponsePtr;

namespace zenhttp {
	struct FRequestPool;
}

namespace UE::DerivedData::Backends
{

/**
 * Backend for a HTTP based caching service (Zen)
 **/
class FZenHttpDerivedDataBackend : public FDerivedDataBackendInterface
{
public:

	/**
	 * Creates the backend, checks health status and attempts to acquire an access token.
	 *
	 * @param ServiceUrl	Base url to the service including schema.
	 * @param Namespace		Namespace to use.
	 */
	FZenHttpDerivedDataBackend(ICacheFactory& Factory, const TCHAR* ServiceUrl, const TCHAR* Namespace);

	~FZenHttpDerivedDataBackend();

	/** return true if this cache is writable **/
	virtual bool IsWritable() const override;
	virtual ESpeedClass GetSpeedClass() const override;
	virtual bool TryToPrefetch(TConstArrayView<FString> CacheKeys) override;

	/**
	 * Retrieve usage stats for this backend. If the backend holds inner backends, this is expected to be passed down recursively.
	 */
	virtual TSharedRef<FDerivedDataCacheStatsNode> GatherUsageStats() const;

	/**
	 * Checks is backend is usable (reachable and accessible).
	 * @return true if usable
	 */
	bool IsUsable() const { return bIsUsable; }

	virtual bool CachedDataProbablyExists(const TCHAR* CacheKey) override;
	virtual bool GetCachedData(const TCHAR* CacheKey, TArray<uint8>& OutData) override;
	virtual EPutStatus PutCachedData(const TCHAR* CacheKey, TArrayView<const uint8> InData, bool bPutEvenIfExists) override;
	virtual void RemoveCachedData(const TCHAR* CacheKey, bool bTransient) override;

	virtual FString GetName() const override;
	virtual bool WouldCache(const TCHAR* CacheKey, TArrayView<const uint8> InData) override;
	virtual bool ApplyDebugOptions(FBackendDebugOptions& InOptions) override;

	// ICacheStore

	virtual FRequest Put(
		TConstArrayView<FCacheRecord> Records,
		FStringView Context,
		ECachePolicy Policy = ECachePolicy::Default,
		EPriority Priority = EPriority::Normal,
		FOnCachePutComplete&& OnComplete = FOnCachePutComplete());

	virtual FRequest Get(
		TConstArrayView<FCacheKey> Keys,
		FStringView Context,
		ECachePolicy Policy,
		EPriority Priority,
		FOnCacheGetComplete&& OnComplete);

	virtual FRequest GetPayload(
		TConstArrayView<FCachePayloadKey> Keys,
		FStringView Context,
		ECachePolicy Policy,
		EPriority Priority,
		FOnCacheGetPayloadComplete&& OnComplete);
	
	virtual void CancelAll();

private:
	ICacheFactory& Factory;
	FString Domain;
	FString Namespace;
	FString DefaultBucket;
	FCriticalSection AccessCs;
	FDerivedDataCacheUsageStats UsageStats;
	TUniquePtr<struct zenhttp::FRequestPool> RequestPool;
	bool bIsUsable;
	uint32 FailedLoginAttempts;

	bool IsServiceReady();
	bool ShouldRetryOnError(int64 ResponseCode);
};

} // UE::DerivedData::Backends

#endif //WITH_HTTP_DDC_BACKEND

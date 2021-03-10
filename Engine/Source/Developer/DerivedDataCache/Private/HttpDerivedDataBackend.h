// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DerivedDataBackendInterface.h"
#include "DerivedDataCacheUsageStats.h"

// Macro for whether to enable the S3 backend. libcurl is not currently available on Mac.
#if PLATFORM_WINDOWS
#define WITH_HTTP_DDC_BACKEND 1
#else
#define WITH_HTTP_DDC_BACKEND 0
#endif

#if WITH_HTTP_DDC_BACKEND

typedef TSharedPtr<class IHttpRequest> FHttpRequestPtr;
typedef TSharedPtr<class IHttpResponse, ESPMode::ThreadSafe> FHttpResponsePtr;


/**
 * Backend for a HTTP based caching service (Jupiter).
 **/
class FHttpDerivedDataBackend : public FDerivedDataBackendInterface
{
public:
	
	/**
	 * Creates the backend, checks health status and attempts to acquire an access token.
	 *
	 * @param ServiceUrl	Base url to the service including schema.
	 * @param Namespace		Namespace to use.
	 * @param OAuthProvider	Url to OAuth provider, for example "https://myprovider.com/oauth2/v1/token".
	 * @param OAuthClientId	OAuth client identifier.
	 * @param OAuthData		OAuth form data to send to login service. Can either be the raw form data or a Windows network file address (starting with "\\").
	 */
	FHttpDerivedDataBackend(const TCHAR* ServiceUrl, 
		const TCHAR* Namespace, 
		const TCHAR* OAuthProvider, 
		const TCHAR* OAuthClientId, 
		const TCHAR* OAuthData);

	~FHttpDerivedDataBackend();

	/**
	 * Checks is backend is usable (reachable and accessible).
	 * @return true if usable
	 */
	bool IsUsable() const { return bIsUsable; }

	virtual bool IsWritable() const override { return true; }
	virtual bool CachedDataProbablyExists(const TCHAR* CacheKey) override;
	virtual bool GetCachedData(const TCHAR* CacheKey, TArray<uint8>& OutData) override;
	virtual void PutCachedData(const TCHAR* CacheKey, TArrayView<const uint8> InData, bool bPutEvenIfExists) override;
	virtual void RemoveCachedData(const TCHAR* CacheKey, bool bTransient) override;
	virtual TSharedRef<FDerivedDataCacheStatsNode> GatherUsageStats() const override;
	
	virtual FString GetName() const override;
	virtual bool TryToPrefetch(const TCHAR* CacheKey) override;
	virtual bool WouldCache(const TCHAR* CacheKey, TArrayView<const uint8> InData) override;
	virtual ESpeedClass GetSpeedClass() const override;
	virtual bool ApplyDebugOptions(FBackendDebugOptions& InOptions) override;

private:
	
	FString Domain;
	FString Namespace;
	FString DefaultBucket;
	FString OAuthProvider;
	FString OAuthClientId;
	FString OAuthSecret;
	FCriticalSection AccessCs;
	FDerivedDataCacheUsageStats UsageStats;
	TUniquePtr<struct FRequestPool> RequestPool;
	TUniquePtr<struct FHttpAccessToken> Access;
	bool bIsUsable;
	uint32 FailedLoginAttempts;

	bool IsServiceReady();
	bool AcquireAccessToken();
	bool ShouldRetryOnError(int64 ResponseCode);
};

#endif //WITH_HTTP_DDC_BACKEND

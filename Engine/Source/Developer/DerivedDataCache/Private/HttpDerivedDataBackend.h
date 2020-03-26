// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DerivedDataBackendInterface.h"
#include "DerivedDataCacheUsageStats.h"

// Macro for whether to enable the S3 backend. libcurl is not currently available on Mac.
#if PLATFORM_WINDOWS
#define WITH_HTTP_DDC_BACKEND 0
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
	 * @param  InServiceUrl		Base url to the service
	 * @param  InNamespace		Namespace to use
	 * @param  InOAuthData		OAuth form data to send to login service. Can either be the raw form data or a Windows network file address (starting with "\\").
	 */
	FHttpDerivedDataBackend(const TCHAR* InServiceUrl, 
		const TCHAR* InNamespace, 
		const TCHAR* InOAuthProvider, 
		const TCHAR* InOAuthClientId, 
		const TCHAR* InOAuthData);

	~FHttpDerivedDataBackend();

	/**
	 * Checks is backend is usable (reachable and accessible).
	 * @return true if usable
	 */
	bool IsUsable() const { return bIsUsable; }

	bool IsWritable() override { return true; }
	bool CachedDataProbablyExists(const TCHAR* CacheKey) override;
	bool GetCachedData(const TCHAR* CacheKey, TArray<uint8>& OutData) override;
	void PutCachedData(const TCHAR* CacheKey, TArrayView<const uint8> InData, bool bPutEvenIfExists) override;
	void RemoveCachedData(const TCHAR* CacheKey, bool bTransient) override;
	void GatherUsageStats(TMap<FString, FDerivedDataCacheUsageStats>& UsageStatsMap, FString&& GraphPath) override;
	

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

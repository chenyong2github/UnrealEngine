// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Compression/CompressedBuffer.h"
#include "Containers/StringView.h"
#include "DerivedDataBackendInterface.h"
#include "DerivedDataCacheKey.h"
#include "DerivedDataCacheRecord.h"
#include "DerivedDataCacheUsageStats.h"

// Macro for whether to enable the Jupiter backend. libcurl is not currently available on Mac.
#if !defined(WITH_HTTP_DDC_BACKEND)
	#define WITH_HTTP_DDC_BACKEND 0
#endif

#if WITH_HTTP_DDC_BACKEND

class FCbPackage;

namespace UE::DerivedData::Backends
{

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
	 * @param ServiceUrl			Base url to the service including schema.
	 * @param Namespace				Namespace to use.
	 * @param StructuredNamespace	Namespace to use for structured cache operations.
	 * @param OAuthProvider			Url to OAuth provider, for example "https://myprovider.com/oauth2/v1/token".
	 * @param OAuthClientId			OAuth client identifier.
	 * @param OAuthData				OAuth form data to send to login service. Can either be the raw form data or a Windows network file address (starting with "\\").
	 */
	FHttpDerivedDataBackend(
		const TCHAR* ServiceUrl, 
		const TCHAR* Namespace, 
		const TCHAR* StructuredNamespace, 
		const TCHAR* OAuthProvider, 
		const TCHAR* OAuthClientId, 
		const TCHAR* OAuthData,
		bool bReadOnly);

	~FHttpDerivedDataBackend();

	/**
	 * Checks is backend is usable (reachable and accessible).
	 * @return true if usable
	 */
	bool IsUsable() const { return bIsUsable; }

	/** return true if this cache is writable **/
	virtual bool IsWritable() const override
	{
		return !bReadOnly && bIsUsable;
	}

	virtual bool CachedDataProbablyExists(const TCHAR* CacheKey) override;
	virtual TBitArray<> CachedDataProbablyExistsBatch(TConstArrayView<FString> CacheKeys) override;
	virtual bool GetCachedData(const TCHAR* CacheKey, TArray<uint8>& OutData) override;
	virtual EPutStatus PutCachedData(const TCHAR* CacheKey, TArrayView<const uint8> InData, bool bPutEvenIfExists) override;
	virtual void RemoveCachedData(const TCHAR* CacheKey, bool bTransient) override;
	virtual TSharedRef<FDerivedDataCacheStatsNode> GatherUsageStats() const override;
	
	virtual FString GetName() const override;
	virtual bool TryToPrefetch(TConstArrayView<FString> CacheKeys) override;
	virtual bool WouldCache(const TCHAR* CacheKey, TArrayView<const uint8> InData) override;
	virtual ESpeedClass GetSpeedClass() const override;
	virtual bool ApplyDebugOptions(FBackendDebugOptions& InOptions) override;

	void SetSpeedClass(ESpeedClass InSpeedClass) { SpeedClass = InSpeedClass; }

	virtual void Put(
		TConstArrayView<FCachePutRequest> Requests,
		IRequestOwner& Owner,
		FOnCachePutComplete&& OnComplete) override;

	virtual void Get(
		TConstArrayView<FCacheGetRequest> Requests,
		IRequestOwner& Owner,
		FOnCacheGetComplete&& OnComplete) override;

	virtual void GetChunks(
		TConstArrayView<FCacheChunkRequest> Requests,
		IRequestOwner& Owner,
		FOnCacheChunkComplete&& OnComplete) override;

	static FHttpDerivedDataBackend* GetAny()
	{
		return AnyInstance;
	}

	const FString& GetDomain() const { return Domain; }
	const FString& GetNamespace() const { return Namespace; }
	const FString& GetStructuredNamespace() const { return StructuredNamespace; }
	const FString& GetOAuthProvider() const { return OAuthProvider; }
	const FString& GetOAuthClientId() const { return OAuthClientId; }
	const FString& GetOAuthSecret() const { return OAuthSecret; }

private:
	FString Domain;
	FString Namespace;
	FString StructuredNamespace;
	FString DefaultBucket;
	FString OAuthProvider;
	FString OAuthClientId;
	FString OAuthSecret;
	FCriticalSection AccessCs;
	FDerivedDataCacheUsageStats UsageStats;
	FBackendDebugOptions DebugOptions;
	FCriticalSection MissedKeysCS;
	TSet<FName> DebugMissedKeys;
	TSet<FCacheKey> DebugMissedCacheKeys;
	TUniquePtr<struct FRequestPool> GetRequestPools[2];
	TUniquePtr<struct FRequestPool> PutRequestPools[2];
	TUniquePtr<struct FHttpAccessToken> Access;
	bool bIsUsable;
	bool bReadOnly;
	uint32 FailedLoginAttempts;
	ESpeedClass SpeedClass;
	static inline FHttpDerivedDataBackend* AnyInstance = nullptr;

	bool IsServiceReady();
	bool AcquireAccessToken();
	bool ShouldRetryOnError(int64 ResponseCode);
	bool ShouldSimulateMiss(const TCHAR* InKey);
	bool ShouldSimulateMiss(const FCacheKey& Key);

	bool PutCacheRecord(const FCacheRecord& Record, FStringView Context, const FCacheRecordPolicy& Policy, uint64& OutWriteSize);
	uint64 PutRef(const FCacheRecord& Record, const FCbPackage& Package, FStringView Bucket, bool bFinalize, TArray<FIoHash>& OutNeededBlobHashes, bool& bOutPutCompletedSuccessfully);

	FOptionalCacheRecord GetCacheRecordOnly(
		const FCacheKey& Key,
		const FStringView Context,
		const FCacheRecordPolicy& Policy);
	FOptionalCacheRecord GetCacheRecord(
		const FCacheKey& Key,
		const FStringView Context,
		const FCacheRecordPolicy& Policy,
		EStatus& OutStatus);
	bool TryGetCachedDataBatch(
		const FCacheKey& Key,
		TArrayView<FValueWithId> Values,
		const FStringView Context,
		TArray<FCompressedBuffer>& OutBuffers);
	bool CachedDataProbablyExistsBatch(
		const FCacheKey& Key,
		TConstArrayView<FValueWithId> Values,
		const FStringView Context);
};

} // UE::DerivedData::Backends

#endif //WITH_HTTP_DDC_BACKEND

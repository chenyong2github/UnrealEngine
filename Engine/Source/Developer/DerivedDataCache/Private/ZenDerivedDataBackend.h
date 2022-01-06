// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "ZenServerInterface.h"

// Macro for whether to enable the Zen DDC backend. libcurl is not currently available on Mac.

#if UE_WITH_ZEN
#	define WITH_ZEN_DDC_BACKEND 1
#else
#	define WITH_ZEN_DDC_BACKEND 0
#endif

#if WITH_ZEN_DDC_BACKEND

#include "Containers/Set.h"
#include "Containers/StringFwd.h"
#include "DerivedDataBackendInterface.h"
#include "DerivedDataCacheUsageStats.h"
#include "HAL/CriticalSection.h"
#include "ZenServerInterface.h"

class FCbObject;
class FCbPackage;
class FCbWriter;
class FCompositeBuffer;
struct FIoHash;

namespace UE::Zen {
	enum class EContentType;
	struct FZenHttpRequestPool;
}

namespace UE::DerivedData {
	struct FCacheKey;
	class FOptionalCacheRecord;
	struct FValueId;
}

namespace UE::DerivedData::Backends {

/**
 * Backend for a HTTP based caching service (Zen)
 **/
class FZenDerivedDataBackend : public FDerivedDataBackendInterface
{
public:
	
	/**
	 * Creates the backend, checks health status and attempts to acquire an access token.
	 *
	 * @param ServiceUrl	Base url to the service including schema.
	 * @param Namespace		Namespace to use.
	 */
	FZenDerivedDataBackend(const TCHAR* ServiceUrl, const TCHAR* Namespace);

	~FZenDerivedDataBackend();

	/**
	 * Checks is backend is usable (reachable and accessible).
	 * @return true if usable
	 */
	bool IsUsable() const { return bIsUsable; }

	virtual bool IsWritable() const override;
	virtual ESpeedClass GetSpeedClass() const override;
	virtual TSharedRef<FDerivedDataCacheStatsNode> GatherUsageStats() const override;

	/**
	 * Synchronous attempt to make sure the cached data will be available as optimally as possible.
	 *
	 * @param	CacheKeys	Alphanumeric+underscore keys of the cache items
	 * @return				true if the data will probably be found in a fast backend on a future request.
	 */
	virtual bool TryToPrefetch(TConstArrayView<FString> CacheKeys) override;

	virtual bool CachedDataProbablyExists(const TCHAR* CacheKey) override;
	virtual bool GetCachedData(const TCHAR* CacheKey, TArray<uint8>& OutData) override;
	virtual EPutStatus PutCachedData(const TCHAR* CacheKey, TArrayView<const uint8> InData, bool bPutEvenIfExists) override;

	virtual void RemoveCachedData(const TCHAR* CacheKey, bool bTransient) override;
	
	virtual FString GetName() const override;
	virtual bool WouldCache(const TCHAR* CacheKey, TArrayView<const uint8> InData) override;
	virtual bool ApplyDebugOptions(FBackendDebugOptions& InOptions) override;

	// ICacheStore

	virtual void Put(
		TConstArrayView<FCachePutRequest> Requests,
		IRequestOwner& Owner,
		FOnCachePutComplete&& OnComplete = FOnCachePutComplete()) override;

	virtual void Get(
		TConstArrayView<FCacheGetRequest> Requests,
		IRequestOwner& Owner,
		FOnCacheGetComplete&& OnComplete) override;

	virtual void GetChunks(
		TConstArrayView<FCacheChunkRequest> Requests,
		IRequestOwner& Owner,
		FOnCacheChunkComplete&& OnComplete) override;

private:
	enum class EGetResult
	{
		Success,
		NotFound,
		Corrupted
	};
	EGetResult GetZenData(FStringView Uri, TArray64<uint8>* OutData, Zen::EContentType ContentType) const;

	// TODO: need ability to specify content type
	FDerivedDataBackendInterface::EPutStatus PutZenData(const TCHAR* Uri, const FCompositeBuffer& InData, Zen::EContentType ContentType);
	EGetResult GetZenData(const FCacheKey& Key, ECachePolicy CachePolicy, FCbPackage& OutPackage) const;

	bool PutCacheRecord(const FCacheRecord& Record, const FCacheRecordPolicy& Policy);

	bool IsServiceReady();
	static FString MakeLegacyZenKey(const TCHAR* CacheKey);
	static void AppendZenUri(const FCacheKey& CacheKey, FStringBuilderBase& Out);
	static void AppendZenUri(const FCacheKey& CacheKey, const FValueId& Id, FStringBuilderBase& Out);
	static void AppendPolicyQueryString(ECachePolicy Policy, FStringBuilderBase& Out);

	static bool ShouldRetryOnError(int64 ResponseCode);
	static uint64 MeasureCacheRecord(const FCacheRecord& Record);

	/* Debug helpers */
	bool ShouldSimulateMiss(const TCHAR* InKey);
	bool ShouldSimulateMiss(const FCacheKey& InKey);
private:
	FString Namespace;
	UE::Zen::FScopeZenService ZenService;
	mutable FDerivedDataCacheUsageStats UsageStats;
	TUniquePtr<UE::Zen::FZenHttpRequestPool> RequestPool;
	bool bIsUsable = false;
	bool bIsRemote = false;
	uint32 FailedLoginAttempts = 0;
	uint32 MaxAttempts = 4;
	int32 CacheRecordBatchSize = 8;
	int32 CacheChunksBatchSize = 8;

	/** Debug Options */
	FBackendDebugOptions DebugOptions;

	/** Keys we ignored due to miss rate settings */
	FCriticalSection MissedKeysCS;
	TSet<FName> DebugMissedKeys;
	TSet<FCacheKey> DebugMissedCacheKeys;
};

} // namespace UE::DerivedData::Backends

#endif // WITH_ZEN_DDC_BACKEND

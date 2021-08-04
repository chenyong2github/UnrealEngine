// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "ZenServerInterface.h"

// Macro for whether to enable the S3 backend. libcurl is not currently available on Mac.
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
#include "DerivedDataRequest.h"
#include "HAL/CriticalSection.h"

class FCbObject;
class FCbPackage;
class FCbWriter;
class FCompositeBuffer;
struct FIoHash;

namespace UE::Zen {
	enum class EContentType;
	struct FRequestPool;
}

namespace UE::DerivedData {
	struct FCacheKey;
	class FOptionalCacheRecord;
	class FPayload;
	struct FPayloadId;
}

namespace UE::DerivedData {

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
	FZenDerivedDataBackend(ICacheFactory& Factory, const TCHAR* ServiceUrl, const TCHAR* Namespace);

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

	virtual FRequest Put(
		TConstArrayView<FCacheRecord> Records,
		FStringView Context,
		ECachePolicy Policy = ECachePolicy::Default,
		EPriority Priority = EPriority::Normal,
		FOnCachePutComplete&& OnComplete = FOnCachePutComplete()) override;

	virtual FRequest Get(
		TConstArrayView<FCacheKey> Keys,
		FStringView Context,
		ECachePolicy Policy,
		EPriority Priority,
		FOnCacheGetComplete&& OnComplete) override;

	virtual FRequest GetPayload(
		TConstArrayView<FCachePayloadKey> Keys,
		FStringView Context,
		ECachePolicy Policy,
		EPriority Priority,
		FOnCacheGetPayloadComplete&& OnComplete) override;

	virtual void CancelAll() override;

private:
	enum class EGetResult
	{
		Success,
		NotFound,
		Corrupted
	};
	EGetResult GetZenData(const TCHAR* Uri, TArray<uint8>* OutData) const;

	// TODO: need ability to specify content type
	FDerivedDataBackendInterface::EPutStatus PutZenData(const TCHAR* Uri, const FCompositeBuffer& InData, Zen::EContentType ContentType);
	EGetResult GetZenData(const FCacheKey& Key, ECachePolicy CachePolicy, FCbPackage& OutPackage) const;

	bool PutCacheRecord(const FCacheRecord& Record, FStringView Context, ECachePolicy Policy);
	FOptionalCacheRecord GetCacheRecord(const FCacheKey& Key, FStringView Context,
		ECachePolicy Policy) const;

	bool IsServiceReady();
	static FString MakeLegacyZenKey(const TCHAR* CacheKey);
	static void AppendZenUri(const FCacheKey& CacheKey, FStringBuilderBase& Out);
	static void AppendZenUri(const FCacheKey& CacheKey, const FPayloadId& PayloadId, FStringBuilderBase& Out);
	static void AppendPolicyQueryString(ECachePolicy Policy, FStringBuilderBase& Out);

	static bool ShouldRetryOnError(int64 ResponseCode);
	static uint64 MeasureCacheRecord(const FCacheRecord& Record);

	// Legacy CacheRecord endpoint
	bool LegacyPutCacheRecord(const FCacheRecord& Record, FStringView Context, ECachePolicy Policy);
	bool LegacyPutCachePayload(const FCacheKey& Key, FStringView Context, const FPayload& Payload, FCbWriter& Writer);
	FOptionalCacheRecord LegacyGetCacheRecord(const FCacheKey& Key, FStringView Context,
		ECachePolicy Policy, bool bAlwaysLoadInlineData = false) const;
	void LegacyMakeZenKey(const FCacheKey& CacheKey, FStringBuilderBase& Out) const;
	void LegacyMakePayloadKey(const FCacheKey& CacheKey, const FIoHash& RawHash, FStringBuilderBase& Out) const;
	FPayload LegacyGetCachePayload(const FCacheKey& Key, FStringView Context, ECachePolicy Policy, const FPayload& Payload) const;
	FOptionalCacheRecord LegacyCreateRecord(FSharedBuffer&& RecordBytes, const FCacheKey& Key, FStringView Context,
		ECachePolicy Policy, bool bAlwaysLoadInlineData) const;
	FPayload LegacyGetCachePayload(const FCacheKey& Key, FStringView Context, ECachePolicy Policy, const FCbObject& Object,
		bool bAlwaysLoadInlineData = false) const;
	FPayload LegacyValidateCachePayload(const FCacheKey& Key, FStringView Context, const FPayload& Payload,
		const FIoHash& CompressedHash, FSharedBuffer&& CompressedData) const;

	/* Debug helpers */
	bool DidSimulateMiss(const TCHAR* InKey);
	bool ShouldSimulateMiss(const TCHAR* InKey);
private:
	ICacheFactory& Factory;
	FString Domain;
	FString Namespace;
	mutable FDerivedDataCacheUsageStats UsageStats;
	TUniquePtr<UE::Zen::FRequestPool> RequestPool;
	bool bIsUsable = false;
	uint32 FailedLoginAttempts = 0;
	uint32 MaxAttempts = 4;
	bool bCacheRecordEndpointEnabled = false;

	/** Debug Options */
	FBackendDebugOptions DebugOptions;

	/** Keys we ignored due to miss rate settings */
	FCriticalSection MissedKeysCS;
	TSet<FName> DebugMissedKeys;
};

} // namespace UE::DerivedData

#endif // WITH_ZEN_DDC_BACKEND

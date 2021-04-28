// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DerivedDataBackendInterface.h"
#include "DerivedDataCacheUsageStats.h"

// Macro for whether to enable the S3 backend. libcurl is not currently available on Mac.
#if PLATFORM_WINDOWS
	#define WITH_S3_DDC_BACKEND 1
#else
	#define WITH_S3_DDC_BACKEND 0
#endif

#if WITH_S3_DDC_BACKEND

namespace UE::DerivedData::Backends
{

/**
 * Backend for a read-only AWS S3 based caching service.
 **/
class FS3DerivedDataBackend : public FDerivedDataBackendInterface
{
public:
	/**
	 * Creates the backend, checks health status and attempts to acquire an access token.
	 *
	 * @param  InRootManifestPath   Local path to the JSON manifest in the workspace containing a list of files to download
	 * @param  InBaseUrl            Base URL for the bucket, with trailing slash (eg. https://foo.s3.us-east-1.amazonaws.com/)
	 * @param  InRegion	            Name of the AWS region (eg. us-east-1)
	 * @param  InCanaryObjectKey    Key for a canary object used to test whether this backend is usable
	 * @param  InCachePath          Path to cache the DDC files
	 */
	FS3DerivedDataBackend(ICacheFactory& Factory, const TCHAR* InRootManifestPath, const TCHAR* InBaseUrl, const TCHAR* InRegion, const TCHAR* InCanaryObjectKey, const TCHAR* InCachePath);
	~FS3DerivedDataBackend();

	/**
	 * Checks is backend is usable (reachable and accessible).
	 * @return true if usable
	 */
	bool IsUsable() const;

	/* S3 Cache cannot be written to*/
	bool IsWritable() const override { return false; }

	/* S3 Cache does not try to write back to lower caches (e.g. Shared DDC) */
	bool BackfillLowerCacheLevels() const override { return false; }

	bool CachedDataProbablyExists(const TCHAR* CacheKey) override;
	bool GetCachedData(const TCHAR* CacheKey, TArray<uint8>& OutData) override;
	EPutStatus PutCachedData(const TCHAR* CacheKey, TArrayView<const uint8> InData, bool bPutEvenIfExists) override;
	void RemoveCachedData(const TCHAR* CacheKey, bool bTransient) override;
	virtual TSharedRef<FDerivedDataCacheStatsNode> GatherUsageStats() const override;

	FString GetName() const override;
	ESpeedClass GetSpeedClass() const override;
	bool TryToPrefetch(TConstArrayView<FString> CacheKeys) override;
	bool WouldCache(const TCHAR* CacheKey, TArrayView<const uint8> InData) override;

	bool ApplyDebugOptions(FBackendDebugOptions& InOptions) override;

	virtual FRequest Put(
		TArrayView<FCacheRecord> Records,
		FStringView Context,
		ECachePolicy Policy,
		EPriority Priority,
		FOnCachePutComplete&& OnComplete) override;

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

	virtual void CancelAll() override
	{
	}

private:
	struct FBundle;
	struct FBundleEntry;
	struct FBundleDownload;

	struct FRootManifest;

	class FHttpRequest;
	class FRequestPool;

	ICacheFactory& Factory;
	FString RootManifestPath;
	FString BaseUrl;
	FString Region;
	FString CanaryObjectKey;
	FString CacheDir;
	TArray<FBundle> Bundles;
	TUniquePtr<FRequestPool> RequestPool;
	FDerivedDataCacheUsageStats UsageStats;
	bool bEnabled;

	bool DownloadManifest(const FRootManifest& RootManifest, FFeedbackContext* Context);
	void RemoveUnusedBundles();
	void ReadBundle(FBundle& Bundle);
	bool FindBundleEntry(const TCHAR* CacheKey, const FBundle*& OutBundle, const FBundleEntry*& OutBundleEntry) const;

	/* Debug helpers */
	bool DidSimulateMiss(const TCHAR* InKey);
	bool ShouldSimulateMiss(const TCHAR* InKey);

	/** Debug Options */
	FBackendDebugOptions DebugOptions;

	/** Keys we ignored due to miss rate settings */
	FCriticalSection MissedKeysCS;
	TSet<FName> DebugMissedKeys;
};

} // UE::DerivedData::Backends

#endif

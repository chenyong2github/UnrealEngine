// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataBackendInterface.h"
#include "ZenServerInterface.h"

#if UE_WITH_ZEN

#include "Containers/Set.h"
#include "Containers/StringFwd.h"
#include "Containers/StringFwd.h"
#include "DerivedDataCachePrivate.h"
#include "DerivedDataCacheRecord.h"
#include "DerivedDataCacheUsageStats.h"
#include "DerivedDataChunk.h"
#include "HAL/CriticalSection.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/ScopeLock.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/CompactBinary.h"
#include "Serialization/CompactBinaryPackage.h"
#include "Serialization/CompactBinarySerialization.h"
#include "Serialization/CompactBinaryValidation.h"
#include "Serialization/CompactBinaryWriter.h"
#include "ZenBackendUtils.h"
#include "ZenSerialization.h"
#include "ZenServerHttp.h"
#include "ZenStatistics.h"

TRACE_DECLARE_INT_COUNTER(ZenDDC_Exist,			TEXT("ZenDDC Exist"));
TRACE_DECLARE_INT_COUNTER(ZenDDC_ExistHit,		TEXT("ZenDDC Exist Hit"));
TRACE_DECLARE_INT_COUNTER(ZenDDC_Get,			TEXT("ZenDDC Get"));
TRACE_DECLARE_INT_COUNTER(ZenDDC_GetHit,		TEXT("ZenDDC Get Hit"));
TRACE_DECLARE_INT_COUNTER(ZenDDC_Put,			TEXT("ZenDDC Put"));
TRACE_DECLARE_INT_COUNTER(ZenDDC_PutHit,		TEXT("ZenDDC Put Hit"));
TRACE_DECLARE_INT_COUNTER(ZenDDC_BytesReceived, TEXT("ZenDDC Bytes Received"));
TRACE_DECLARE_INT_COUNTER(ZenDDC_BytesSent,		TEXT("ZenDDC Bytes Sent"));
TRACE_DECLARE_INT_COUNTER(ZenDDC_CacheRecordRequestCount, TEXT("ZenDDC CacheRecord Request Count"));
TRACE_DECLARE_INT_COUNTER(ZenDDC_ChunkRequestCount, TEXT("ZenDDC Chunk Request Count"));

namespace UE::DerivedData::CacheStore::ZenCache
{

template<typename T>
void ForEachBatch(const int32 BatchSize, const int32 TotalCount, T&& Fn)
{
	check(BatchSize > 0);

	if (TotalCount > 0)
	{
		const int32 BatchCount = FMath::DivideAndRoundUp(TotalCount, BatchSize);
		const int32 Last = TotalCount - 1;

		for (int32 BatchIndex = 0; BatchIndex < BatchCount; BatchIndex++)
		{
			const int32 BatchFirstIndex	= BatchIndex * BatchSize;
			const int32 BatchLastIndex	= FMath::Min(BatchFirstIndex + BatchSize - 1, Last);

			Fn(BatchFirstIndex, BatchLastIndex);
		}
	}
}

//----------------------------------------------------------------------------------------------------------
// FZenDerivedDataBackend
//----------------------------------------------------------------------------------------------------------

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

	virtual void PutValue(
		TConstArrayView<FCachePutValueRequest> Requests,
		IRequestOwner& Owner,
		FOnCachePutValueComplete&& OnComplete = FOnCachePutValueComplete()) override;

	virtual void GetValue(
		TConstArrayView<FCacheGetValueRequest> Requests,
		IRequestOwner& Owner,
		FOnCacheGetValueComplete&& OnComplete = FOnCacheGetValueComplete()) override;

	virtual void GetChunks(
		TConstArrayView<FCacheGetChunkRequest> Requests,
		IRequestOwner& Owner,
		FOnCacheGetChunkComplete&& OnComplete) override;

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
	bool PutCacheValue(const FCacheKey& Key, const FValue& Value, const ECachePolicy& ValuePolicy);

	bool IsServiceReady();
	static FString MakeLegacyZenKey(const TCHAR* CacheKey);
	static void AppendZenUri(const FCacheKey& CacheKey, FStringBuilderBase& Out);
	static void AppendZenUri(const FCacheKey& CacheKey, const FValueId& Id, FStringBuilderBase& Out);
	static void AppendPolicyQueryString(ECachePolicy Policy, FStringBuilderBase& Out);

	static bool ShouldRetryOnError(int64 ResponseCode);

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

FZenDerivedDataBackend::FZenDerivedDataBackend(
	const TCHAR* InServiceUrl,
	const TCHAR* InNamespace)
	: Namespace(InNamespace)
	, ZenService(InServiceUrl)
{
	if (IsServiceReady())
	{
		RequestPool = MakeUnique<Zen::FZenHttpRequestPool>(ZenService.GetInstance().GetURL(), 32);
		bIsUsable = true;
		bIsRemote = false;

		Zen::FZenStats ZenStats;

		if (ZenService.GetInstance().GetStats(ZenStats) == true)
		{
			 bIsRemote = ZenStats.UpstreamStats.EndPointStats.IsEmpty() == false;
		}
	}

	GConfig->GetInt(TEXT("Zen"), TEXT("CacheRecordBatchSize"), CacheRecordBatchSize, GEngineIni);
	GConfig->GetInt(TEXT("Zen"), TEXT("CacheChunksBatchSize"), CacheChunksBatchSize, GEngineIni);
}

FZenDerivedDataBackend::~FZenDerivedDataBackend()
{
}

FString FZenDerivedDataBackend::GetName() const
{
	return ZenService.GetInstance().GetURL();
}

bool FZenDerivedDataBackend::IsServiceReady()
{
	return ZenService.GetInstance().IsServiceReady();
}

bool FZenDerivedDataBackend::ShouldRetryOnError(int64 ResponseCode)
{
	// Access token might have expired, request a new token and try again.
	if (ResponseCode == 401)
	{
		return true;
	}

	// Too many requests, make a new attempt
	if (ResponseCode == 429)
	{
		return true;
	}

	return false;
}

bool FZenDerivedDataBackend::CachedDataProbablyExists(const TCHAR* CacheKey)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ZenDDC::Exist);
	TRACE_COUNTER_ADD(ZenDDC_Exist, int64(1));
	COOK_STAT(auto Timer = UsageStats.TimeProbablyExists());

	if (ShouldSimulateMiss(CacheKey))
	{
		return false;
	}
	
	FString Uri = MakeLegacyZenKey(CacheKey);
	int64 ResponseCode = 0; 
	uint32 Attempts = 0;

	// Retry request until we get an accepted response or exhaust allowed number of attempts.
	while (ResponseCode == 0 && ++Attempts < MaxAttempts)
	{
		Zen::FZenScopedRequestPtr Request(RequestPool.Get());
		Zen::FZenHttpRequest::Result Result = Request->PerformBlockingHead(*Uri, Zen::EContentType::Binary);
		ResponseCode = Request->GetResponseCode();

		if (Zen::IsSuccessCode(ResponseCode) || ResponseCode == 404)
		{
			const bool bIsHit = (Result == Zen::FZenHttpRequest::Result::Success && Zen::IsSuccessCode(ResponseCode));

			if (bIsHit)
			{
				TRACE_COUNTER_ADD(ZenDDC_ExistHit, int64(1));
				COOK_STAT(Timer.AddHit(0));
			}
			return bIsHit;
		}

		if (!ShouldRetryOnError(ResponseCode))
		{
			return false;
		}

		ResponseCode = 0;
	}

	return false;
}

bool FZenDerivedDataBackend::GetCachedData(const TCHAR* CacheKey, TArray<uint8>& OutData)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ZenDDC::GetCachedData);
	TRACE_COUNTER_ADD(ZenDDC_Get, int64(1));
	COOK_STAT(auto Timer = UsageStats.TimeGet());

	if (ShouldSimulateMiss(CacheKey))
	{
		return false;
	}

	double StartTime = FPlatformTime::Seconds();

	TArray64<uint8> ArrayBuffer;
	EGetResult Result = GetZenData(MakeLegacyZenKey(CacheKey), &ArrayBuffer, Zen::EContentType::Binary);
	check(ArrayBuffer.Num() <= UINT32_MAX);
	OutData = MoveTemp(ArrayBuffer);
	if (Result != EGetResult::Success)
	{
		switch (Result)
		{
		default:
			UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Cache miss on %s"), *GetName(), CacheKey);
			break;
		case EGetResult::Corrupted:
			UE_LOG(LogDerivedDataCache, Warning,
				TEXT("Checksum from server on %s did not match recieved data. Discarding cached result."), CacheKey);
			break;
		}
		return false;
	}

	TRACE_COUNTER_ADD(ZenDDC_GetHit, int64(1));
	TRACE_COUNTER_ADD(ZenDDC_BytesReceived, int64(OutData.Num()));
	COOK_STAT(Timer.AddHit(OutData.Num()));
	double ReadDuration = FPlatformTime::Seconds() - StartTime;
	double ReadSpeed = (OutData.Num() / ReadDuration) / (1024.0 * 1024.0);
	UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Cache hit on %s (%d bytes, %.02f secs, %.2fMB/s)"),
		*GetName(), CacheKey, OutData.Num(), ReadDuration, ReadSpeed);
	return true;
}

FZenDerivedDataBackend::EGetResult
FZenDerivedDataBackend::GetZenData(FStringView Uri, TArray64<uint8>* OutData, Zen::EContentType ContentType) const
{
	// Retry request until we get an accepted response or exhaust allowed number of attempts.
	EGetResult GetResult = EGetResult::NotFound;
	for (uint32 Attempts = 0; Attempts < MaxAttempts; ++Attempts)
	{
		Zen::FZenScopedRequestPtr Request(RequestPool.Get());
		if (Request.IsValid())
		{
			Zen::FZenHttpRequest::Result Result;
			if (OutData)
			{
				Result = Request->PerformBlockingDownload(Uri, OutData, ContentType);
			}
			else
			{
				Result = Request->PerformBlockingHead(Uri, ContentType);
			}
			int64 ResponseCode = Request->GetResponseCode();

			// Request was successful, make sure we got all the expected data.
			if (Zen::IsSuccessCode(ResponseCode))
			{
				return EGetResult::Success;
			}

			if (!ShouldRetryOnError(ResponseCode))
			{
				break;
			}
		}
	}

	if (OutData)
	{
		OutData->Reset();
	}

	return GetResult;
}

FZenDerivedDataBackend::EGetResult
FZenDerivedDataBackend::GetZenData(const FCacheKey& CacheKey, ECachePolicy CachePolicy, FCbPackage& OutPackage) const
{
	TStringBuilder<256> QueryUri;
	AppendZenUri(CacheKey, QueryUri);
	AppendPolicyQueryString(CachePolicy, QueryUri);

	// Retry request until we get an accepted response or exhaust allowed number of attempts.
	EGetResult GetResult = EGetResult::NotFound;
	for (uint32 Attempts = 0; Attempts < MaxAttempts; ++Attempts)
	{
		Zen::FZenScopedRequestPtr Request(RequestPool.Get());
		if (Request.IsValid())
		{
			Zen::FZenHttpRequest::Result Result = Request->PerformBlockingDownload(QueryUri.ToString(), OutPackage);
			int64 ResponseCode = Request->GetResponseCode();
			bool bPackageValid = Request->GetResponseFormatValid();

			// Request was successful, make sure we got all the expected data.
			if (Zen::IsSuccessCode(ResponseCode))
			{
				if (bPackageValid)
				{
					GetResult = EGetResult::Success;
				}
				else
				{
					GetResult = EGetResult::Corrupted;
				}
				break;
			}

			if (!ShouldRetryOnError(ResponseCode))
			{
				break;
			}
		}
	}

	if (GetResult != EGetResult::Success)
	{
		OutPackage.Reset();
	}
	return GetResult;
}

FDerivedDataBackendInterface::EPutStatus
FZenDerivedDataBackend::PutCachedData(const TCHAR* CacheKey, TArrayView<const uint8> InData, bool bPutEvenIfExists)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ZenDDC::PutCachedData);

	if (ShouldSimulateMiss(CacheKey))
	{
		return EPutStatus::NotCached;
	}

	FSharedBuffer DataBuffer = FSharedBuffer::MakeView(InData.GetData(), InData.Num());
	return PutZenData(*MakeLegacyZenKey(CacheKey), FCompositeBuffer(DataBuffer), Zen::EContentType::Binary);
}

FDerivedDataBackendInterface::EPutStatus
FZenDerivedDataBackend::PutZenData(const TCHAR* Uri, const FCompositeBuffer& InData, Zen::EContentType ContentType)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ZenDDC_Put);
	COOK_STAT(auto Timer = UsageStats.TimePut());

	int64 ResponseCode = 0; 
	uint32 Attempts = 0;

	// Retry request until we get an accepted response or exhaust allowed number of attempts.
	while (ResponseCode == 0 && ++Attempts < MaxAttempts)
	{
		Zen::FZenScopedRequestPtr Request(RequestPool.Get());
		if (Request.IsValid())
		{
			Zen::FZenHttpRequest::Result Result = Request->PerformBlockingPut(Uri, InData, ContentType);
			ResponseCode = Request->GetResponseCode();

			if (Zen::IsSuccessCode(ResponseCode))
			{
				TRACE_COUNTER_ADD(ZenDDC_BytesSent, int64(Request->GetBytesSent()));
				COOK_STAT(Timer.AddHit(Request->GetBytesSent()));
				return EPutStatus::Cached;
			}

			if (!ShouldRetryOnError(ResponseCode))
			{
				return EPutStatus::NotCached;
			}

			ResponseCode = 0;
		}
	}

	return EPutStatus::NotCached;
}

FString FZenDerivedDataBackend::MakeLegacyZenKey(const TCHAR* CacheKey)
{
	FIoHash KeyHash = FIoHash::HashBuffer(CacheKey, FCString::Strlen(CacheKey) * sizeof(TCHAR));
	return FString::Printf(TEXT("/z$/legacy/%s"), *LexToString(KeyHash));
}

void FZenDerivedDataBackend::AppendZenUri(const FCacheKey& CacheKey, FStringBuilderBase& Out)
{
	Out << TEXT("/z$/") << CacheKey.Bucket << TEXT('/') << CacheKey.Hash;
}

void FZenDerivedDataBackend::AppendZenUri(const FCacheKey& CacheKey, const FValueId& Id, FStringBuilderBase& Out)
{
	AppendZenUri(CacheKey, Out);
	Out << TEXT('/') << Id;
}

void FZenDerivedDataBackend::AppendPolicyQueryString(ECachePolicy Policy, FStringBuilderBase& Uri)
{
	if (Policy != ECachePolicy::Default)
	{
		Uri << TEXT("?Policy=") << Policy;
	}
}

void FZenDerivedDataBackend::RemoveCachedData(const TCHAR* CacheKey, bool bTransient)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ZenDDC_Remove);
	FString Uri = MakeLegacyZenKey(CacheKey);

	int64 ResponseCode = 0; 
	uint32 Attempts = 0;

	// Retry request until we get an accepted response or exhaust allowed number of attempts.
	while (ResponseCode == 0 && ++Attempts < MaxAttempts)
	{
		Zen::FZenScopedRequestPtr Request(RequestPool.Get());
		if (Request)
		{
			Zen::FZenHttpRequest::Result Result = Request->PerformBlockingDelete(*Uri);
			ResponseCode = Request->GetResponseCode();

			if (Zen::IsSuccessCode(ResponseCode))
			{
				return;
			}

			if (!ShouldRetryOnError(ResponseCode))
			{
				return;
			}

			ResponseCode = 0;
		}
	}
}

bool FZenDerivedDataBackend::IsWritable() const 
{
	return true;
}

FDerivedDataBackendInterface::ESpeedClass 
FZenDerivedDataBackend::GetSpeedClass() const
{
	return ESpeedClass::Fast;
}

TSharedRef<FDerivedDataCacheStatsNode> FZenDerivedDataBackend::GatherUsageStats() const
{
	Zen::FZenStats ZenStats;

	FDerivedDataCacheUsageStats LocalStats;
	FDerivedDataCacheUsageStats RemoteStats;

#if ENABLE_COOK_STATS
	using EHitOrMiss = FCookStats::CallStats::EHitOrMiss;
	using ECacheStatType = FCookStats::CallStats::EStatType;

	Zen::GetDefaultServiceInstance().GetStats(ZenStats);

	const int64 RemotePutSize = int64(ZenStats.UpstreamStats.TotalUploadedMB * 1024 * 1024);
	const int64 RemoteGetSize = int64(ZenStats.UpstreamStats.TotalDownloadedMB * 1024 * 1024);
	const int64 LocalGetSize = FMath::Max<int64>(0, UsageStats.GetStats.GetAccumulatedValueAnyThread(EHitOrMiss::Hit, ECacheStatType::Bytes) - RemoteGetSize);

	LocalStats.PutStats = UsageStats.PutStats;
	LocalStats.ExistsStats = UsageStats.ExistsStats;
	LocalStats.PrefetchStats = UsageStats.PrefetchStats;

	LocalStats.GetStats.Accumulate(EHitOrMiss::Hit, ECacheStatType::Counter, ZenStats.CacheStats.Hits - ZenStats.CacheStats.UpstreamHits, /*bIsInGameThread*/ false);
	LocalStats.GetStats.Accumulate(EHitOrMiss::Miss, ECacheStatType::Counter, ZenStats.CacheStats.Misses + ZenStats.CacheStats.UpstreamHits, /*bIsInGameThread*/ false);
	RemoteStats.GetStats.Accumulate(EHitOrMiss::Hit, ECacheStatType::Counter, ZenStats.CacheStats.UpstreamHits, /*bIsInGameThread*/ false);
	RemoteStats.GetStats.Accumulate(EHitOrMiss::Miss, ECacheStatType::Counter, ZenStats.CacheStats.Misses, /*bIsInGameThread*/ false);

	LocalStats.GetStats.Accumulate(EHitOrMiss::Hit, ECacheStatType::Bytes, LocalGetSize, /*bIsInGameThread*/ false);
	RemoteStats.GetStats.Accumulate(EHitOrMiss::Hit, ECacheStatType::Bytes, RemoteGetSize, /*bIsInGameThread*/ false);
	RemoteStats.PutStats.Accumulate(EHitOrMiss::Hit, ECacheStatType::Bytes, RemotePutSize, /*bIsInGameThread*/ false);
#endif

	TSharedRef<FDerivedDataCacheStatsNode> LocalNode =
		MakeShared<FDerivedDataCacheStatsNode>(TEXT("Zen"), ZenService.GetInstance().GetURL(), /*bIsLocal*/ true);
	LocalNode->Stats.Add(TEXT(""), LocalStats);

	if (ZenStats.UpstreamStats.EndPointStats.IsEmpty())
	{
		return LocalNode;
	}

	TSharedRef<FDerivedDataCacheStatsNode> RemoteNode =
		MakeShared<FDerivedDataCacheStatsNode>(ZenStats.UpstreamStats.EndPointStats[0].Name, ZenStats.UpstreamStats.EndPointStats[0].Url, /*bIsLocal*/ false);
	RemoteNode->Stats.Add(TEXT(""), RemoteStats);

	TSharedRef<FDerivedDataCacheStatsNode> GroupNode =
		MakeShared<FDerivedDataCacheStatsNode>(TEXT("Zen Group"), TEXT(""), /*bIsLocal*/ true);
	GroupNode->Children.Add(LocalNode);
	GroupNode->Children.Add(RemoteNode);
	return GroupNode;
}

bool FZenDerivedDataBackend::TryToPrefetch(TConstArrayView<FString> CacheKeys)
{
	return CachedDataProbablyExistsBatch(CacheKeys).CountSetBits() == CacheKeys.Num();
}

bool FZenDerivedDataBackend::WouldCache(const TCHAR* CacheKey, TArrayView<const uint8> InData)
{
	return true;
}

bool FZenDerivedDataBackend::ApplyDebugOptions(FBackendDebugOptions& InOptions)
{
	DebugOptions = InOptions;
	return true;
}

bool FZenDerivedDataBackend::ShouldSimulateMiss(const TCHAR* InKey)
{
	if (DebugOptions.RandomMissRate == 0 && DebugOptions.SimulateMissTypes.IsEmpty())
	{
		return false;
	}

	const FName Key(InKey);
	const uint32 Hash = GetTypeHash(Key);

	if (FScopeLock Lock(&MissedKeysCS); DebugMissedKeys.ContainsByHash(Hash, Key))
	{
		return true;
	}

	if (DebugOptions.ShouldSimulateMiss(InKey))
	{
		FScopeLock Lock(&MissedKeysCS);
		UE_LOG(LogDerivedDataCache, Verbose, TEXT("Simulating miss in %s for %s"), *GetName(), InKey);
		DebugMissedKeys.AddByHash(Hash, Key);
		return true;
	}

	return false;
}

bool FZenDerivedDataBackend::ShouldSimulateMiss(const FCacheKey& Key)
{
	if (DebugOptions.RandomMissRate == 0 && DebugOptions.SimulateMissTypes.IsEmpty())
	{
		return false;
	}

	const uint32 Hash = GetTypeHash(Key);

	if (FScopeLock Lock(&MissedKeysCS); DebugMissedCacheKeys.ContainsByHash(Hash, Key))
	{
		return true;
	}

	if (DebugOptions.ShouldSimulateMiss(Key))
	{
		FScopeLock Lock(&MissedKeysCS);
		DebugMissedCacheKeys.AddByHash(Hash, Key);
		return true;
	}

	return false;
}

void FZenDerivedDataBackend::Put(
	const TConstArrayView<FCachePutRequest> Requests,
	IRequestOwner& Owner,
	FOnCachePutComplete&& OnComplete)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ZenDDC::PutCachedRecord);

	for (const FCachePutRequest& Request : Requests)
	{
		COOK_STAT(auto Timer = UsageStats.TimePut());
		const FCacheRecord& Record = Request.Record;
		bool bResult;
		if (ShouldSimulateMiss(Record.GetKey()))
		{
			UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Simulated miss for put of %s from '%s'"),
				*GetName(), *WriteToString<96>(Record.GetKey()), *Request.Name);
			bResult = false;
		}
		else
		{
			bResult = PutCacheRecord(Record, Request.Policy);
		}

		if (bResult)
		{
			UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Cache put complete for %s from '%s'"),
				*GetName(), *WriteToString<96>(Record.GetKey()), *Request.Name);
			COOK_STAT(Timer.AddHit(Private::GetCacheRecordCompressedSize(Record)));
			if (OnComplete)
			{
				OnComplete({ Request.Name, Record.GetKey(), Request.UserData, EStatus::Ok });
			}
		}
		else
		{
			COOK_STAT(Timer.AddMiss());
			if (OnComplete)
			{
				OnComplete({ Request.Name, Record.GetKey(), Request.UserData, EStatus::Error });
			}
		}
	}
}

void FZenDerivedDataBackend::Get(
	const TConstArrayView<FCacheGetRequest> Requests,
	IRequestOwner& Owner,
	FOnCacheGetComplete&& OnComplete)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ZenDDC::GetCacheRecord);
	TRACE_COUNTER_ADD(ZenDDC_CacheRecordRequestCount, int64(Requests.Num()));

	int32 TotalCompleted = 0;

	ForEachBatch(CacheRecordBatchSize, Requests.Num(),
		[this, &Requests, &Owner, &OnComplete, &TotalCompleted](int32 BatchFirst, int32 BatchLast)
	{
		TRACE_COUNTER_ADD(ZenDDC_Get, int64(BatchLast) - int64(BatchFirst) + 1);
		COOK_STAT(auto Timer = UsageStats.TimeGet());
		
		FCbWriter BatchRequest;
		BatchRequest.BeginObject();
		{
			BatchRequest << "Method"_ASV << "GetCacheRecords";
			BatchRequest.BeginObject("Params"_ASV);
			{
				BatchRequest.BeginArray("CacheKeys"_ASV);
				for (int32 KeyIndex = BatchFirst; KeyIndex <= BatchLast; KeyIndex++)
				{
					const FCacheKey& Key = Requests[KeyIndex].Key;

					BatchRequest.BeginObject();
					BatchRequest << "Bucket"_ASV << Key.Bucket.ToString();
					BatchRequest << "Hash"_ASV << Key.Hash;
					BatchRequest.EndObject();
				}
				BatchRequest.EndArray();

				// TODO: The policy needs to be sent with each key.
				BatchRequest.SetName("Policy"_ASV);
				Requests[BatchFirst].Policy.Save(BatchRequest);
			}
			BatchRequest.EndObject();
		}
		BatchRequest.EndObject();

		FCbPackage BatchResponse;
		Zen::FZenHttpRequest::Result HttpResult = Zen::FZenHttpRequest::Result::Failed;

		{
			Zen::FZenScopedRequestPtr Request(RequestPool.Get());
			HttpResult = Request->PerformRpc(TEXT("/z$/$rpc"_SV), BatchRequest.Save().AsObject(), BatchResponse);
		}

		if (HttpResult == Zen::FZenHttpRequest::Result::Success)
		{
			const FCbObject& ResponseObj = BatchResponse.GetObject();
			
			int32 KeyIndex = BatchFirst;
			for (FCbField RecordField : ResponseObj["Result"_ASV])
			{
				const FCacheGetRequest& Request = Requests[KeyIndex++];
				const FCacheKey& Key = Request.Key;
				
				FOptionalCacheRecord Record;

				if (!RecordField.IsNull())
				{
					if (ShouldSimulateMiss(Key))
					{
						UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Simulated miss for put of '%s' from '%s'"),
							*GetName(), *WriteToString<96>(Key), *Request.Name);
					}
					else
					{
						Record = FCacheRecord::Load(BatchResponse, RecordField.AsObject());
					}
				}

				if (Record)
				{
					UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Cache hit for '%s' from '%s'"),
						*GetName(), *WriteToString<96>(Key), *Request.Name);
					
					TRACE_COUNTER_ADD(ZenDDC_GetHit, int64(1));
					int64 ReceivedSize = Private::GetCacheRecordCompressedSize(Record.Get());
					TRACE_COUNTER_ADD(ZenDDC_BytesReceived, ReceivedSize);
					COOK_STAT(Timer.AddHit(ReceivedSize));

					if (OnComplete)
					{
						OnComplete({ Request.Name, MoveTemp(Record).Get(), Request.UserData, EStatus::Ok });
					}
				}
				else
				{
					UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Cache miss for '%s' from '%s'"),
						*GetName(), *WriteToString<96>(Key), *Request.Name);
					
					if (OnComplete)
					{
						OnComplete({ Request.Name, FCacheRecordBuilder(Key).Build(), Request.UserData, EStatus::Error });
					}
				}
				
				TotalCompleted++;
			}
		}
		else
		{
			for (int32 KeyIndex = BatchFirst; KeyIndex <= BatchLast; KeyIndex++)
			{
				const FCacheGetRequest& Request = Requests[KeyIndex];
				const FCacheKey& Key = Request.Key;

				UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Cache miss for '%s' from '%s'"),
					*GetName(), *WriteToString<96>(Key), *Request.Name);
					
				if (OnComplete)
				{
					OnComplete({ Request.Name, FCacheRecordBuilder(Key).Build(), Request.UserData, EStatus::Error });
				}
				
				TotalCompleted++;
			}
		}
	});
	
	UE_CLOG(TotalCompleted != Requests.Num(), LogDerivedDataCache, Warning, TEXT("Only '%d/%d' cache record request(s) completed"), TotalCompleted, Requests.Num());
	TRACE_COUNTER_SUBTRACT(ZenDDC_CacheRecordRequestCount, int64(Requests.Num()));
}

void FZenDerivedDataBackend::PutValue(
	TConstArrayView<FCachePutValueRequest> Requests,
	IRequestOwner& Owner,
	FOnCachePutValueComplete&& OnComplete)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ZenDDC::PutValue);

	for (const FCachePutValueRequest& Request : Requests)
	{
		COOK_STAT(auto Timer = UsageStats.TimePut());
		bool bResult;
		if (ShouldSimulateMiss(Request.Key))
		{
			UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Simulated miss for PutValue of %s from '%s'"),
				*GetName(), *WriteToString<96>(Request.Key), *Request.Name);
			bResult = false;
		}
		else
		{
			bResult = PutCacheValue(Request.Key, Request.Value, Request.Policy);
		}

		if (bResult)
		{
			UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Cache PutValue complete for %s from '%s'"),
				*GetName(), *WriteToString<96>(Request.Key), *Request.Name);
			COOK_STAT(Timer.AddHit(Request.Value.GetData().GetCompressedSize()));
			if (OnComplete)
			{
				OnComplete({ Request.Name, Request.Key, Request.UserData, EStatus::Ok });
			}
		}
		else
		{
			COOK_STAT(Timer.AddMiss());
			if (OnComplete)
			{
				OnComplete({ Request.Name, Request.Key, Request.UserData, EStatus::Error });
			}
		}
	}
}

void FZenDerivedDataBackend::GetValue(
	TConstArrayView<FCacheGetValueRequest> Requests,
	IRequestOwner& Owner,
	FOnCacheGetValueComplete&& OnComplete)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ZenDDC::GetValue);
	TRACE_COUNTER_ADD(ZenDDC_CacheRecordRequestCount, int64(Requests.Num()));

	int32 TotalCompleted = 0;

	ForEachBatch(CacheRecordBatchSize, Requests.Num(),
		[this, &Requests, &Owner, &OnComplete, &TotalCompleted](int32 BatchFirst, int32 BatchLast)
		{
			TRACE_COUNTER_ADD(ZenDDC_Get, int64(BatchLast) - int64(BatchFirst) + 1);
			COOK_STAT(auto Timer = UsageStats.TimeGet());

			FCbWriter BatchRequest;
			BatchRequest.BeginObject();
			{
				BatchRequest << "Method"_ASV << "GetCacheRecords";
				BatchRequest.BeginObject("Params"_ASV);
				{
					BatchRequest.BeginArray("CacheKeys"_ASV);
					for (int32 KeyIndex = BatchFirst; KeyIndex <= BatchLast; KeyIndex++)
					{
						const FCacheKey& Key = Requests[KeyIndex].Key;

						BatchRequest.BeginObject();
						BatchRequest << "Bucket"_ASV << Key.Bucket.ToString();
						BatchRequest << "Hash"_ASV << Key.Hash;
						BatchRequest.EndObject();
					}
					BatchRequest.EndArray();

					// TODO: The policy needs to be sent with each key.
					const ECachePolicy& Policy = Requests[BatchFirst].Policy;
					BatchRequest.BeginObject("Policy"_ASV);
					{
						BatchRequest << "RecordPolicy"_ASV << static_cast<uint32>(Policy);
					}
					BatchRequest.EndObject();
				}
				BatchRequest.EndObject();
			}
			BatchRequest.EndObject();

			FCbPackage BatchResponse;
			Zen::FZenHttpRequest::Result HttpResult = Zen::FZenHttpRequest::Result::Failed;

			{
				Zen::FZenScopedRequestPtr Request(RequestPool.Get());
				HttpResult = Request->PerformRpc(TEXT("/z$/$rpc"_SV), BatchRequest.Save().AsObject(), BatchResponse);
			}

			if (HttpResult == Zen::FZenHttpRequest::Result::Success)
			{
				const FCbObject& ResponseObj = BatchResponse.GetObject();

				int32 KeyIndex = BatchFirst;
				for (FCbField ValueReferenceField : ResponseObj["Result"_ASV])
				{
					const FCacheGetValueRequest& Request = Requests[KeyIndex++];
					const FCacheKey& Key = Request.Key;
					FIoHash RawHash = ValueReferenceField["RawHash"_ASV].AsBinaryAttachment();
					TOptional<FValue> ResultValue;

					if (!RawHash.IsZero())
					{
						if (ShouldSimulateMiss(Key))
						{
							UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Simulated miss for put of '%s' from '%s'"),
								*GetName(), *WriteToString<96>(Key), *Request.Name);
						}
						else if (EnumHasAnyFlags(Request.Policy, UE::DerivedData::ECachePolicy::SkipData))
						{
							ResultValue.Emplace(RawHash, ValueReferenceField["RawSize"].AsUInt64());
						}
						else
						{
							const FCbAttachment* Attachment = BatchResponse.FindAttachment(RawHash);
							if (Attachment)
							{
								ResultValue.Emplace(Attachment->AsCompressedBinary());
							}
						}
					}

					if (ResultValue)
					{
						UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Cache hit for '%s' from '%s'"),
							*GetName(), *WriteToString<96>(Key), *Request.Name);

						TRACE_COUNTER_ADD(ZenDDC_GetHit, int64(1));
						int64 ReceivedSize = ResultValue->GetData().GetCompressedSize();
						TRACE_COUNTER_ADD(ZenDDC_BytesReceived, ReceivedSize);
						COOK_STAT(Timer.AddHit(ReceivedSize));

						if (OnComplete)
						{
							OnComplete({ Request.Name, Key, MoveTemp(*ResultValue), Request.UserData, EStatus::Ok });
						}
					}
					else
					{
						UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Cache miss for '%s' from '%s'"),
							*GetName(), *WriteToString<96>(Key), *Request.Name);

						if (OnComplete)
						{
							OnComplete({ Request.Name, Key, {}, Request.UserData, EStatus::Error });
						}
					}

					TotalCompleted++;
				}
			}
			else
			{
				for (int32 KeyIndex = BatchFirst; KeyIndex <= BatchLast; KeyIndex++)
				{
					const FCacheGetValueRequest& Request = Requests[KeyIndex];
					const FCacheKey& Key = Request.Key;

					UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Cache miss for '%s' from '%s'"),
						*GetName(), *WriteToString<96>(Key), *Request.Name);

					if (OnComplete)
					{
						OnComplete({ Request.Name, Key, {}, Request.UserData, EStatus::Error });
					}

					TotalCompleted++;
				}
			}
		});

	UE_CLOG(TotalCompleted != Requests.Num(), LogDerivedDataCache, Warning, TEXT("Only '%d/%d' GetValue request(s) completed"), TotalCompleted, Requests.Num());
	TRACE_COUNTER_SUBTRACT(ZenDDC_CacheRecordRequestCount, int64(Requests.Num()));
}


void FZenDerivedDataBackend::GetChunks(
	TConstArrayView<FCacheGetChunkRequest> Requests,
	IRequestOwner& Owner,
	FOnCacheGetChunkComplete&& OnComplete)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ZenDDC::GetChunks);
	TRACE_COUNTER_ADD(ZenDDC_ChunkRequestCount, int64(Requests.Num()));

	TArray<FCacheGetChunkRequest, TInlineAllocator<16>> SortedRequests(Requests);
	SortedRequests.StableSort(TChunkLess());

	int32 TotalCompleted = 0;

	ForEachBatch(CacheChunksBatchSize, SortedRequests.Num(),
		[this, &SortedRequests, &Owner, &OnComplete, &TotalCompleted](int32 BatchFirst, int32 BatchLast)
	{
		TRACE_COUNTER_ADD(ZenDDC_Get, int64(BatchLast) - int64(BatchFirst) + 1);
		COOK_STAT(auto Timer = UsageStats.TimeGet());

		FCbWriter BatchRequest;
		BatchRequest.BeginObject();
		{
			BatchRequest << "Method"_ASV << "GetCacheValues";
			BatchRequest.BeginObject("Params"_ASV);
			{
				BatchRequest.BeginArray("ChunkRequests"_ASV);
				for (int32 ChunkIndex = BatchFirst; ChunkIndex <= BatchLast; ChunkIndex++)
				{
					const FCacheGetChunkRequest& Request = SortedRequests[ChunkIndex];
					
					BatchRequest.BeginObject();
					
					BatchRequest.BeginObject("Key"_ASV);
					BatchRequest << "Bucket"_ASV << Request.Key.Bucket.ToString();
					BatchRequest << "Hash"_ASV << Request.Key.Hash;
					BatchRequest.EndObject();

					BatchRequest.AddObjectId("ValueId"_ASV, Request.Id);
					BatchRequest << "RawOffset"_ASV << Request.RawOffset;
					BatchRequest << "RawSize"_ASV << Request.RawSize;
					BatchRequest << "Policy"_ASV << static_cast<uint32>(Request.Policy);

					BatchRequest.EndObject();
				}
				BatchRequest.EndArray();
			}
			BatchRequest.EndObject();
		}
		BatchRequest.EndObject();

		FCbPackage BatchResponse;
		Zen::FZenHttpRequest::Result HttpResult = Zen::FZenHttpRequest::Result::Failed;

		{
			Zen::FZenScopedRequestPtr Request(RequestPool.Get());
			HttpResult = Request->PerformRpc(TEXT("/z$/$rpc"_SV), BatchRequest.Save().AsObject(), BatchResponse);
		}

		if (HttpResult == Zen::FZenHttpRequest::Result::Success)
		{
			const FCbObject& ResponseObj = BatchResponse.GetObject();

			int32 ChunkIndex = BatchFirst;
			for (FCbFieldView HashView : ResponseObj["Result"_ASV])
			{
				const FCacheGetChunkRequest& Request = SortedRequests[ChunkIndex++];

				if (ShouldSimulateMiss(Request.Key))
				{
					UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Simulated miss for get of '%s' from '%s'"),
						*GetName(), *WriteToString<96>(Request.Key ,'/', Request.Id), *Request.Name);
					
					if (OnComplete)
					{
						OnComplete({ Request.Name, Request.Key, Request.Id, Request.RawOffset, 0, {}, {}, Request.UserData, EStatus::Error });
					}
				}
				else if (const FCbAttachment* Attachment = BatchResponse.FindAttachment(HashView.AsHash()))
				{
					const FCompressedBuffer& CompressedBuffer = Attachment->AsCompressedBinary();
					FSharedBuffer Buffer = FCompressedBufferReader(CompressedBuffer).Decompress(Request.RawOffset, Request.RawSize);
					
					UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Cache hit for '%s' from '%s'"),
						*GetName(), *WriteToString<96>(Request.Key, '/', Request.Id), *Request.Name);

					const uint64 RawSize = Buffer.GetSize();
					TRACE_COUNTER_ADD(ZenDDC_GetHit, int64(1));
					TRACE_COUNTER_ADD(ZenDDC_BytesReceived, RawSize);
					COOK_STAT(Timer.AddHit(RawSize));
					
					if (OnComplete)
					{
						OnComplete({ Request.Name, Request.Key, Request.Id, Request.RawOffset, RawSize, CompressedBuffer.GetRawHash(), MoveTemp(Buffer), Request.UserData, EStatus::Ok });
					}
				}
				else
				{
					UE_LOG(LogDerivedDataCache, Display, TEXT("%s: Cache miss with missing value '%s' for '%s' from '%s'"),
						*GetName(), *WriteToString<16>(Request.Id), *WriteToString<96>(Request.Key), *Request.Name);
					
					if (OnComplete)
					{
						OnComplete({ Request.Name, Request.Key, Request.Id, Request.RawOffset, 0, {}, {}, Request.UserData, EStatus::Error });
					}
				}

				TotalCompleted++;
			}
		}
		else
		{
			for (int32 ChunkIndex = BatchFirst; ChunkIndex <= BatchLast; ChunkIndex++)
			{
				const FCacheGetChunkRequest& Request = SortedRequests[ChunkIndex];
				
				UE_LOG(LogDerivedDataCache, Display, TEXT("%s: Cache miss with missing value '%s' for '%s' from '%s'"),
					*GetName(), *WriteToString<16>(Request.Id), *WriteToString<96>(Request.Key), *Request.Name);
				
				if (OnComplete)
				{
					OnComplete({ Request.Name, Request.Key, Request.Id, Request.RawOffset, 0, {}, {}, Request.UserData, EStatus::Error });
				}
				
				TotalCompleted++;
			}
		}
	});

	UE_CLOG(TotalCompleted != SortedRequests.Num(), LogDerivedDataCache, Warning, TEXT("Only '%d/%d' cache chunk request(s) completed"), TotalCompleted, SortedRequests.Num());
	TRACE_COUNTER_SUBTRACT(ZenDDC_ChunkRequestCount, int64(Requests.Num()));
}

bool FZenDerivedDataBackend::PutCacheRecord(const FCacheRecord& Record, const FCacheRecordPolicy& Policy)
{
	const FCacheKey& Key = Record.GetKey();
	FCbPackage Package = Record.Save();
	FBufferArchive Ar;
	Package.Save(Ar);
	FCompositeBuffer Buffer = FCompositeBuffer(FSharedBuffer::MakeView(Ar.GetData(), Ar.Num()));
	TStringBuilder<256> Uri;
	AppendZenUri(Record.GetKey(), Uri);
	AppendPolicyQueryString(Policy.GetRecordPolicy(), Uri);
	if (PutZenData(Uri.ToString(), Buffer, Zen::EContentType::CbPackage)
		!= FDerivedDataBackendInterface::EPutStatus::Cached)
	{
		return false;
	}

	return true;
}

bool FZenDerivedDataBackend::PutCacheValue(const FCacheKey& Key, const FValue& Value, const ECachePolicy& ValuePolicy)
{
	FCbWriter Writer;
	Writer.BeginObject();
	Writer.AddBinaryAttachment("RawHash", Value.GetRawHash());
	Writer.AddInteger("RawSize", Value.GetRawSize());
	Writer.EndObject();

	FCbPackage Package(Writer.Save().AsObject());
	if (Value.HasData())
	{
		Package.AddAttachment(FCbAttachment(Value.GetData()));
	}

	FBufferArchive Ar;
	Package.Save(Ar);
	FCompositeBuffer Buffer = FCompositeBuffer(FSharedBuffer::MakeView(Ar.GetData(), Ar.Num()));
	TStringBuilder<256> Uri;
	AppendZenUri(Key, Uri);
	AppendPolicyQueryString(ValuePolicy, Uri);
	if (PutZenData(Uri.ToString(), Buffer, Zen::EContentType::CbPackage)
		!= FDerivedDataBackendInterface::EPutStatus::Cached)
	{
		return false;
	}

	return true;
}

FDerivedDataBackendInterface* CreateZenDerivedDataBackend(const TCHAR* NodeName, const TCHAR* ServiceUrl, const TCHAR* Namespace)
{
	FZenDerivedDataBackend* Backend = new FZenDerivedDataBackend(ServiceUrl, Namespace);
	if (Backend->IsUsable())
	{
		return Backend;
	}
	UE_LOG(LogDerivedDataCache, Warning, TEXT("%s could not contact the service (%s), will not use it."), NodeName, *Backend->GetName());
	delete Backend;
	return nullptr;
}

} // namespace UE::DerivedData::CacheStore::ZenCache

#else

namespace UE::DerivedData::CacheStore::ZenCache
{

FDerivedDataBackendInterface* CreateZenDerivedDataBackend(const TCHAR* NodeName, const TCHAR* ServiceUrl, const TCHAR* Namespace)
{
	return nullptr;
}

} // UE::DerivedData::CacheStore::ZenCache

#endif // UE_WITH_ZEN

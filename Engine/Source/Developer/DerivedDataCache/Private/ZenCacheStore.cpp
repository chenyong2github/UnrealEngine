// Copyright Epic Games, Inc. All Rights Reserved.

#include "ZenServerInterface.h"

#if UE_WITH_ZEN

#include "BatchView.h"
#include "DerivedDataBackendInterface.h"
#include "DerivedDataCachePrivate.h"
#include "DerivedDataCacheRecord.h"
#include "DerivedDataCacheUsageStats.h"
#include "DerivedDataChunk.h"
#include "DerivedDataLegacyCacheStore.h"
#include "Http/HttpClient.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Optional.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Serialization/CompactBinary.h"
#include "Serialization/CompactBinaryPackage.h"
#include "Serialization/CompactBinarySerialization.h"
#include "Serialization/CompactBinaryValidation.h"
#include "Serialization/CompactBinaryWriter.h"
#include "Serialization/LargeMemoryReader.h"
#include "Serialization/LargeMemoryWriter.h"
#include "Templates/Function.h"
#include "ZenBackendUtils.h"
#include "ZenSerialization.h"
#include "ZenStatistics.h"

TRACE_DECLARE_INT_COUNTER(ZenDDC_Get,			TEXT("ZenDDC Get"));
TRACE_DECLARE_INT_COUNTER(ZenDDC_GetHit,		TEXT("ZenDDC Get Hit"));
TRACE_DECLARE_INT_COUNTER(ZenDDC_Put,			TEXT("ZenDDC Put"));
TRACE_DECLARE_INT_COUNTER(ZenDDC_PutHit,		TEXT("ZenDDC Put Hit"));
TRACE_DECLARE_INT_COUNTER(ZenDDC_BytesReceived, TEXT("ZenDDC Bytes Received"));
TRACE_DECLARE_INT_COUNTER(ZenDDC_BytesSent,		TEXT("ZenDDC Bytes Sent"));
TRACE_DECLARE_INT_COUNTER(ZenDDC_CacheRecordRequestCount, TEXT("ZenDDC CacheRecord Request Count"));
TRACE_DECLARE_INT_COUNTER(ZenDDC_ChunkRequestCount, TEXT("ZenDDC Chunk Request Count"));

namespace UE::DerivedData
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
// FZenCacheStore
//----------------------------------------------------------------------------------------------------------

/**
 * Backend for a HTTP based caching service (Zen)
 */
class FZenCacheStore final : public ILegacyCacheStore
{
public:
	
	/**
	 * Creates the cache store client, checks health status and attempts to acquire an access token.
	 *
	 * @param ServiceUrl	Base url to the service including schema.
	 * @param Namespace		Namespace to use.
	 */
	FZenCacheStore(const TCHAR* ServiceUrl, const TCHAR* Namespace, const TCHAR* StructuredNamespace);

	inline FString GetName() const { return ZenService.GetInstance().GetURL(); }

	/**
	 * Checks if cache service is usable (reachable and accessible).
	 * @return true if usable
	 */
	inline bool IsUsable() const { return bIsUsable; }

	// ICacheStore

	void Put(
		TConstArrayView<FCachePutRequest> Requests,
		IRequestOwner& Owner,
		FOnCachePutComplete&& OnComplete = FOnCachePutComplete()) final;

	void Get(
		TConstArrayView<FCacheGetRequest> Requests,
		IRequestOwner& Owner,
		FOnCacheGetComplete&& OnComplete) final;

	void PutValue(
		TConstArrayView<FCachePutValueRequest> Requests,
		IRequestOwner& Owner,
		FOnCachePutValueComplete&& OnComplete = FOnCachePutValueComplete()) final;

	void GetValue(
		TConstArrayView<FCacheGetValueRequest> Requests,
		IRequestOwner& Owner,
		FOnCacheGetValueComplete&& OnComplete = FOnCacheGetValueComplete()) final;

	void GetChunks(
		TConstArrayView<FCacheGetChunkRequest> Requests,
		IRequestOwner& Owner,
		FOnCacheGetChunkComplete&& OnComplete) final;

	// ILegacyCacheStore

	void LegacyStats(FDerivedDataCacheStatsNode& OutNode) final;
	bool LegacyDebugOptions(FBackendDebugOptions& Options) final;

private:
	bool IsServiceReady();

	using FOnRpcComplete = TUniqueFunction<void(FHttpRequest::EResult HttpResult, FHttpRequest* Request, FCbPackage& OutResponse)>;
	static FHttpRequest::EResult ParseRpcResponse(FHttpRequest::EResult ResultFromPost, FHttpRequest& Request, FCbPackage& OutResponse);
	static FHttpRequest::EResult PerformBlockingRpc(FHttpRequest& Request,
		FStringView Uri,
		FCbObject RequestObject,
		FCbPackage &OutResponse);
	static FHttpRequest::EResult PerformBlockingRpc(FHttpRequest& Request,
		FStringView Uri,
		const FCbPackage& RequestPackage,
		FCbPackage& OutResponse);
	static void EnqueueAsyncRpc(FHttpRequest& Request,
		UE::DerivedData::IRequestOwner& Owner,
		FHttpRequestPool* Pool,
		FStringView Uri,
		FCbObject RequestObject,
		FOnRpcComplete&& OnComplete);
	static void EnqueueAsyncRpc(FHttpRequest& Request,
		UE::DerivedData::IRequestOwner& Owner,
		FHttpRequestPool* Pool,
		FStringView Uri,
		const FCbPackage& RequestPackage,
		FOnRpcComplete&& OnComplete);

private:
	FString Namespace;
	FString StructuredNamespace;
	UE::Zen::FScopeZenService ZenService;
	mutable FDerivedDataCacheUsageStats UsageStats;
	TUniquePtr<FHttpRequestPool> RequestPool;
	bool bIsUsable = false;
	int32 BatchPutMaxBytes = 1024*1024;
	int32 CacheRecordBatchSize = 8;
	int32 CacheChunksBatchSize = 8;

	/** Debug Options */
	FBackendDebugOptions DebugOptions;
};

FZenCacheStore::FZenCacheStore(
	const TCHAR* InServiceUrl,
	const TCHAR* InNamespace,
	const TCHAR* InStructuredNamespace)
	: Namespace(InNamespace)
	, StructuredNamespace(InStructuredNamespace)
	, ZenService(InServiceUrl)
{
	if (IsServiceReady())
	{
		RequestPool = MakeUnique<FHttpRequestPool>(ZenService.GetInstance().GetURL(), ZenService.GetInstance().GetURL(), nullptr, nullptr, 32);
		bIsUsable = true;

		// Issue a request for stats as it will be fetched asynchronously and issuing now makes them available sooner for future callers.
		Zen::FZenStats ZenStats;
		ZenService.GetInstance().GetStats(ZenStats);
	}

	GConfig->GetInt(TEXT("Zen"), TEXT("BatchPutMaxBytes"), BatchPutMaxBytes, GEngineIni);
	GConfig->GetInt(TEXT("Zen"), TEXT("CacheRecordBatchSize"), CacheRecordBatchSize, GEngineIni);
	GConfig->GetInt(TEXT("Zen"), TEXT("CacheChunksBatchSize"), CacheChunksBatchSize, GEngineIni);
}

bool FZenCacheStore::IsServiceReady()
{
	return ZenService.GetInstance().IsServiceReady();
}

FHttpRequest::EResult FZenCacheStore::ParseRpcResponse(FHttpRequest::EResult ResultFromPost, FHttpRequest& Request, FCbPackage& OutResponse)
{
	if (ResultFromPost != FHttpRequest::EResult::Success || !FHttpRequest::IsSuccessResponse(Request.GetResponseCode()))
	{
		return FHttpRequest::EResult::Failed;
	}

	const TArray64<uint8>& ResponseBuffer = Request.GetResponseBuffer();
	if (ResponseBuffer.Num())
	{
		FLargeMemoryReader Ar(ResponseBuffer.GetData(), ResponseBuffer.Num());
		if (!OutResponse.TryLoad(Ar))
		{
			return FHttpRequest::EResult::Failed;
		}
	}

	return FHttpRequest::EResult::Success;
}

FHttpRequest::EResult FZenCacheStore::PerformBlockingRpc(FHttpRequest& Request,
	FStringView Uri,
	FCbObject RequestObject,
	FCbPackage& OutResponse)
{
	return ParseRpcResponse(
		Request.PerformBlockingPost(Uri, RequestObject.GetBuffer(), EHttpContentType::CbObject, EHttpContentType::CbPackage), Request, OutResponse);
}

FHttpRequest::EResult FZenCacheStore::PerformBlockingRpc(FHttpRequest& Request,
	FStringView Uri,
	const FCbPackage& RequestPackage,
	FCbPackage& OutResponse)
{
	FLargeMemoryWriter PackageMemory;
	UE::Zen::Http::SaveCbPackage(RequestPackage, PackageMemory);

	return ParseRpcResponse(
		Request.PerformBlockingPost(Uri, FCompositeBuffer(FSharedBuffer::MakeView(PackageMemory.GetView())), EHttpContentType::CbPackage, EHttpContentType::CbPackage), Request, OutResponse);
}

void FZenCacheStore::EnqueueAsyncRpc(FHttpRequest& Request,
	UE::DerivedData::IRequestOwner& Owner,
	FHttpRequestPool* Pool,
	FStringView Uri,
	FCbObject RequestObject,
	FOnRpcComplete&& OnComplete)
{
	auto OnHttpRequestComplete = [OnComplete = MoveTemp(OnComplete)]
	(FHttpRequest::EResult HttpResult, FHttpRequest* Request)
	{
		FCbPackage Response;
		FHttpRequest::EResult ParsedResponseResult = ParseRpcResponse(HttpResult, *Request, Response);
		OnComplete(ParsedResponseResult, Request, Response);
		return FHttpRequest::ECompletionBehavior::Done;
	};
	Request.EnqueueAsyncPost(Owner, Pool, Uri, RequestObject.GetBuffer(), MoveTemp(OnHttpRequestComplete), EHttpContentType::CbObject, EHttpContentType::CbPackage);
}

void FZenCacheStore::EnqueueAsyncRpc(FHttpRequest& Request,
	UE::DerivedData::IRequestOwner& Owner,
	FHttpRequestPool* Pool,
	FStringView Uri,
	const FCbPackage& RequestPackage,
	FOnRpcComplete&& OnComplete)
{
	auto OnHttpRequestComplete = [OnComplete = MoveTemp(OnComplete)]
	(FHttpRequest::EResult HttpResult, FHttpRequest* Request)
	{
		FCbPackage Response;
		FHttpRequest::EResult ParsedResponseResult = ParseRpcResponse(HttpResult, *Request, Response);
		OnComplete(ParsedResponseResult, Request, Response);
		return FHttpRequest::ECompletionBehavior::Done;
	};
	FLargeMemoryWriter PackageMemory;
	UE::Zen::Http::SaveCbPackage(RequestPackage, PackageMemory);
	uint64 PackageMemorySize = PackageMemory.TotalSize();
	FSharedBuffer PackageSharedBuffer = FSharedBuffer::TakeOwnership(PackageMemory.ReleaseOwnership(), PackageMemorySize, FMemory::Free);
	Request.EnqueueAsyncPost(Owner, Pool, Uri, FCompositeBuffer(PackageSharedBuffer), MoveTemp(OnHttpRequestComplete), EHttpContentType::CbPackage, EHttpContentType::CbPackage);
}

void FZenCacheStore::LegacyStats(FDerivedDataCacheStatsNode& OutNode)
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

	if (ZenStats.UpstreamStats.EndPointStats.IsEmpty())
	{
		OutNode = {TEXT("Zen"), ZenService.GetInstance().GetURL(), /*bIsLocal*/ true};
		OutNode.UsageStats.Add(TEXT(""), LocalStats);
		return;
	}

	TSharedRef<FDerivedDataCacheStatsNode> LocalNode =
		MakeShared<FDerivedDataCacheStatsNode>(TEXT("Zen"), ZenService.GetInstance().GetURL(), /*bIsLocal*/ true);
	LocalNode->UsageStats.Add(TEXT(""), LocalStats);

	TSharedRef<FDerivedDataCacheStatsNode> RemoteNode =
		MakeShared<FDerivedDataCacheStatsNode>(ZenStats.UpstreamStats.EndPointStats[0].Name, ZenStats.UpstreamStats.EndPointStats[0].Url, /*bIsLocal*/ false);
	RemoteNode->UsageStats.Add(TEXT(""), RemoteStats);

	OutNode = {TEXT("Zen Group"), TEXT(""), /*bIsLocal*/ true};
	OutNode.Children.Add(LocalNode);
	OutNode.Children.Add(RemoteNode);
}

bool FZenCacheStore::LegacyDebugOptions(FBackendDebugOptions& InOptions)
{
	DebugOptions = InOptions;
	return true;
}

void FZenCacheStore::Put(
	const TConstArrayView<FCachePutRequest> Requests,
	IRequestOwner& Owner,
	FOnCachePutComplete&& OnComplete)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ZenDDC::PutCachedRecord);
	COOK_STAT(auto Timer = UsageStats.TimePut());

	uint64 BatchSize = 0;
	TBatchView<const FCachePutRequest> Batches(Requests,
		[&BatchSize, this](const FCachePutRequest& NextRequest) -> EBatchView
		{
			const FCacheRecord& Record = NextRequest.Record;
			uint64 RecordSize = sizeof(FCacheKey) + Record.GetMeta().GetSize();
			for (const FValueWithId& Value : Record.GetValues())
			{
				RecordSize += Value.GetData().GetCompressedSize();
			}
			BatchSize += RecordSize;
			if (BatchSize > BatchPutMaxBytes)
			{
				BatchSize = RecordSize;
				return EBatchView::NewBatch;
			}
			return EBatchView::Continue;
		});

	auto OnHit = [this, &OnComplete COOK_STAT(, &Timer)](const FCachePutRequest& Request)
	{
		const FCacheKey& Key = Request.Record.GetKey();
		UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Cache Put complete for %s from '%s'"),
			*GetName(), *WriteToString<96>(Key), *Request.Name);
		COOK_STAT(Timer.AddHit(Private::GetCacheRecordCompressedSize(Request.Record)));
		OnComplete(Request.MakeResponse(EStatus::Ok));
	};
	auto OnMiss = [this, &OnComplete COOK_STAT(, &Timer)](const FCachePutRequest& Request)
	{
		const FCacheKey& Key = Request.Record.GetKey();
		UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Cache Put miss for '%s' from '%s'"),
			*GetName(), *WriteToString<96>(Key), *Request.Name);
		COOK_STAT(Timer.AddMiss());
		OnComplete(Request.MakeResponse(EStatus::Error));
	};

	for (TArrayView<const FCachePutRequest> Batch : Batches)
	{
		FCbPackage BatchPackage;
		FCbWriter BatchWriter;
		BatchWriter.BeginObject();
		{
			BatchWriter << ANSITEXTVIEW("Method") << "PutCacheRecords";
			BatchWriter.BeginObject(ANSITEXTVIEW("Params"));
			{
				ECachePolicy BatchDefaultPolicy = Batch[0].Policy.GetRecordPolicy();
				BatchWriter << ANSITEXTVIEW("DefaultPolicy") << *WriteToString<128>(BatchDefaultPolicy);
				BatchWriter.AddString(ANSITEXTVIEW("Namespace"), StructuredNamespace);

				BatchWriter.BeginArray(ANSITEXTVIEW("Requests"));
				for (const FCachePutRequest& Request : Batch)
				{
					const FCacheRecord& Record = Request.Record;

					BatchWriter.BeginObject();
					{
						BatchWriter.SetName(ANSITEXTVIEW("Record"));
						Record.Save(BatchPackage, BatchWriter);
						if (!Request.Policy.IsUniform() || Request.Policy.GetRecordPolicy() != BatchDefaultPolicy)
						{
							BatchWriter.SetName(ANSITEXTVIEW("Policy"));
							Request.Policy.Save(BatchWriter);
						}
					}
					BatchWriter.EndObject();
				}
				BatchWriter.EndArray();
			}
			BatchWriter.EndObject();
		}
		BatchWriter.EndObject();
		BatchPackage.SetObject(BatchWriter.Save().AsObject());

		FCbPackage BatchResponse;
		FHttpRequest::EResult HttpResult = FHttpRequest::EResult::Failed;

		{
			FScopedHttpPoolRequestPtr Request(RequestPool.Get());
			HttpResult = PerformBlockingRpc(*Request.Get(), TEXTVIEW("/z$/$rpc"), BatchPackage, BatchResponse);
		}

		int32 RequestIndex = 0;
		if (HttpResult == FHttpRequest::EResult::Success)
		{
			const FCbObject& ResponseObj = BatchResponse.GetObject();
			for (FCbField ResponseField : ResponseObj[ANSITEXTVIEW("Result")])
			{
				if (RequestIndex >= Batch.Num())
				{
					++RequestIndex;
					continue;
				}
				const FCachePutRequest& Request = Batch[RequestIndex++];

				const FCacheKey& Key = Request.Record.GetKey();
				bool bPutSucceeded = ResponseField.AsBool();
				if (DebugOptions.ShouldSimulatePutMiss(Key))
				{
					UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Simulated miss for put of %s from '%s'"),
						*GetName(), *WriteToString<96>(Key), *Request.Name);
					bPutSucceeded = false;
				}
				bPutSucceeded ? OnHit(Request) : OnMiss(Request);
			}
			if (RequestIndex != Batch.Num())
			{
				UE_LOG(LogDerivedDataCache, Warning,
					TEXT("Invalid response received from PutCacheRecords rpc: %d results expected, received %d."), Batch.Num(), RequestIndex);
			}
		}
		for (const FCachePutRequest& Request : Batch.RightChop(RequestIndex))
		{
			OnMiss(Request);
		}
	}
}

void FZenCacheStore::Get(
	const TConstArrayView<FCacheGetRequest> Requests,
	IRequestOwner& Owner,
	FOnCacheGetComplete&& OnComplete)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ZenDDC::GetCacheRecord);
	TRACE_COUNTER_ADD(ZenDDC_CacheRecordRequestCount, int64(Requests.Num()));
	TRACE_COUNTER_ADD(ZenDDC_Get, int64(Requests.Num()));
	COOK_STAT(auto Timer = UsageStats.TimeGet());

	auto OnHit = [this, &OnComplete COOK_STAT(, &Timer)](const FCacheGetRequest& Request, FCacheRecord&& Record)
	{
		UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Cache hit for '%s' from '%s'"),
			*GetName(), *WriteToString<96>(Request.Key), *Request.Name);

		TRACE_COUNTER_ADD(ZenDDC_GetHit, int64(1));
		int64 ReceivedSize = Private::GetCacheRecordCompressedSize(Record);
		TRACE_COUNTER_ADD(ZenDDC_BytesReceived, ReceivedSize);
		COOK_STAT(Timer.AddHit(ReceivedSize));

		OnComplete({Request.Name, MoveTemp(Record), Request.UserData, EStatus::Ok});
	};
	auto OnMiss = [this, &OnComplete](const FCacheGetRequest& Request)
	{
		UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Cache miss for '%s' from '%s'"),
			*GetName(), *WriteToString<96>(Request.Key), *Request.Name);

		OnComplete(Request.MakeResponse(EStatus::Error));
	};

	ForEachBatch(CacheRecordBatchSize, Requests.Num(),
		[this, &Requests, &Owner, &OnComplete, &OnHit, &OnMiss](int32 BatchFirst, int32 BatchLast)
	{
		TArrayView<const FCacheGetRequest> Batch(Requests.GetData() + BatchFirst, BatchLast - BatchFirst + 1);

		FCbWriter BatchRequest;
		BatchRequest.BeginObject();
		{
			BatchRequest << ANSITEXTVIEW("Method") << ANSITEXTVIEW("GetCacheRecords");
			BatchRequest.BeginObject(ANSITEXTVIEW("Params"));
			{
				ECachePolicy BatchDefaultPolicy = Batch[0].Policy.GetRecordPolicy();
				BatchRequest << ANSITEXTVIEW("DefaultPolicy") << *WriteToString<128>(BatchDefaultPolicy);
				BatchRequest.AddString(ANSITEXTVIEW("Namespace"), StructuredNamespace);

				BatchRequest.BeginArray(ANSITEXTVIEW("Requests"));
				for (const FCacheGetRequest& Request : Batch)
				{
					BatchRequest.BeginObject();
					{
						BatchRequest << ANSITEXTVIEW("Key") << Request.Key;
						if (!Request.Policy.IsUniform() || Request.Policy.GetRecordPolicy() != BatchDefaultPolicy)
						{
							BatchRequest.SetName(ANSITEXTVIEW("Policy"));
							Request.Policy.Save(BatchRequest);
						}
					}
					BatchRequest.EndObject();
				}
				BatchRequest.EndArray();
			}
			BatchRequest.EndObject();
		}
		BatchRequest.EndObject();

		FCbPackage BatchResponse;
		FHttpRequest::EResult HttpResult = FHttpRequest::EResult::Failed;

		{
			LLM_SCOPE_BYTAG(UntaggedDDCResult);
			FScopedHttpPoolRequestPtr Request(RequestPool.Get());
			HttpResult = PerformBlockingRpc(*Request.Get(), TEXTVIEW("/z$/$rpc"), BatchRequest.Save().AsObject(), BatchResponse);
		}

		int32 RequestIndex = 0;
		if (HttpResult == FHttpRequest::EResult::Success)
		{
			const FCbObject& ResponseObj = BatchResponse.GetObject();
			
			for (FCbField RecordField : ResponseObj[ANSITEXTVIEW("Result")])
			{
				if (RequestIndex >= Batch.Num())
				{
					++RequestIndex;
					continue;
				}
				const FCacheGetRequest& Request = Batch[RequestIndex++];

				const FCacheKey& Key = Request.Key;
				FOptionalCacheRecord Record;

				if (DebugOptions.ShouldSimulateGetMiss(Key))
				{
					UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Simulated miss for put of '%s' from '%s'"),
						*GetName(), *WriteToString<96>(Key), *Request.Name);
				}
				else if (!RecordField.IsNull())
				{
					Record = FCacheRecord::Load(BatchResponse, RecordField.AsObject());
				}
				Record ? OnHit(Request, MoveTemp(Record).Get()) : OnMiss(Request);
			}
			if (RequestIndex != Batch.Num())
			{
				UE_LOG(LogDerivedDataCache, Warning,
					TEXT("Invalid response received from GetCacheRecords rpc: %d results expected, received %d."), Batch.Num(), RequestIndex);
			}
		}
		for (const FCacheGetRequest& Request : Batch.RightChop(RequestIndex))
		{
			OnMiss(Request);
		}
	});
	
	TRACE_COUNTER_SUBTRACT(ZenDDC_CacheRecordRequestCount, int64(Requests.Num()));
}

void FZenCacheStore::PutValue(
	TConstArrayView<FCachePutValueRequest> Requests,
	IRequestOwner& Owner,
	FOnCachePutValueComplete&& OnComplete)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ZenDDC::PutValue);
	COOK_STAT(auto Timer = UsageStats.TimePut());

	uint64 BatchSize = 0;
	TBatchView<const FCachePutValueRequest> Batches(Requests,
		[&BatchSize, this](const FCachePutValueRequest& NextValue) -> EBatchView
		{
			uint64 ValueSize = sizeof(FCacheKey) + NextValue.Value.GetData().GetCompressedSize();
			BatchSize += ValueSize;
			if (BatchSize > BatchPutMaxBytes)
			{
				BatchSize = ValueSize;
				return EBatchView::NewBatch;
			}
			return EBatchView::Continue;
		});

	auto OnHit = [this, &OnComplete COOK_STAT(, &Timer)](const FCachePutValueRequest& Request)
	{
		UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Cache PutValue complete for %s from '%s'"),
			*GetName(), *WriteToString<96>(Request.Key), *Request.Name);
		COOK_STAT(Timer.AddHit(Request.Value.GetData().GetCompressedSize()));
		OnComplete(Request.MakeResponse(EStatus::Ok));
	};
	auto OnMiss = [this, &OnComplete COOK_STAT(, &Timer)](const FCachePutValueRequest& Request)
	{
		UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Cache PutValue miss for '%s' from '%s'"),
			*GetName(), *WriteToString<96>(Request.Key), *Request.Name);
		COOK_STAT(Timer.AddMiss());
		OnComplete(Request.MakeResponse(EStatus::Error));
	};

	for (TArrayView<const FCachePutValueRequest> Batch : Batches)
	{
		FCbPackage BatchPackage;
		FCbWriter BatchWriter;
		BatchWriter.BeginObject();
		{
			BatchWriter << ANSITEXTVIEW("Method") << ANSITEXTVIEW("PutCacheValues");
			BatchWriter.BeginObject(ANSITEXTVIEW("Params"));
			{
				ECachePolicy BatchDefaultPolicy = Batch[0].Policy;
				BatchWriter << ANSITEXTVIEW("DefaultPolicy") << *WriteToString<128>(BatchDefaultPolicy);
				BatchWriter.AddString(ANSITEXTVIEW("Namespace"), StructuredNamespace);

				BatchWriter.BeginArray("Requests");
				for (const FCachePutValueRequest& Request : Batch)
				{
					BatchWriter.BeginObject();
					{
						BatchWriter << ANSITEXTVIEW("Key") << Request.Key;
						const FValue& Value = Request.Value;
						BatchWriter.AddBinaryAttachment("RawHash", Value.GetRawHash());
						if (Value.HasData())
						{
							BatchPackage.AddAttachment(FCbAttachment(Value.GetData()));
						}
						if (Request.Policy != BatchDefaultPolicy)
						{
							BatchWriter << ANSITEXTVIEW("Policy") << WriteToString<128>(Request.Policy);
						}
					}
					BatchWriter.EndObject();
				}
				BatchWriter.EndArray();
			}
			BatchWriter.EndObject();
		}
		BatchWriter.EndObject();
		BatchPackage.SetObject(BatchWriter.Save().AsObject());

		FCbPackage BatchResponse;
		FHttpRequest::EResult HttpResult = FHttpRequest::EResult::Failed;
		{
			FScopedHttpPoolRequestPtr Request(RequestPool.Get());
			HttpResult = PerformBlockingRpc(*Request.Get(), TEXTVIEW("/z$/$rpc"), BatchPackage, BatchResponse);
		}

		int32 RequestIndex = 0;
		if (HttpResult == FHttpRequest::EResult::Success)
		{
			const FCbObject& ResponseObj = BatchResponse.GetObject();
			for (FCbField ResponseField : ResponseObj[ANSITEXTVIEW("Result")])
			{
				if (RequestIndex >= Batch.Num())
				{
					++RequestIndex;
					continue;
				}
				const FCachePutValueRequest& Request = Batch[RequestIndex++];

				bool bPutSucceeded = ResponseField.AsBool();
				if (DebugOptions.ShouldSimulatePutMiss(Request.Key))
				{
					UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Simulated miss for PutValue of %s from '%s'"),
						*GetName(), *WriteToString<96>(Request.Key), *Request.Name);
					bPutSucceeded = false;
				}
				bPutSucceeded ? OnHit(Request) : OnMiss(Request);
			}
			if (RequestIndex != Batch.Num())
			{
				UE_LOG(LogDerivedDataCache, Warning,
					TEXT("Invalid response received from PutCacheValues rpc: %d results expected, received %d."), Batch.Num(), RequestIndex);
			}
		}
		for (const FCachePutValueRequest& Request : Batch.RightChop(RequestIndex))
		{
			OnMiss(Request);
		}
	}
}

void FZenCacheStore::GetValue(
	TConstArrayView<FCacheGetValueRequest> Requests,
	IRequestOwner& Owner,
	FOnCacheGetValueComplete&& OnComplete)
				{
	TRACE_CPUPROFILER_EVENT_SCOPE(ZenDDC::GetValue);
	TRACE_COUNTER_ADD(ZenDDC_CacheRecordRequestCount, int64(Requests.Num()));
	COOK_STAT(auto Timer = UsageStats.TimeGet());
	TRACE_COUNTER_ADD(ZenDDC_Get, (int64)Requests.Num());
					
	auto OnHit = [this, &OnComplete COOK_STAT(, &Timer)](const FCacheGetValueRequest& Request, FValue&& Value)
	{
		UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Cache hit for '%s' from '%s'"),
			*GetName(), *WriteToString<96>(Request.Key), *Request.Name);
					TRACE_COUNTER_ADD(ZenDDC_GetHit, int64(1));
		int64 ReceivedSize = Value.GetData().GetCompressedSize();
					TRACE_COUNTER_ADD(ZenDDC_BytesReceived, ReceivedSize);
					COOK_STAT(Timer.AddHit(ReceivedSize));

		OnComplete({Request.Name, Request.Key, MoveTemp(Value), Request.UserData, EStatus::Ok});
	};
	auto OnMiss = [this, &OnComplete](const FCacheGetValueRequest& Request)
	{
		UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Cache miss for '%s' from '%s'"),
			*GetName(), *WriteToString<96>(Request.Key), *Request.Name);

		OnComplete(Request.MakeResponse(EStatus::Error));
	};

	ForEachBatch(CacheRecordBatchSize, Requests.Num(),
		[this, &Requests, &Owner, &OnComplete, &OnHit, &OnMiss](int32 BatchFirst, int32 BatchLast)
		{
			TArrayView<const FCacheGetValueRequest> Batch(Requests.GetData() + BatchFirst, BatchLast - BatchFirst + 1);

			FCbWriter BatchRequest;
			BatchRequest.BeginObject();
			{
				BatchRequest << ANSITEXTVIEW("Method") << ANSITEXTVIEW("GetCacheValues");
				BatchRequest.BeginObject(ANSITEXTVIEW("Params"));
				{
					ECachePolicy BatchDefaultPolicy = Batch[0].Policy;
					BatchRequest << ANSITEXTVIEW("DefaultPolicy") << *WriteToString<128>(BatchDefaultPolicy);
					BatchRequest.AddString(ANSITEXTVIEW("Namespace"), StructuredNamespace);

					BatchRequest.BeginArray("Requests");
					for (const FCacheGetValueRequest& Request : Batch)
					{
						BatchRequest.BeginObject();
						{
							BatchRequest << ANSITEXTVIEW("Key") << Request.Key;
							if (Request.Policy != BatchDefaultPolicy)
							{
								BatchRequest << ANSITEXTVIEW("Policy") << WriteToString<128>(Request.Policy);
							}
						}
						BatchRequest.EndObject();
					}
					BatchRequest.EndArray();
				}
				BatchRequest.EndObject();
			}
			BatchRequest.EndObject();

			FCbPackage BatchResponse;
			FHttpRequest::EResult HttpResult = FHttpRequest::EResult::Failed;

			{
				LLM_SCOPE_BYTAG(UntaggedDDCResult);
				FScopedHttpPoolRequestPtr Request(RequestPool.Get());
				HttpResult = PerformBlockingRpc(*Request.Get(), TEXTVIEW("/z$/$rpc"), BatchRequest.Save().AsObject(), BatchResponse);
			}

			int32 RequestIndex = 0;
			if (HttpResult == FHttpRequest::EResult::Success)
			{
				const FCbObject& ResponseObj = BatchResponse.GetObject();

				for (FCbFieldView ResultField : ResponseObj[ANSITEXTVIEW("Result")])
				{
					if (RequestIndex >= Batch.Num())
					{
						++RequestIndex;
						continue;
					}
					const FCacheGetValueRequest& Request = Batch[RequestIndex++];

					FCbObjectView ResultObj = ResultField.AsObjectView();
					TOptional<FValue> Value;
					if (DebugOptions.ShouldSimulateGetMiss(Request.Key))
					{
						UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Simulated miss for GetValue of '%s' from '%s'"),
							*GetName(), *WriteToString<96>(Request.Key), *Request.Name);
					}
					else
					{
						FCbFieldView RawHashField = ResultObj["RawHash"];
						FIoHash RawHash = RawHashField.AsHash();
						if (const FCbAttachment* Attachment = BatchResponse.FindAttachment(RawHash))
						{
							Value.Emplace(Attachment->AsCompressedBinary());
						}
						else
						{
							FCbFieldView RawSizeField = ResultObj["RawSize"];
							uint64 RawSize = RawSizeField.AsUInt64();
							if (!RawSizeField.HasError() && !RawHashField.HasError())
							{
								Value.Emplace(RawHash, RawSize);
							}
						}
					}
					(bool)Value ? OnHit(Request, MoveTemp(*Value)) : OnMiss(Request);
				}
				if (RequestIndex != Batch.Num())
				{
					UE_LOG(LogDerivedDataCache, Warning,
						TEXT("Invalid response received from GetCacheValues rpc: %d results expected, received %d."), Batch.Num(), RequestIndex);
				}
			}
			for (const FCacheGetValueRequest& Request : Batch.RightChop(RequestIndex))
			{
				OnMiss(Request);
			}
		});
	TRACE_COUNTER_SUBTRACT(ZenDDC_CacheRecordRequestCount, int64(Requests.Num()));
}

void FZenCacheStore::GetChunks(
	TConstArrayView<FCacheGetChunkRequest> Requests,
	IRequestOwner& Owner,
	FOnCacheGetChunkComplete&& OnComplete)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ZenDDC::GetChunks);
	TRACE_COUNTER_ADD(ZenDDC_ChunkRequestCount, int64(Requests.Num()));
	TRACE_COUNTER_ADD(ZenDDC_Get, int64(Requests.Num()));
	COOK_STAT(auto Timer = UsageStats.TimeGet());

	TArray<FCacheGetChunkRequest, TInlineAllocator<16>> SortedRequests(Requests);
	SortedRequests.StableSort(TChunkLess());
	auto OnHit = [this, &OnComplete COOK_STAT(, &Timer)](const FCacheGetChunkRequest& Request, FIoHash&& RawHash, uint64 RawSize, FSharedBuffer&& RequestedBytes)
	{
		UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: CacheChunk hit for '%s' from '%s'"),
			*GetName(), *WriteToString<96>(Request.Key, '/', Request.Id), *Request.Name);
		TRACE_COUNTER_ADD(ZenDDC_GetHit, int64(1));
		COOK_STAT(Timer.AddHit(RawSize));
		OnComplete({Request.Name, Request.Key, Request.Id, Request.RawOffset,
			RawSize, MoveTemp(RawHash), MoveTemp(RequestedBytes), Request.UserData, EStatus::Ok});
	};

	auto OnMiss = [this, &OnComplete COOK_STAT(, &Timer)](const FCacheGetChunkRequest& Request)
	{
		UE_LOG(LogDerivedDataCache, Display, TEXT("%s: CacheChunk miss with missing value '%s' for '%s' from '%s'"),
			*GetName(), *WriteToString<16>(Request.Id), *WriteToString<96>(Request.Key), *Request.Name);
		OnComplete(Request.MakeResponse(EStatus::Error));
	};

	ForEachBatch(CacheChunksBatchSize, SortedRequests.Num(),
		[this, &SortedRequests, &Owner, &OnHit, &OnMiss](int32 BatchFirst, int32 BatchLast)
	{
		TArrayView<const FCacheGetChunkRequest> Batch(SortedRequests.GetData() + BatchFirst, BatchLast - BatchFirst + 1);

		FCbWriter BatchRequest;
		BatchRequest.BeginObject();
		{
			BatchRequest << ANSITEXTVIEW("Method") << "GetCacheChunks";
			BatchRequest.AddInteger(ANSITEXTVIEW("MethodVersion"), 1);
			BatchRequest.BeginObject(ANSITEXTVIEW("Params"));
			{
				ECachePolicy DefaultPolicy = Batch[0].Policy;
				BatchRequest << ANSITEXTVIEW("DefaultPolicy") << WriteToString<128>(DefaultPolicy);
				BatchRequest.AddString(ANSITEXTVIEW("Namespace"), StructuredNamespace);

				BatchRequest.BeginArray(ANSITEXTVIEW("ChunkRequests"));
				for (const FCacheGetChunkRequest& Request : Batch)
				{
					BatchRequest.BeginObject();
					{
						BatchRequest << ANSITEXTVIEW("Key") << Request.Key;

						if (Request.Id.IsValid())
						{
							BatchRequest.AddObjectId(ANSITEXTVIEW("ValueId"), Request.Id);
						}
						if (Request.RawOffset != 0)
						{
							BatchRequest << ANSITEXTVIEW("RawOffset") << Request.RawOffset;
						}
						if (Request.RawSize != MAX_uint64)
						{
							BatchRequest << ANSITEXTVIEW("RawSize") << Request.RawSize;
						}
						if (!Request.RawHash.IsZero())
						{
							BatchRequest << ANSITEXTVIEW("ChunkId") << Request.RawHash;
						}
						if (Request.Policy != DefaultPolicy)
						{
							BatchRequest << ANSITEXTVIEW("Policy") << WriteToString<128>(Request.Policy);
						}
					}
					BatchRequest.EndObject();
				}
				BatchRequest.EndArray();
			}
			BatchRequest.EndObject();
		}
		BatchRequest.EndObject();

		FCbPackage BatchResponse;
		FHttpRequest::EResult HttpResult = FHttpRequest::EResult::Failed;

		{
			LLM_SCOPE_BYTAG(UntaggedDDCResult);
			FScopedHttpPoolRequestPtr Request(RequestPool.Get());
			HttpResult = PerformBlockingRpc(*Request.Get(), TEXTVIEW("/z$/$rpc"), BatchRequest.Save().AsObject(), BatchResponse);
		}

		int32 RequestIndex = 0;
		if (HttpResult == FHttpRequest::EResult::Success)
		{
			const FCbObject& ResponseObj = BatchResponse.GetObject();

			for (FCbFieldView ResultView : ResponseObj[ANSITEXTVIEW("Result")])
			{
				if (RequestIndex >= Batch.Num())
				{
					++RequestIndex;
					continue;
				}
				const FCacheGetChunkRequest& Request = Batch[RequestIndex++];
					
				FIoHash RawHash;
				bool Succeeded = false;
				uint64 RawSize = 0;
				FCbObjectView ResultObject = ResultView.AsObjectView();
				FSharedBuffer RequestedBytes;
				if (DebugOptions.ShouldSimulateGetMiss(Request.Key))
				{
					UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Simulated miss for get of '%s' from '%s'"),
						*GetName(), *WriteToString<96>(Request.Key, '/', Request.Id), *Request.Name);
				}
				else
				{
					FCbFieldView HashView = ResultObject[ANSITEXTVIEW("RawHash")];
					RawHash = HashView.AsHash();
					if (!HashView.HasError())
					{
						if (const FCbAttachment* Attachment = BatchResponse.FindAttachment(HashView.AsHash()))
						{
							FCompressedBuffer CompressedBuffer = Attachment->AsCompressedBinary();
							if (CompressedBuffer)
							{
								TRACE_COUNTER_ADD(ZenDDC_BytesReceived, CompressedBuffer.GetCompressedSize());
								RequestedBytes = FCompressedBufferReader(CompressedBuffer).Decompress(Request.RawOffset, Request.RawSize);
								RawSize = RequestedBytes.GetSize();
								Succeeded = true;
							}
						}
						else
						{
							FCbFieldView RawSizeField = ResultObject[ANSITEXTVIEW("RawSize")];
							uint64 TotalSize = RawSizeField.AsUInt64();
							Succeeded = !RawSizeField.HasError();
							if (Succeeded)
							{
								RawSize = FMath::Min(Request.RawSize, TotalSize - FMath::Min(Request.RawOffset, TotalSize));
							}
						}
					}
				}
				Succeeded ? OnHit(Request, MoveTemp(RawHash), RawSize, MoveTemp(RequestedBytes)) : OnMiss(Request);
			}
		}
		for (const FCacheGetChunkRequest& Request : Batch.RightChop(RequestIndex))
		{
			OnMiss(Request);
		}
	});

	TRACE_COUNTER_SUBTRACT(ZenDDC_ChunkRequestCount, int64(Requests.Num()));
}

TTuple<ILegacyCacheStore*, ECacheStoreFlags> CreateZenCacheStore(const TCHAR* NodeName, const TCHAR* Config)
{
	FString ServiceUrl;
	FParse::Value(Config, TEXT("Host="), ServiceUrl);

	FString Namespace;
	if (!FParse::Value(Config, TEXT("Namespace="), Namespace))
	{
		Namespace = FApp::GetProjectName();
		UE_LOG(LogDerivedDataCache, Warning, TEXT("%s: Missing required parameter 'Namespace', falling back to '%s'"), NodeName, *Namespace);
	}

	FString StructuredNamespace;
	if (!FParse::Value(Config, TEXT("StructuredNamespace="), StructuredNamespace))
	{
		StructuredNamespace = Namespace;
		UE_LOG(LogDerivedDataCache, Warning, TEXT("%s: Missing required parameter 'StructuredNamespace', falling back to Namespace: '%s'"), NodeName, *StructuredNamespace);
	}

	TUniquePtr<FZenCacheStore> Backend = MakeUnique<FZenCacheStore>(*ServiceUrl, *Namespace, *StructuredNamespace);
	if (!Backend->IsUsable())
	{
		UE_LOG(LogDerivedDataCache, Warning, TEXT("%s: Failed to contact the service (%s), will not use it."), NodeName, *Backend->GetName());
		Backend.Reset();
	}

	return MakeTuple(Backend.Release(), ECacheStoreFlags::Local | ECacheStoreFlags::Remote | ECacheStoreFlags::Query | ECacheStoreFlags::Store);
}

} // namespace UE::DerivedData

#else

namespace UE::DerivedData
{

TTuple<ILegacyCacheStore*, ECacheStoreFlags> CreateZenCacheStore(const TCHAR* NodeName, const TCHAR* Config)
{
	UE_LOG(LogDerivedDataCache, Warning, TEXT("%s: Zen cache is not yet supported in the current build configuration."), NodeName);
	return MakeTuple(nullptr, ECacheStoreFlags::None);
}

} // UE::DerivedData

#endif // UE_WITH_ZEN
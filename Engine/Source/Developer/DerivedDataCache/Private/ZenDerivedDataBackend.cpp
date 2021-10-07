// Copyright Epic Games, Inc. All Rights Reserved.
#include "ZenDerivedDataBackend.h"

#if WITH_ZEN_DDC_BACKEND

#include "Algo/Accumulate.h"
#include "DerivedDataCacheRecord.h"
#include "DerivedDataChunk.h"
#include "DerivedDataPayload.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/ScopeLock.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Serialization/CompactBinary.h"
#include "Serialization/CompactBinaryPackage.h"
#include "Serialization/CompactBinaryValidation.h"
#include "Serialization/CompactBinaryWriter.h"
#include "Serialization/BufferArchive.h"
#include "ZenBackendUtils.h"
#include "ZenSerialization.h"
#include "ZenServerHttp.h"

TRACE_DECLARE_INT_COUNTER(ZenDDC_Exist,			TEXT("ZenDDC Exist"));
TRACE_DECLARE_INT_COUNTER(ZenDDC_ExistHit,		TEXT("ZenDDC Exist Hit"));
TRACE_DECLARE_INT_COUNTER(ZenDDC_Get,			TEXT("ZenDDC Get"));
TRACE_DECLARE_INT_COUNTER(ZenDDC_GetHit,		TEXT("ZenDDC Get Hit"));
TRACE_DECLARE_INT_COUNTER(ZenDDC_Put,			TEXT("ZenDDC Put"));
TRACE_DECLARE_INT_COUNTER(ZenDDC_PutHit,		TEXT("ZenDDC Put Hit"));
TRACE_DECLARE_INT_COUNTER(ZenDDC_BytesReceived, TEXT("ZenDDC Bytes Received"));
TRACE_DECLARE_INT_COUNTER(ZenDDC_BytesSent,		TEXT("ZenDDC Bytes Sent"));

namespace UE::DerivedData::Backends {

//----------------------------------------------------------------------------------------------------------
// FZenDerivedDataBackend
//----------------------------------------------------------------------------------------------------------

FZenDerivedDataBackend::FZenDerivedDataBackend(
	const TCHAR* InServiceUrl,
	const TCHAR* InNamespace)
	: Namespace(InNamespace)
	, ZenService(InServiceUrl)
{
	if (IsServiceReady())
	{
		RequestPool = MakeUnique<Zen::FZenHttpRequestPool>(ZenService.GetInstance().GetURL());
		bIsUsable = true;
	}
 	bCacheRecordEndpointEnabled = false;
	GConfig->GetBool(TEXT("Zen"), TEXT("CacheRecordEndpointEnabled"), bCacheRecordEndpointEnabled, GEngineIni);
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
	TRACE_CPUPROFILER_EVENT_SCOPE(ZenDDC_Exist);
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
	TRACE_CPUPROFILER_EVENT_SCOPE(ZenDDC_Get);
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

void FZenDerivedDataBackend::AppendZenUri(const FCacheKey& CacheKey, const FPayloadId& PayloadId, FStringBuilderBase& Out)
{
	AppendZenUri(CacheKey, Out);
	Out << TEXT('/') << PayloadId;
}

void FZenDerivedDataBackend::AppendPolicyQueryString(ECachePolicy Policy, FStringBuilderBase& Uri)
{
	bool bQueryEmpty = true;
	bool bValueEmpty = true;
	auto AppendKey = [&Uri, &bQueryEmpty, &bValueEmpty](const TCHAR* Key)
	{
		if (bQueryEmpty)
		{
			TCHAR LastChar = Uri.Len() == 0 ? '\0' : Uri.LastChar();
			if (LastChar != '?' && LastChar != '&')
			{
				Uri << '?';
			}
			bQueryEmpty = false;
		}
		else
		{
			Uri << '&';
		}
		bValueEmpty = true;
		Uri << Key;
	};
	auto AppendValue = [&Uri, &bValueEmpty](const TCHAR* Value)
	{
		if (bValueEmpty)
		{
			bValueEmpty = false;
		}
		else
		{
			Uri << ',';
		}
		Uri << Value;
	};

	if (!EnumHasAllFlags(Policy, ECachePolicy::Query))
	{
		AppendKey(TEXT("query="));
		if (EnumHasAnyFlags(Policy, ECachePolicy::QueryLocal)) { AppendValue(TEXT("local")); }
		if (EnumHasAnyFlags(Policy, ECachePolicy::QueryRemote)) { AppendValue(TEXT("remote")); }
		if (!EnumHasAnyFlags(Policy, ECachePolicy::Query)) { AppendValue(TEXT("none")); }
	}
	if (!EnumHasAllFlags(Policy, ECachePolicy::Store))
	{
		AppendKey(TEXT("store="));
		if (EnumHasAnyFlags(Policy, ECachePolicy::StoreLocal)) { AppendValue(TEXT("local")); }
		if (EnumHasAnyFlags(Policy, ECachePolicy::StoreRemote)) { AppendValue(TEXT("remote")); }
		if (!EnumHasAnyFlags(Policy, ECachePolicy::Store)) { AppendValue(TEXT("none")); }
	}
	if (EnumHasAnyFlags(Policy, ECachePolicy::SkipData))
	{
		AppendKey(TEXT("skip="));
		if (EnumHasAllFlags(Policy, ECachePolicy::SkipData)) { AppendValue(TEXT("data")); }
		else
		{
			if (EnumHasAnyFlags(Policy, ECachePolicy::SkipMeta)) { AppendValue(TEXT("meta")); }
			if (EnumHasAnyFlags(Policy, ECachePolicy::SkipValue)) { AppendValue(TEXT("value")); }
			if (EnumHasAnyFlags(Policy, ECachePolicy::SkipAttachments)) { AppendValue(TEXT("attachments")); }
		}
	}
}

uint64 FZenDerivedDataBackend::MeasureCacheRecord(const FCacheRecord& Record)
{
	return Record.GetMeta().GetSize() +
		Record.GetValuePayload().GetRawSize() +
		Algo::TransformAccumulate(Record.GetAttachmentPayloads(), &FPayload::GetRawSize, uint64(0));
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
	TSharedRef<FDerivedDataCacheStatsNode> Usage = MakeShared<FDerivedDataCacheStatsNode>(this, FString::Printf(TEXT("%s.%s"), TEXT("ZenDDC"), *GetName()));
	Usage->Stats.Add(TEXT(""), UsageStats);

	return Usage;
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
	TConstArrayView<FCacheRecord> Records,
	FStringView Context,
	ECachePolicy Policy,
	IRequestOwner& Owner,
	FOnCachePutComplete&& OnComplete)
{
	for (const FCacheRecord& Record : Records)
	{
		COOK_STAT(auto Timer = UsageStats.TimePut());
		bool bResult;
		if (ShouldSimulateMiss(Record.GetKey()))
		{
			UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Simulated miss for put of %s from '%.*s'"),
				*GetName(), *WriteToString<96>(Record.GetKey()), Context.Len(), Context.GetData());
			bResult = false;
		}
		else if (bCacheRecordEndpointEnabled)
		{
			bResult = PutCacheRecord(Record, Context, Policy);
		}
		else
		{
			bResult = LegacyPutCacheRecord(Record, Context, Policy);
		}
		if (bResult)
		{
			UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Cache put complete for %s from '%.*s'"),
				*GetName(), *WriteToString<96>(Record.GetKey()), Context.Len(), Context.GetData());
			COOK_STAT(Timer.AddHit(MeasureCacheRecord(Record)));
			if (OnComplete)
			{
				OnComplete({ Record.GetKey(), EStatus::Ok });
			}
		}
		else
		{
			COOK_STAT(Timer.AddMiss(MeasureCacheRecord(Record)));
			if (OnComplete)
			{
				OnComplete({ Record.GetKey(), EStatus::Error });
			}
		}
	}
}

void FZenDerivedDataBackend::Get(
	TConstArrayView<FCacheKey> Keys,
	FStringView Context,
	FCacheRecordPolicy Policy,
	IRequestOwner& Owner,
	FOnCacheGetComplete&& OnComplete)
{
	for (const FCacheKey& Key : Keys)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ZenDDC_Get);
		TRACE_COUNTER_ADD(ZenDDC_Get, int64(1));
		COOK_STAT(auto Timer = UsageStats.TimeGet());

		FOptionalCacheRecord Record;
		if (ShouldSimulateMiss(Key))
		{
			UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Simulated miss for get of %s from '%.*s'"),
				*GetName(), *WriteToString<96>(Key), Context.Len(), Context.GetData());
		}
		else if (bCacheRecordEndpointEnabled)
		{
			Record = GetCacheRecord(Key, Context, Policy);
		}
		else
		{
			Record = LegacyGetCacheRecord(Key, Context, Policy);
		}
		if (Record)
		{
			UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Cache hit for %s from '%.*s'"),
				*GetName(), *WriteToString<96>(Key), Context.Len(), Context.GetData());
			TRACE_COUNTER_ADD(ZenDDC_GetHit, int64(1));
			int64 ReceivedSize = MeasureCacheRecord(Record.Get());
			TRACE_COUNTER_ADD(ZenDDC_BytesReceived, ReceivedSize);
			COOK_STAT(Timer.AddHit(ReceivedSize));

			if (OnComplete)
			{
				OnComplete({ MoveTemp(Record).Get(), EStatus::Ok });
			}
		}
		else
		{
			if (OnComplete)
			{
				OnComplete({ FCacheRecordBuilder(Key).Build(), EStatus::Error });
			}
		}
	}
}

void FZenDerivedDataBackend::GetChunks(
	TConstArrayView<FCacheChunkRequest> Chunks,
	FStringView Context,
	IRequestOwner& Owner,
	FOnCacheGetChunkComplete&& OnComplete)
{
	if (bCacheRecordEndpointEnabled)
	{
		for (const FCacheChunkRequest& Chunk : Chunks)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(ZenDDC_Get);
			TRACE_COUNTER_ADD(ZenDDC_Get, int64(1));
			COOK_STAT(auto Timer = UsageStats.TimeGet());

			TStringBuilder<256> QueryUri;
			AppendZenUri(Chunk.Key, Chunk.Id, QueryUri);
			AppendPolicyQueryString(Chunk.Policy, QueryUri);

			EGetResult GetResult;
			FCompressedBuffer CompressedBuffer;
			if (ShouldSimulateMiss(Chunk.Key))
			{
				UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Simulated miss for get of %s from '%.*s'"),
					*GetName(), *WriteToString<96>(Chunk.Key, '/', Chunk.Id), Context.Len(), Context.GetData());
				GetResult = EGetResult::NotFound;
			}
			else
			{
				TArray64<uint8> PayloadData;
				GetResult = GetZenData(QueryUri, &PayloadData, Zen::EContentType::CompressedBinary);
				if (GetResult == EGetResult::Success)
				{
					CompressedBuffer = FCompressedBuffer::FromCompressed(MakeSharedBufferFromArray(MoveTemp(PayloadData)));
				}
			}
			if (CompressedBuffer)
			{
				const uint64 RawSize = FMath::Min(CompressedBuffer.GetRawSize(), Chunk.RawSize);
				UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Cache hit for %s from '%.*s'"),
					*GetName(), *WriteToString<96>(Chunk.Key, '/', Chunk.Id), Context.Len(), Context.GetData());
				TRACE_COUNTER_ADD(ZenDDC_GetHit, int64(1));
				TRACE_COUNTER_ADD(ZenDDC_BytesReceived, RawSize);
				COOK_STAT(Timer.AddHit(RawSize));
				if (OnComplete)
				{
					FUniqueBuffer Buffer = FUniqueBuffer::Alloc(RawSize);
					CompressedBuffer.DecompressToComposite().CopyTo(Buffer, Chunk.RawOffset);
					OnComplete({ Chunk.Key, Chunk.Id, Chunk.RawOffset, RawSize, CompressedBuffer.GetRawHash(), Buffer.MoveToShared(), EStatus::Ok });
				}
			}
			else
			{
				UE_LOG(LogDerivedDataCache, Display, TEXT("%s: Cache miss with %s payload %s for %s from '%.*s'"),
					*GetName(), (GetResult == EGetResult::NotFound ? TEXT("missing") : TEXT("corrupted")),
					*WriteToString<16>(Chunk.Id), *WriteToString<96>(Chunk.Key), Context.Len(), Context.GetData());
				if (OnComplete)
				{
					OnComplete({ Chunk.Key, Chunk.Id, Chunk.RawOffset, 0, {}, {}, EStatus::Error });
				}
			}
		}
	}
	else
	{
		// We have to load the CacheRecord for each chunks, so sort all the chunks sharing
		// the same key to occur together and we can load the CacheRecord at the start of each run.
		TArray<FCacheChunkRequest, TInlineAllocator<16>> SortedChunks(Chunks);
		SortedChunks.StableSort(TChunkLess());

		FOptionalCacheRecord Record;
		for (const FCacheChunkRequest& Chunk : SortedChunks)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(ZenDDC_Get);
			TRACE_COUNTER_ADD(ZenDDC_Get, int64(1));
			COOK_STAT(auto Timer = UsageStats.TimeGet());

			if (!Record || Record.Get().GetKey() != Chunk.Key)
			{
				Record = LegacyGetCacheRecord(Chunk.Key, Context, Chunk.Policy | ECachePolicy::SkipData, /*bAlwaysLoadInlineData*/ true);
			}
			if (Record)
			{
				const ECachePolicy PolicyMask = Record.Get().GetValuePayload().GetId() == Chunk.Id
					? ECachePolicy::SkipValue : ECachePolicy::SkipAttachments;
				const ECachePolicy Policy = Chunk.Policy | (ECachePolicy::SkipData & ~PolicyMask);
				if (FPayload Payload = LegacyGetCachePayload(Chunk.Key, Context, Policy, Record.Get().GetPayload(Chunk.Id)))
				{
					const uint64 RawSize = FMath::Min(Payload.GetRawSize(), Chunk.RawSize);
					UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Cache hit for %s from '%.*s'"),
						*GetName(), *WriteToString<96>(Chunk.Key, '/', Chunk.Id), Context.Len(), Context.GetData());
					TRACE_COUNTER_ADD(ZenDDC_GetHit, int64(1));
					TRACE_COUNTER_ADD(ZenDDC_BytesReceived, RawSize);
					COOK_STAT(Timer.AddHit(RawSize));
					if (OnComplete)
					{
						FUniqueBuffer Buffer = FUniqueBuffer::Alloc(RawSize);
						Payload.GetData().DecompressToComposite().CopyTo(Buffer, Chunk.RawOffset);
						OnComplete({ Chunk.Key, Chunk.Id, Chunk.RawOffset, RawSize, Payload.GetRawHash(), Buffer.MoveToShared(), EStatus::Ok });
					}
					continue;
				}
			}

			if (OnComplete)
			{
				OnComplete({ Chunk.Key, Chunk.Id, Chunk.RawOffset, 0, {}, {}, EStatus::Error });
			}
		}
	}
}

bool FZenDerivedDataBackend::PutCacheRecord(const FCacheRecord& Record, FStringView Context, ECachePolicy Policy)
{
	const FCacheKey& Key = Record.GetKey();
	FCbPackage Package = Record.Save();
	FBufferArchive Ar;
	Package.Save(Ar);
	FCompositeBuffer Buffer = FCompositeBuffer(FSharedBuffer::MakeView(Ar.GetData(), Ar.Num()));
	TStringBuilder<256> Uri;
	AppendZenUri(Record.GetKey(), Uri);
	AppendPolicyQueryString(Policy, Uri);
	if (PutZenData(Uri.ToString(), Buffer, Zen::EContentType::CbPackage)
		!= FDerivedDataBackendInterface::EPutStatus::Cached)
	{
		return false;
	}

	return true;
}

FOptionalCacheRecord FZenDerivedDataBackend::GetCacheRecord(
	const FCacheKey& Key,
	const FStringView Context,
	const FCacheRecordPolicy& Policy) const
{
	FCbPackage Package;
	EGetResult GetResult = GetZenData(Key, Policy.GetRecordPolicy(), Package);
	if (GetResult != EGetResult::Success)
	{
		switch (GetResult)
		{
		default:
			UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Cache miss with missing record for %s from '%.*s'"),
				*GetName(), *WriteToString<96>(Key), Context.Len(), Context.GetData());
			break;
		case EGetResult::Corrupted:
			UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Cache miss with corrupted record (corrupted package) for %s from '%.*s'"),
				*GetName(), *WriteToString<96>(Key), Context.Len(), Context.GetData());
			break;
		}
		return FOptionalCacheRecord();
	}
	FOptionalCacheRecord Record = FCacheRecord::Load(Package);
	if (!Record)
	{
		UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Cache miss with corrupted record (corrupted record) for %s from '%.*s'"),
			*GetName(), *WriteToString<96>(Key), Context.Len(), Context.GetData());
	}
	return Record;
}

bool FZenDerivedDataBackend::LegacyPutCacheRecord(const FCacheRecord& Record, FStringView Context, ECachePolicy Policy)
{
	const FCacheKey& Key = Record.GetKey();
	// Save the payloads and build the cache object.
	const TConstArrayView<FPayload> Attachments = Record.GetAttachmentPayloads();
	TCbWriter<512> Writer;
	Writer.BeginObject();
	if (const FCbObject& Meta = Record.GetMeta())
	{
		Writer.AddObject("Meta"_ASV, Meta);
		Writer.AddHash("MetaHash"_ASV, Meta.GetHash());
	}
	if (const FPayload& Value = Record.GetValuePayload())
	{
		Writer.SetName("Value"_ASV);
		if (!LegacyPutCachePayload(Key, Context, Value, Writer))
		{
			return false;
		}
	}
	if (!Attachments.IsEmpty())
	{
		Writer.BeginArray("Attachments"_ASV);
		for (const FPayload& Attachment : Attachments)
		{
			if (!LegacyPutCachePayload(Key, Context, Attachment, Writer))
			{
				return false;
			}
		}
		Writer.EndArray();
	}
	Writer.EndObject();

	// Save the record to storage.
	FUniqueBuffer Buffer = FUniqueBuffer::Alloc(Writer.GetSaveSize());
	Writer.Save(Buffer);
	TStringBuilder<256> Uri;
	LegacyMakeZenKey(Key, Uri);
	if (PutZenData(Uri.ToString(), FCompositeBuffer(Buffer.MoveToShared()), Zen::EContentType::CbObject)
		!= FDerivedDataBackendInterface::EPutStatus::Cached)
	{
		return false;
	}

	return true;
}

bool FZenDerivedDataBackend::LegacyPutCachePayload(const FCacheKey& Key, FStringView Context, const FPayload& Payload, FCbWriter& Writer)
{
	const FIoHash& RawHash = Payload.GetRawHash();
	const FCompositeBuffer CompressedBuffer = Payload.GetData().GetCompressed();

	uint64 RawSize = Payload.GetRawSize();
	if (RawSize != 0)
	{
		TStringBuilder<256> Uri;
		LegacyMakePayloadKey(Key, RawHash, Uri);
		if (Payload.HasData())
		{
			if (PutZenData(Uri.ToString(), CompressedBuffer, Zen::EContentType::CompressedBinary) != FDerivedDataBackendInterface::EPutStatus::Cached)
			{
				return false;
			}
		}
		else
		{
			// Put of a record with payloads-with-no-data should succeed iff the payload data is available in the CacheStore
			if (GetZenData(Uri.ToString(), nullptr, Zen::EContentType::CompressedBinary) != EGetResult::Success)
			{
				return false;
			}
		}
	}

	Writer.BeginObject();
	Writer.AddObjectId("Id"_ASV, Payload.GetId());
	Writer.AddInteger("RawSize"_ASV, RawSize);
	if (RawSize == 0)
	{
		Writer.AddHash("RawHash"_ASV, RawHash);
		Writer.AddHash("CompressedHash"_ASV, FIoHash::HashBuffer(CompressedBuffer));
		Writer.AddBinary("CompressedData"_ASV, CompressedBuffer);
	}
	else
	{
		Writer.AddBinaryAttachment("RawHash"_ASV, RawHash);
	}
	Writer.EndObject();
	return true;
}

FOptionalCacheRecord FZenDerivedDataBackend::LegacyGetCacheRecord(
	const FCacheKey& Key,
	const FStringView Context,
	const FCacheRecordPolicy& Policy,
	const bool bAlwaysLoadInlineData) const
{
	TStringBuilder<256> Uri;
	LegacyMakeZenKey(Key, Uri);
	TArray64<uint8> Buffer;
	EGetResult Result = GetZenData(Uri.ToString(), &Buffer, Zen::EContentType::CbObject);
	if (Result != EGetResult::Success)
	{
		return FOptionalCacheRecord();
	}
	return FZenDerivedDataBackend::LegacyCreateRecord(MakeSharedBufferFromArray(MoveTemp(Buffer)),
		Key, Context, Policy, bAlwaysLoadInlineData);
}

FPayload FZenDerivedDataBackend::LegacyGetCachePayload(const FCacheKey& Key, FStringView Context, ECachePolicy Policy,
	const FPayload& PayloadIdOnly) const
{
	auto GetDescriptor = [&PayloadIdOnly, &Key, &Context]()
	{
		return FString::Printf(TEXT("%s with hash %s for %s from '%.*s'"),
			*WriteToString<16>(PayloadIdOnly.GetId()), *WriteToString<48>(PayloadIdOnly.GetRawHash()),
			*WriteToString<96>(Key), Context.Len(), Context.GetData());
	};

	if (PayloadIdOnly.HasData())
	{
		return PayloadIdOnly;
	}

	TStringBuilder<256> Uri;
	LegacyMakePayloadKey(Key, PayloadIdOnly.GetRawHash(), Uri);

	bool bSkipData = EnumHasAllFlags(Policy, ECachePolicy::SkipData);
	EGetResult GetResult;
	TArray64<uint8> ArrayBuffer;
	GetResult = GetZenData(Uri.ToString(), bSkipData ? nullptr : &ArrayBuffer, Zen::EContentType::CompressedBinary);
	if (GetResult != EGetResult::Success)
	{
		switch (GetResult)
		{
		default:
			UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Cache miss with missing payload %s"),
				*GetName(), *GetDescriptor());
			break;
		case EGetResult::Corrupted:
			UE_LOG(LogDerivedDataCache, Display, TEXT("%s: Cache miss with corrupted payload %s"),
				*GetName(), *GetDescriptor());
			break;
		}
		return FPayload();
	}
	if (bSkipData)
	{
		return PayloadIdOnly;
	}

	FCompressedBuffer CompressedBuffer = FCompressedBuffer::FromCompressed(
		MakeSharedBufferFromArray(MoveTemp(ArrayBuffer)));
	if (!CompressedBuffer || CompressedBuffer.GetRawHash() != PayloadIdOnly.GetRawHash())
	{
		UE_LOG(LogDerivedDataCache, Display, TEXT("%s: Cache miss with corrupted payload %s"),
			*GetName(), *GetDescriptor());
		return FPayload();
	}
	return FPayload(PayloadIdOnly.GetId(), MoveTemp(CompressedBuffer));
}

FOptionalCacheRecord FZenDerivedDataBackend::LegacyCreateRecord(
	FSharedBuffer&& RecordBytes,
	const FCacheKey& Key,
	const FStringView Context,
	const FCacheRecordPolicy& Policy,
	const bool bAlwaysLoadInlineData) const
{
	// TODO: Temporary function copied from FFileSystemDerivedDataBackend.
	// Eventually We will instead send the Policy to zen sever along with the request for the FCacheKey,
	// and it will end back all payloads in the response rather than requiring separate requests.
	const FCbObject RecordObject(MoveTemp(RecordBytes));
	FCacheRecordBuilder RecordBuilder(Key);

	if (!EnumHasAnyFlags(Policy.GetRecordPolicy(), ECachePolicy::SkipMeta))
	{
		if (FCbFieldView MetaHash = RecordObject.FindView("MetaHash"_ASV))
		{
			if (FCbObject MetaObject = RecordObject["Meta"_ASV].AsObject(); MetaObject.GetHash() == MetaHash.AsHash())
			{
				RecordBuilder.SetMeta(MoveTemp(MetaObject));
			}
			else
			{
				UE_LOG(LogDerivedDataCache, Display, TEXT("%s: Cache miss with corrupted metadata for %s from '%.*s'"),
					*GetName(), *WriteToString<96>(Key), Context.Len(), Context.GetData());
				return FOptionalCacheRecord();
			}
		}
	}

	if (FCbObject ValueObject = RecordObject["Value"_ASV].AsObject())
	{
		FPayload Payload = LegacyGetCachePayload(Key, Context, Policy,
			ECachePolicy::SkipValue, ValueObject, bAlwaysLoadInlineData);
		if (Payload.IsNull())
		{
			return FOptionalCacheRecord();
		}
		RecordBuilder.SetValue(MoveTemp(Payload));
	}

	for (FCbField AttachmentField : RecordObject["Attachments"_ASV])
	{
		FPayload Payload = LegacyGetCachePayload(Key, Context, Policy,
			ECachePolicy::SkipAttachments, AttachmentField.AsObject(), bAlwaysLoadInlineData);
		if (Payload.IsNull())
		{
			return FOptionalCacheRecord();
		}
		RecordBuilder.AddAttachment(MoveTemp(Payload));
	}

	return RecordBuilder.Build();
}

FPayload FZenDerivedDataBackend::LegacyGetCachePayload(
	const FCacheKey& Key,
	const FStringView Context,
	const FCacheRecordPolicy& Policy,
	const ECachePolicy SkipFlag,
	const FCbObject& Object,
	const bool bAlwaysLoadInlineData) const
{
	// TODO: Temporary function copied from FFileSystemDerivedDataBackend, helper for CreateRecord
	const FPayloadId Id = Object.FindView("Id"_ASV).AsObjectId();
	const uint64 RawSize = Object.FindView("RawSize"_ASV).AsUInt64(MAX_uint64);
	const FIoHash RawHash = Object.FindView("RawHash"_ASV).AsHash();
	FIoHash CompressedHash = Object.FindView("CompressedHash"_ASV).AsHash();
	FSharedBuffer CompressedData = Object["CompressedData"_ASV].AsBinary();

	if (Id.IsNull() || RawSize == MAX_uint64 || RawHash.IsZero() || !(CompressedHash.IsZero() == CompressedData.IsNull()))
	{
		UE_LOG(LogDerivedDataCache, Display, TEXT("%s: Cache miss with invalid record format for %s from '%.*s'"),
			*GetName(), *WriteToString<96>(Key), Context.Len(), Context.GetData());
		return FPayload();
	}

	const ECachePolicy PayloadPolicy = Policy.GetPayloadPolicy(Id);
	FPayload Payload(Id, RawHash, RawSize);

	if (CompressedData)
	{
		if (EnumHasAllFlags(PayloadPolicy, SkipFlag) && !bAlwaysLoadInlineData)
		{
			return Payload;
		}
		else
		{
			return LegacyValidateCachePayload(Key, Context, Payload, CompressedHash, MoveTemp(CompressedData));
		}
	}

	return LegacyGetCachePayload(Key, Context, PayloadPolicy, Payload);
}

FPayload FZenDerivedDataBackend::LegacyValidateCachePayload(const FCacheKey& Key, FStringView Context,
	const FPayload& Payload, const FIoHash& CompressedHash, FSharedBuffer&& CompressedData) const
{
	// TODO: Temporary function copied from FFileSystemDerivedDataBackend, helper for CreateRecord
	if (FCompressedBuffer CompressedBuffer = FCompressedBuffer::FromCompressed(MoveTemp(CompressedData));
		CompressedBuffer &&
		CompressedBuffer.GetRawHash() == Payload.GetRawHash() &&
		FIoHash::HashBuffer(CompressedBuffer.GetCompressed()) == CompressedHash)
	{
		return FPayload(Payload.GetId(), MoveTemp(CompressedBuffer));
	}
	UE_LOG(LogDerivedDataCache, Display,
		TEXT("%s: Cache miss with corrupted payload %s with hash %s for %s from '%.*s'"),
		*GetName(), *WriteToString<16>(Payload.GetId()), *WriteToString<48>(Payload.GetRawHash()),
		*WriteToString<96>(Key), Context.Len(), Context.GetData());
	return FPayload();
}

void FZenDerivedDataBackend::LegacyMakeZenKey(const FCacheKey& CacheKey, FStringBuilderBase& Out) const
{
	Out.Reset();
	Out << TEXT("/z$/") << CacheKey;
}

void FZenDerivedDataBackend::LegacyMakePayloadKey(const FCacheKey& CacheKey, const FIoHash& RawHash, FStringBuilderBase& Out) const
{
	Out.Reset();
	Out << TEXT("/z$/") << CacheKey << TEXT('/') << RawHash;
}


} // namespace UE::DerivedData::Backends

#endif //WITH_HTTP_DDC_BACKEND

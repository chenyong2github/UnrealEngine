// Copyright Epic Games, Inc. All Rights Reserved.
#include "ZenDerivedDataBackend.h"

#if WITH_ZEN_DDC_BACKEND

#include "Algo/Accumulate.h"
#include "DerivedDataCacheRecord.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/ScopeLock.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Serialization/CompactBinary.h"
#include "Serialization/CompactBinaryPackage.h"
#include "Serialization/CompactBinaryValidation.h"
#include "Serialization/CompactBinaryWriter.h"
#include "ZenBackendUtils.h"
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
	ICacheFactory& InFactory, 
	const TCHAR* InServiceUrl,
	const TCHAR* InNamespace)
	: Factory(InFactory)
	, Domain(InServiceUrl)
	, Namespace(InNamespace)
{
	if (IsServiceReady())
	{
		RequestPool = MakeUnique<Zen::FRequestPool>(InServiceUrl);
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
	return Domain;
}


bool FZenDerivedDataBackend::IsServiceReady()
{
	Zen::FZenHttpRequest Request(*Domain, false);
	Zen::FZenHttpRequest::Result Result = Request.PerformBlockingDownload(TEXT("health/ready"), nullptr);
	
	if (Result == Zen::FZenHttpRequest::Result::Success && Zen::IsSuccessCode(Request.GetResponseCode()))
	{
		UE_LOG(LogDerivedDataCache, Display, TEXT("Z$ HTTP DDC service status: %s."), *Request.GetResponseAsString());
		return true;
	}
	else
	{
		UE_LOG(LogDerivedDataCache, Warning, TEXT("Unable to reach Z$ HTTP DDC service at %s. Status: %d . Response: %s"), *Domain, Request.GetResponseCode(), *Request.GetResponseAsString());
	}

	return false;
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
		Zen::FScopedRequestPtr Request(RequestPool.Get());
		Zen::FZenHttpRequest::Result Result = Request->PerformBlockingHead(*Uri);
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

	EGetResult Result = GetZenData(*MakeLegacyZenKey(CacheKey), &OutData);
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
FZenDerivedDataBackend::GetZenData(const TCHAR* Uri, TArray<uint8>* OutData) const
{
	// Retry request until we get an accepted response or exhaust allowed number of attempts.
	EGetResult GetResult = EGetResult::NotFound;
	for (uint32 Attempts = 0; Attempts < MaxAttempts; ++Attempts)
	{
		Zen::FScopedRequestPtr Request(RequestPool.Get());
		if (Request.IsValid())
		{
			Zen::FZenHttpRequest::Result Result = Request->PerformBlockingDownload(Uri, OutData);
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
		Zen::FScopedRequestPtr Request(RequestPool.Get());
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
	if (DidSimulateMiss(CacheKey))
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
		Zen::FScopedRequestPtr Request(RequestPool.Get());
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
	FStringView BucketStr = CacheKey.Bucket.ToString<TCHAR>();
	Out.Appendf(TEXT("/z$/%.*s/"), BucketStr.Len(), BucketStr.GetData());
	Out << CacheKey.Hash;
}

void FZenDerivedDataBackend::AppendZenUri(const FCacheKey& CacheKey, const FPayloadId& PayloadId, FStringBuilderBase& Out)
{
	AppendZenUri(CacheKey, Out);
	Out << TEXT("/");
	UE::String::BytesToHexLower(PayloadId.GetBytes(), Out);
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
		Zen::FScopedRequestPtr Request(RequestPool.Get());
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
	return MakeShared<FDerivedDataCacheStatsNode>(this, "foo");
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

bool FZenDerivedDataBackend::DidSimulateMiss(const TCHAR* InKey)
{
	if (DebugOptions.RandomMissRate == 0 || DebugOptions.SimulateMissTypes.Num() == 0)
	{
		return false;
	}
	FScopeLock Lock(&MissedKeysCS);
	return DebugMissedKeys.Contains(FName(InKey));
}

bool FZenDerivedDataBackend::ShouldSimulateMiss(const TCHAR* InKey)
{
	// once missed, always missed
	if (DidSimulateMiss(InKey))
	{
		return true;
	}

	if (DebugOptions.ShouldSimulateMiss(InKey))
	{
		FScopeLock Lock(&MissedKeysCS);
		UE_LOG(LogDerivedDataCache, Verbose, TEXT("Simulating miss in %s for %s"), *GetName(), InKey);
		DebugMissedKeys.Add(FName(InKey));
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
		if (bCacheRecordEndpointEnabled)
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
	ECachePolicy Policy,
	IRequestOwner& Owner,
	FOnCacheGetComplete&& OnComplete)
{
	for (const FCacheKey& Key : Keys)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ZenDDC_Get);
		TRACE_COUNTER_ADD(ZenDDC_Get, int64(1));
		COOK_STAT(auto Timer = UsageStats.TimeGet());

		FOptionalCacheRecord Record;
		if (bCacheRecordEndpointEnabled)
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
				OnComplete({ Factory.CreateRecord(Key).Build(), EStatus::Error });
			}
		}
	}
}

void FZenDerivedDataBackend::GetPayload(
	TConstArrayView<FCachePayloadKey> Keys,
	FStringView Context,
	ECachePolicy Policy,
	IRequestOwner& Owner,
	FOnCacheGetPayloadComplete&& OnComplete)
{
	if (bCacheRecordEndpointEnabled)
	{
		for (const FCachePayloadKey& Key : Keys)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(ZenDDC_Get);
			TRACE_COUNTER_ADD(ZenDDC_Get, int64(1));
			COOK_STAT(auto Timer = UsageStats.TimeGet());

			TStringBuilder<256> QueryUri;
			AppendZenUri(Key.CacheKey, Key.Id, QueryUri);
			AppendPolicyQueryString(Policy, QueryUri);

			TArray<uint8> PayloadData;
			EGetResult GetResult = GetZenData(QueryUri.ToString(), &PayloadData);
			FCompressedBuffer CompressedBuffer;
			if (GetResult == EGetResult::Success)
			{
				CompressedBuffer = FCompressedBuffer::FromCompressed(MakeSharedBufferFromArray(MoveTemp(PayloadData)));
			}
			if (CompressedBuffer)
			{
				FPayload Payload = FPayload(Key.Id, MoveTemp(CompressedBuffer));
				UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Cache hit for %s from '%.*s'"),
					*GetName(), *WriteToString<96>(Key), Context.Len(), Context.GetData());
				TRACE_COUNTER_ADD(ZenDDC_GetHit, int64(1));
				TRACE_COUNTER_ADD(ZenDDC_BytesReceived, Payload.GetRawSize());
				COOK_STAT(Timer.AddHit(Payload.GetRawSize()));
				if (OnComplete)
				{
					OnComplete({ Key.CacheKey, MoveTemp(Payload), EStatus::Ok });
				}
			}
			else
			{
				UE_LOG(LogDerivedDataCache, Display, TEXT("%s: Cache miss with %s payload %s for %s from '%.*s'"),
					*GetName(), (GetResult == EGetResult::NotFound ? TEXT("missing") : TEXT("corrupted")),
					*WriteToString<16>(Key.Id), *WriteToString<96>(Key), Context.Len(), Context.GetData());
				if (OnComplete)
				{
					OnComplete({ Key.CacheKey, FPayload(Key.Id), EStatus::Error });
				}
			}
		}
	}
	else
	{
		// We have to load the CacheRecord for each Payload, so sort all the Payloads sharing
		// the same key to occur together and we can load the CacheRecord at the start of each run of Payloads
		TArray<FCachePayloadKey, TInlineAllocator<16>> SortedKeys(Keys);
		SortedKeys.StableSort();

		FOptionalCacheRecord Record;
		for (const FCachePayloadKey& Key : SortedKeys)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(ZenDDC_Get);
			TRACE_COUNTER_ADD(ZenDDC_Get, int64(1));
			COOK_STAT(auto Timer = UsageStats.TimeGet());

			if (!Record || Record.Get().GetKey() != Key.CacheKey)
			{
				Record = LegacyGetCacheRecord(Key.CacheKey, Context, Policy | ECachePolicy::SkipData, /*bAlwaysLoadInlineData*/ true);
			}
			if (FPayload Payload = Record ? LegacyGetCachePayload(Key.CacheKey, Context, Policy, Record.Get().GetPayload(Key.Id)) : FPayload::Null)
			{
				UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Cache hit for %s from '%.*s'"),
					*GetName(), *WriteToString<96>(Key), Context.Len(), Context.GetData());
				TRACE_COUNTER_ADD(ZenDDC_GetHit, int64(1));
				TRACE_COUNTER_ADD(ZenDDC_BytesReceived, Payload.GetRawSize());
				COOK_STAT(Timer.AddHit(Payload.GetRawSize()));
				if (OnComplete)
				{
					OnComplete({ Key.CacheKey, MoveTemp(Payload), EStatus::Ok });
				}
			}
			else
			{
				if (OnComplete)
				{
					OnComplete({ Key.CacheKey, FPayload(Key.Id), EStatus::Error });
				}
			}
		}
	}
}

void FZenDerivedDataBackend::CancelAll()
{
}

bool FZenDerivedDataBackend::PutCacheRecord(const FCacheRecord& Record, FStringView Context, ECachePolicy Policy)
{
	const FCacheKey& Key = Record.GetKey();
	FCbPackage Package = Factory.SaveRecord(Record);
	FCbWriter PackageWriter;
	Package.Save(PackageWriter);
	FUniqueBuffer Buffer = FUniqueBuffer::Alloc(PackageWriter.GetSaveSize());
	PackageWriter.Save(Buffer);
	TStringBuilder<256> Uri;
	AppendZenUri(Record.GetKey(), Uri);
	AppendPolicyQueryString(Policy, Uri);
	if (PutZenData(Uri.ToString(), FCompositeBuffer(Buffer.MoveToShared()), Zen::EContentType::CompactBinaryPackage)
		!= FDerivedDataBackendInterface::EPutStatus::Cached)
	{
		return false;
	}

	return true;
}

FOptionalCacheRecord FZenDerivedDataBackend::GetCacheRecord(const FCacheKey& Key, FStringView Context,
	ECachePolicy Policy) const
{
	FCbPackage Package;
	EGetResult GetResult = GetZenData(Key, Policy, Package);
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
	FOptionalCacheRecord Record = Factory.LoadRecord(Package);
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
	if (PutZenData(Uri.ToString(), FCompositeBuffer(Buffer.MoveToShared()), Zen::EContentType::CompactBinary)
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

	const bool bStoreInline = !Payload.HasData();
	if (!bStoreInline)
	{
		TStringBuilder<256> Uri;
		LegacyMakePayloadKey(Key, RawHash, Uri);
		if (PutZenData(Uri.ToString(), CompressedBuffer, Zen::EContentType::Binary) != FDerivedDataBackendInterface::EPutStatus::Cached)
		{
			return false;
		}
	}

	Writer.BeginObject();
	Writer.AddObjectId("Id"_ASV, Payload.GetId());
	Writer.AddInteger("RawSize"_ASV, Payload.GetRawSize());
	if (bStoreInline)
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

FOptionalCacheRecord FZenDerivedDataBackend::LegacyGetCacheRecord(const FCacheKey& Key,
	FStringView Context, ECachePolicy Policy, bool bAlwaysLoadInlineData) const
{
	TStringBuilder<256> Uri;
	LegacyMakeZenKey(Key, Uri);
	TArray<uint8> Buffer;
	EGetResult Result = GetZenData(Uri.ToString(), &Buffer);
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
	TArray<uint8> ArrayBuffer;
	GetResult = GetZenData(Uri.ToString(), bSkipData ? nullptr : &ArrayBuffer);
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

FOptionalCacheRecord FZenDerivedDataBackend::LegacyCreateRecord(FSharedBuffer&& RecordBytes, const FCacheKey& Key,
	FStringView Context, ECachePolicy Policy, bool bAlwaysLoadInlineData) const
{
	// TODO: Temporary function copied from FFileSystemDerivedDataBackend.
	// Eventually We will instead send the Policy to zen sever along with the request for the FCacheKey,
	// and it will end back all payloads in the response rather than requiring separate requests.
	const FCbObject RecordObject(MoveTemp(RecordBytes));
	FCacheRecordBuilder RecordBuilder = Factory.CreateRecord(Key);

	if (!EnumHasAnyFlags(Policy, ECachePolicy::SkipMeta))
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
		const ECachePolicy ValuePolicy = Policy | (ECachePolicy::SkipData & ~ECachePolicy::SkipValue);
		FPayload Payload = LegacyGetCachePayload(Key, Context, ValuePolicy, ValueObject, bAlwaysLoadInlineData);
		if (Payload.IsNull())
		{
			return FOptionalCacheRecord();
		}
		RecordBuilder.SetValue(MoveTemp(Payload));
	}

	for (FCbField AttachmentField : RecordObject["Attachments"_ASV])
	{
		const ECachePolicy AttachmentsPolicy = Policy | (ECachePolicy::SkipData & ~ECachePolicy::SkipAttachments);
		FPayload Payload = LegacyGetCachePayload(Key, Context, AttachmentsPolicy, AttachmentField.AsObject(),
			bAlwaysLoadInlineData);
		if (Payload.IsNull())
		{
			return FOptionalCacheRecord();
		}
		RecordBuilder.AddAttachment(MoveTemp(Payload));
	}

	return RecordBuilder.Build();
}

FPayload FZenDerivedDataBackend::LegacyGetCachePayload(const FCacheKey& Key, FStringView Context, ECachePolicy Policy,
	const FCbObject& Object, bool bAlwaysLoadInlineData) const
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

	FPayload Payload(Id, RawHash, RawSize);

	if (CompressedData)
	{
		if (EnumHasAllFlags(Policy, ECachePolicy::SkipData) && !bAlwaysLoadInlineData)
		{
			return Payload;
		}
		else
		{
			return LegacyValidateCachePayload(Key, Context, Payload, CompressedHash, MoveTemp(CompressedData));
		}
	}

	return LegacyGetCachePayload(Key, Context, Policy, Payload);
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
	FStringView Bucket = CacheKey.Bucket.ToString<TCHAR>();
	Out.Appendf(TEXT("/z$/%.*s/%s"), Bucket.Len(), Bucket.GetData(), *LexToString(CacheKey.Hash));
}

void FZenDerivedDataBackend::LegacyMakePayloadKey(const FCacheKey& CacheKey, const FIoHash& RawHash, FStringBuilderBase& Out) const
{
	Out.Reset();
	FStringView Bucket = CacheKey.Bucket.ToString<TCHAR>();
	Out.Appendf(TEXT("/z$/%.*s/%s/%s"), Bucket.Len(), Bucket.GetData(), *LexToString(CacheKey.Hash), *LexToString(RawHash));
}


} // namespace UE::DerivedData::Backends

#endif //WITH_HTTP_DDC_BACKEND

// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataBackendInterface.h"
#include "DerivedDataLegacyCacheStore.h"
#include "Templates/Tuple.h"

#if WITH_HTTP_DDC_BACKEND

#include "Compression/CompressedBuffer.h"
#include "Containers/StringView.h"
#include "Containers/Ticker.h"
#include "DerivedDataCacheKey.h"
#include "DerivedDataCacheRecord.h"
#include "DerivedDataCacheUsageStats.h"
#include "DerivedDataChunk.h"
#include "DerivedDataRequestOwner.h"
#include "DerivedDataValue.h"
#include "DesktopPlatformModule.h"
#include "Dom/JsonObject.h"
#include "HAL/IConsoleManager.h"
#include "Http/HttpClient.h"
#include "IO/IoHash.h"
#include "Memory/SharedBuffer.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopeLock.h"
#include "Misc/StringBuilder.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Serialization/CompactBinary.h"
#include "Serialization/CompactBinaryPackage.h"
#include "Serialization/CompactBinaryValidation.h"
#include "Serialization/CompactBinaryWriter.h"

#if PLATFORM_MICROSOFT
#include "Microsoft/WindowsHWrapper.h"
#include "Microsoft/AllowMicrosoftPlatformTypes.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include "Microsoft/HideMicrosoftPlatformTypes.h"
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#endif

#define UE_HTTPDDC_GET_REQUEST_POOL_SIZE 48
#define UE_HTTPDDC_PUT_REQUEST_POOL_SIZE 16
#define UE_HTTPDDC_NONBLOCKING_REQUEST_POOL_SIZE 128
#define UE_HTTPDDC_MAX_FAILED_LOGIN_ATTEMPTS 16
#define UE_HTTPDDC_MAX_ATTEMPTS 4

namespace UE::DerivedData
{

static bool bHttpEnableOidc = false;
static FAutoConsoleVariableRef CVarHttpEnableOidc(
	TEXT("DDC.Http.EnableOidc"),
	bHttpEnableOidc,
	TEXT("If true, Oidc tokens are used, otherwise legacy shared credentials are used."),
	ECVF_Default);

TRACE_DECLARE_INT_COUNTER(HttpDDC_Get, TEXT("HttpDDC Get"));
TRACE_DECLARE_INT_COUNTER(HttpDDC_GetHit, TEXT("HttpDDC Get Hit"));
TRACE_DECLARE_INT_COUNTER(HttpDDC_Put, TEXT("HttpDDC Put"));
TRACE_DECLARE_INT_COUNTER(HttpDDC_PutHit, TEXT("HttpDDC Put Hit"));
TRACE_DECLARE_INT_COUNTER(HttpDDC_BytesReceived, TEXT("HttpDDC Bytes Received"));
TRACE_DECLARE_INT_COUNTER(HttpDDC_BytesSent, TEXT("HttpDDC Bytes Sent"));


template <typename T>
class TRefCountedUniqueFunction final : public FThreadSafeRefCountedObject
{
public:
	explicit TRefCountedUniqueFunction(T&& InFunction) : Function(MoveTemp(InFunction))
	{
	}

	const T& GetFunction() const { return Function; }

private:
	T Function;
};


static bool ShouldAbortForShutdown()
{
	return !GIsBuildMachine && FDerivedDataBackend::Get().IsShuttingDown();
}

static bool IsValueDataReady(FValue& Value, const ECachePolicy Policy)
{
	if (!EnumHasAnyFlags(Policy, ECachePolicy::Query))
	{
		Value = Value.RemoveData();
		return true;
	}

	if (Value.HasData())
	{
		if (EnumHasAnyFlags(Policy, ECachePolicy::SkipData))
		{
			Value = Value.RemoveData();
		}
		return true;
	}
	return false;
};

struct FHttpCacheStoreParams
{
	FString Host;
	FString Namespace;
	FString StructuredNamespace;
	FString OAuthProvider;
	FString OAuthClientId;
	FString OAuthSecret;
	FString OAuthScope;
	FString OAuthProviderIdentifier;
	FString OAuthAccessToken;
	bool bResolveHostCanonicalName = true;
	bool bReadOnly = false;

	void Parse(const TCHAR* NodeName, const TCHAR* Config);
};

//----------------------------------------------------------------------------------------------------------
// FHttpCacheStore
//----------------------------------------------------------------------------------------------------------

/**
 * Backend for a HTTP based caching service (Jupiter).
 */
class FHttpCacheStore final : public ILegacyCacheStore
{
public:
	
	/**
	 * Creates the cache store client, checks health status and attempts to acquire an access token.
	 */
	explicit FHttpCacheStore(const FHttpCacheStoreParams& Params);

	~FHttpCacheStore();

	/**
	 * Checks is cache service is usable (reachable and accessible).
	 * @return true if usable
	 */
	inline bool IsUsable() const { return bIsUsable; }

	void Put(
		TConstArrayView<FCachePutRequest> Requests,
		IRequestOwner& Owner,
		FOnCachePutComplete&& OnComplete) final;
	void Get(
		TConstArrayView<FCacheGetRequest> Requests,
		IRequestOwner& Owner,
		FOnCacheGetComplete&& OnComplete) final;
	void PutValue(
		TConstArrayView<FCachePutValueRequest> Requests,
		IRequestOwner& Owner,
		FOnCachePutValueComplete&& OnComplete) final;
	void GetValue(
		TConstArrayView<FCacheGetValueRequest> Requests,
		IRequestOwner& Owner,
		FOnCacheGetValueComplete&& OnComplete) final;
	void GetChunks(
		TConstArrayView<FCacheGetChunkRequest> Requests,
		IRequestOwner& Owner,
		FOnCacheGetChunkComplete&& OnComplete) final;

	void LegacyStats(FDerivedDataCacheStatsNode& OutNode) final;
	bool LegacyDebugOptions(FBackendDebugOptions& Options) final;

	static FHttpCacheStore* GetAny()
	{
		return AnyInstance;
	}

	const FString& GetDomain() const { return Domain; }
	const FString& GetNamespace() const { return Namespace; }
	const FString& GetStructuredNamespace() const { return StructuredNamespace; }
	const FString& GetOAuthProvider() const { return OAuthProvider; }
	const FString& GetOAuthClientId() const { return OAuthClientId; }
	const FString& GetOAuthSecret() const { return OAuthSecret; }
	const FString& GetOAuthScope() const { return OAuthScope; }
	const FString& GetOAuthProviderIdentifier() const { return OAuthProviderIdentifier; }
	const FString& GetOAuthAccessToken() const { return OAuthAccessToken; }

private:
	FString Domain;
	FString EffectiveDomain;
	FString Namespace;
	FString StructuredNamespace;
	FString DefaultBucket;
	FString OAuthProvider;
	FString OAuthClientId;
	FString OAuthSecret;
	FString OAuthScope;
	FString OAuthProviderIdentifier;
	FString OAuthAccessToken;

	FCriticalSection AccessCs;
	FDerivedDataCacheUsageStats UsageStats;
	FBackendDebugOptions DebugOptions;
	TUniquePtr<FHttpSharedData> SharedData;
	TUniquePtr<FHttpRequestPool> GetRequestPools[2];
	TUniquePtr<FHttpRequestPool> PutRequestPools[2];
	TUniquePtr<FHttpRequestPool> NonBlockingRequestPools;
	TUniquePtr<FHttpAccessToken> Access;
	bool bIsUsable;
	bool bReadOnly;
	uint32 FailedLoginAttempts;
	static inline FHttpCacheStore* AnyInstance = nullptr;

	bool IsServiceReady();
	bool AcquireAccessToken();
	bool ShouldRetryOnError(FHttpRequest::EResult Result, int64 ResponseCode);
	bool ShouldRetryOnError(int64 ResponseCode) { return ShouldRetryOnError(FHttpRequest::EResult::Success, ResponseCode); }

	enum class OperationCategory
	{
		Get,
		Put,
	};
	template <OperationCategory Category>
	FHttpRequest* WaitForHttpRequestForOwner(IRequestOwner& Owner, bool bUnboundedOverflow, FHttpRequestPool*& OutPool)
	{
		if (!FHttpRequest::AllowAsync())
		{
			if (Category == OperationCategory::Get)
			{
				OutPool = GetRequestPools[IsInGameThread()].Get();
			}
			else
			{
				OutPool = PutRequestPools[IsInGameThread()].Get();
			}
			return OutPool->WaitForFreeRequest();
		}
		else
		{
			OutPool = NonBlockingRequestPools.Get();
			return OutPool->WaitForFreeRequest(bUnboundedOverflow);
		}
	}

	struct FGetCacheRecordOnlyResponse
	{
		FSharedString Name;
		FCacheKey Key;
		uint64 UserData = 0;
		uint64 BytesReceived = 0;
		FOptionalCacheRecord Record;
		EStatus Status = EStatus::Error;
	};
	using FOnGetCacheRecordOnlyComplete = TUniqueFunction<void(FGetCacheRecordOnlyResponse&& Response)>;
	void GetCacheRecordOnlyAsync(
		IRequestOwner& Owner,
		const FSharedString& Name,
		const FCacheKey& Key,
		const FCacheRecordPolicy& Policy,
		uint64 UserData,
		FOnGetCacheRecordOnlyComplete&& OnComplete);

	void GetCacheRecordAsync(
		IRequestOwner& Owner,
		const FSharedString& Name,
		const FCacheKey& Key,
		const FCacheRecordPolicy& Policy,
		uint64 UserData,
		TUniqueFunction<void(FCacheGetResponse&& Response, uint64 BytesReceived)>&& OnComplete);

	void PutCacheRecordAsync(
		IRequestOwner& Owner,
		const FSharedString& Name,
		const FCacheRecord& Record,
		const FCacheRecordPolicy& Policy,
		uint64 UserData,
		TUniqueFunction<void(FCachePutResponse&& Response, uint64 BytesSent)>&& OnComplete);

	void PutCacheValueAsync(
		IRequestOwner& Owner,
		const FSharedString& Name,
		const FCacheKey& Key,
		const FValue& Value,
		ECachePolicy Policy,
		uint64 UserData,
		TUniqueFunction<void(FCachePutValueResponse&& Response, uint64 BytesSent)>&& OnComplete);

	void GetCacheValueAsync(
		IRequestOwner& Owner,
		FSharedString Name,
		const FCacheKey& Key,
		ECachePolicy Policy,
		uint64 UserData,
		FOnCacheGetValueComplete&& OnComplete);

	void RefCachedDataProbablyExistsBatchAsync(
		IRequestOwner& Owner,
		TConstArrayView<FCacheGetValueRequest> ValueRefs,
		FOnCacheGetValueComplete&& OnComplete);

	class FPutPackageOp;
	class FGetRecordOp;
};

//----------------------------------------------------------------------------------------------------------
// FHttpCacheStore::FPutPackageOp
//----------------------------------------------------------------------------------------------------------
class FHttpCacheStore::FPutPackageOp final : public FThreadSafeRefCountedObject
{
public:

	struct FCachePutPackageResponse
	{
		FSharedString Name;
		FCacheKey Key;
		uint64 UserData = 0;
		uint64 BytesSent = 0;
		EStatus Status = EStatus::Error;
	};
	using FOnCachePutPackageComplete = TUniqueFunction<void(FCachePutPackageResponse&& Response)>;

	/** Performs a multi-request operation for uploading a package of content. */
	static void PutPackage(
		FHttpCacheStore& CacheStore,
		IRequestOwner& Owner,
		const FSharedString& Name,
		FCacheKey Key,
		FCbPackage&& Package,
		FCacheRecordPolicy Policy,
		uint64 UserData,
		FOnCachePutPackageComplete&& OnComplete);

private:
	FHttpCacheStore& CacheStore;
	IRequestOwner& Owner;
	const FSharedString Name;
	const FCacheKey Key;
	const uint64 UserData;
	std::atomic<uint64> BytesSent;
	const FCbObject PackageObject;
	const FIoHash PackageObjectHash;
	const uint32 TotalBlobUploads;
	std::atomic<uint32> SuccessfulBlobUploads;
	std::atomic<uint32> PendingBlobUploads;
	FOnCachePutPackageComplete OnComplete;

	struct FCachePutRefResponse
	{
		FSharedString Name;
		FCacheKey Key;
		uint64 UserData = 0;
		uint64 BytesSent = 0;
		TConstArrayView<FIoHash> NeededBlobHashes;
		EStatus Status = EStatus::Error;
	};
	using FOnCachePutRefComplete = TUniqueFunction<void(FCachePutRefResponse&& Response)>;

	FPutPackageOp(
		FHttpCacheStore& InCacheStore,
		IRequestOwner& InOwner,
		const FSharedString& InName,
		const FCacheKey& InKey,
		uint64 InUserData,
		uint64 InBytesSent,
		const FCbObject& InPackageObject,
		const FIoHash& InPackageObjectHash,
		uint32 InTotalBlobUploads,
		FOnCachePutPackageComplete&& InOnComplete);

	static void PutRefAsync(
		FHttpCacheStore& CacheStore,
		IRequestOwner& Owner,
		const FSharedString& Name,
		FCacheKey Key,
		FCbObject Object,
		FIoHash ObjectHash,
		uint64 UserData,
		bool bFinalize,
		FOnCachePutRefComplete&& OnComplete);

	static void OnPackagePutRefComplete(
		FHttpCacheStore& CacheStore,
		IRequestOwner& Owner,
		const FSharedString& Name,
		const FCacheKey& Key,
		FCbPackage&& Package,
		FCacheRecordPolicy Policy,
		uint64 UserData,
		FOnCachePutPackageComplete&& OnComplete,
		FCachePutRefResponse&& Response);

	FHttpRequest::ECompletionBehavior OnCompressedBlobUploadComplete(
		FHttpRequest::EResult HttpResult,
		FHttpRequest* Request);

	void OnPutRefFinalizationComplete(
		FCachePutRefResponse&& Response);

	FCachePutPackageResponse MakeResponse(uint64 InBytesSent, EStatus Status)
	{
		return FCachePutPackageResponse{ Name, Key, UserData, InBytesSent, Status };
	};
};

//----------------------------------------------------------------------------------------------------------
// FHttpCacheStore::FGetRecordOp
//----------------------------------------------------------------------------------------------------------
class FHttpCacheStore::FGetRecordOp final : public FThreadSafeRefCountedObject
{
public:
	/** Performs a multi-request operation for downloading a record. */
	static void GetRecord(
		FHttpCacheStore& CacheStore,
		IRequestOwner& Owner,
		const FSharedString& Name,
		const FCacheKey& Key,
		const FCacheRecordPolicy& Policy,
		uint64 UserData,
		TUniqueFunction<void(FCacheGetResponse&& Response, uint64 BytesReceived)>&& OnComplete);

	struct FGetCachedDataBatchResponse
	{
		FSharedString Name;
		FCacheKey Key;
		int32 ValueIndex;
		uint64 BytesReceived = 0;
		FCompressedBuffer DataBuffer;
		EStatus Status = EStatus::Error;
	};
	using FOnGetCachedDataBatchComplete = TUniqueFunction<void(FGetCachedDataBatchResponse&& Response)>;

	/** Utility method for fetching a batch of value data. */
	template <typename ValueType, typename ValueIdGetterType>
	static void GetDataBatch(
		FHttpCacheStore& CacheStore,
		IRequestOwner& Owner,
		FSharedString Name,
		const FCacheKey& Key,
		TConstArrayView<ValueType> Values,
		ValueIdGetterType ValueIdGetter,
		FOnGetCachedDataBatchComplete&& OnComplete);
private:
	FHttpCacheStore& CacheStore;
	IRequestOwner& Owner;
	const FSharedString Name;
	const FCacheKey Key;
	const uint64 UserData;
	std::atomic<uint64> BytesReceived;
	TArray<FCompressedBuffer> FetchedBuffers;
	const TArray<FValueWithId> RequiredGets;
	const TArray<FValueWithId> RequiredHeads;
	FCacheRecordBuilder RecordBuilder;
	const uint32 TotalOperations;
	std::atomic<uint32> SuccessfulOperations;
	std::atomic<uint32> PendingOperations;
	TUniqueFunction<void(FCacheGetResponse&& Response, uint64 BytesReceived)> OnComplete;

	FGetRecordOp(
		FHttpCacheStore& InCacheStore,
		IRequestOwner& InOwner,
		const FSharedString& InName,
		const FCacheKey& InKey,
		uint64 InUserData,
		uint64 InBytesReceived,
		TArray<FValueWithId>&& InRequiredGets,
		TArray<FValueWithId>&& InRequiredHeads,
		FCacheRecordBuilder&& InRecordBuilder,
		TUniqueFunction<void(FCacheGetResponse&& Response, uint64 BytesReceived)>&& InOnComplete);

	static void OnOnlyRecordComplete(
		FHttpCacheStore& CacheStore,
		IRequestOwner& Owner,
		const FCacheRecordPolicy& Policy,
		TUniqueFunction<void(FCacheGetResponse&& Response, uint64 BytesReceived)>&& OnComplete,
		FGetCacheRecordOnlyResponse&& Response);

	struct FCachedDataProbablyExistsBatchResponse
	{
		FSharedString Name;
		FCacheKey Key;
		int32 ValueIndex;
		EStatus Status = EStatus::Error;
	};
	using FOnCachedDataProbablyExistsBatchComplete = TUniqueFunction<void(FCachedDataProbablyExistsBatchResponse&& Response)>;
	void DataProbablyExistsBatch(
		TConstArrayView<FValueWithId> Values,
		FOnCachedDataProbablyExistsBatchComplete&& OnComplete);

	void FinishDataStep(bool bSuccess, uint64 InBytesReceived);
};

void FHttpCacheStore::FPutPackageOp::PutPackage(
	FHttpCacheStore& CacheStore,
	IRequestOwner& Owner,
	const FSharedString& Name,
	FCacheKey Key,
	FCbPackage&& Package,
	FCacheRecordPolicy Policy,
	uint64 UserData,
	FOnCachePutPackageComplete&& OnComplete)
{
	// TODO: Jupiter currently always overwrites.  It doesn't have a "write if not present" feature (for records or attachments),
	//		 but would require one to implement all policy correctly.

	// Initial record upload
	PutRefAsync(CacheStore, Owner, Name, Key, Package.GetObject(), Package.GetObjectHash(), UserData, false,
		[&CacheStore, &Owner, Name = FSharedString(Name), Key, Package = MoveTemp(Package), Policy, UserData, OnComplete = MoveTemp(OnComplete)](FCachePutRefResponse&& Response) mutable
		{
			return OnPackagePutRefComplete(CacheStore, Owner, Name, Key, MoveTemp(Package), Policy, UserData, MoveTemp(OnComplete), MoveTemp(Response));
		});
}

FHttpCacheStore::FPutPackageOp::FPutPackageOp(
	FHttpCacheStore& InCacheStore,
	IRequestOwner& InOwner,
	const FSharedString& InName,
	const FCacheKey& InKey,
	uint64 InUserData,
	uint64 InBytesSent,
	const FCbObject& InPackageObject,
	const FIoHash& InPackageObjectHash,
	uint32 InTotalBlobUploads,
	FOnCachePutPackageComplete&& InOnComplete)
	: CacheStore(InCacheStore)
	, Owner(InOwner)
	, Name(InName)
	, Key(InKey)
	, UserData(InUserData)
	, BytesSent(InBytesSent)
	, PackageObject(InPackageObject)
	, PackageObjectHash(InPackageObjectHash)
	, TotalBlobUploads(InTotalBlobUploads)
	, SuccessfulBlobUploads(0)
	, PendingBlobUploads(InTotalBlobUploads)
	, OnComplete(MoveTemp(InOnComplete))
{
}

void FHttpCacheStore::FPutPackageOp::PutRefAsync(
	FHttpCacheStore& CacheStore,
	IRequestOwner& Owner,
	const FSharedString& Name,
	FCacheKey Key,
	FCbObject Object,
	FIoHash ObjectHash,
	uint64 UserData,
	bool bFinalize,
	FOnCachePutRefComplete&& OnComplete)
{
	FString Bucket(Key.Bucket.ToString());
	Bucket.ToLowerInline();

	TStringBuilder<256> RefsUri;
	RefsUri << "api/v1/refs/" << CacheStore.StructuredNamespace << "/" << Bucket << "/" << Key.Hash;
	if (bFinalize)
	{
		RefsUri << "/finalize/" << ObjectHash;
	}

	FHttpRequestPool* Pool = nullptr;
	FHttpRequest* Request = CacheStore.WaitForHttpRequestForOwner<OperationCategory::Put>(Owner, bFinalize /* bUnboundedOverflow */, Pool);

	auto OnHttpRequestComplete = [&CacheStore, &Owner, Name = FSharedString(Name), Key, Object, UserData, bFinalize, OnComplete = MoveTemp(OnComplete)]
	(FHttpRequest::EResult HttpResult, FHttpRequest* Request)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(HttpDDC_PutRefAsync_OnHttpRequestComplete);

		if (Owner.IsCanceled())
		{
			OnComplete({ Name, Key, UserData, Request->GetBytesSent(), {}, EStatus::Canceled });
			return FHttpRequest::ECompletionBehavior::Done;
		}

		int64 ResponseCode = Request->GetResponseCode();
		if (FHttpRequest::IsSuccessResponse(ResponseCode))
		{
			TArray<FIoHash> NeededBlobHashes;

			// Useful when debugging issues related to compressed/uncompressed blobs being returned from Jupiter
			const bool bPutRefBlobsAlways = false;

			if (bPutRefBlobsAlways && !bFinalize)
			{
				Object.IterateAttachments([&NeededBlobHashes](FCbFieldView AttachmentFieldView)
				{
					FIoHash AttachmentHash = AttachmentFieldView.AsHash();
					if (!AttachmentHash.IsZero())
					{
						NeededBlobHashes.Add(AttachmentHash);
					}
				});
			}
			else if (TSharedPtr<FJsonObject> ResponseObject = Request->GetResponseAsJsonObject())
			{
				TArray<FString> NeedsArrayStrings;
				ResponseObject->TryGetStringArrayField(TEXT("needs"), NeedsArrayStrings);

				NeededBlobHashes.Reserve(NeedsArrayStrings.Num());
				for (const FString& NeededString : NeedsArrayStrings)
				{
					FIoHash BlobHash;
					LexFromString(BlobHash, *NeededString);
					if (!BlobHash.IsZero())
					{
						NeededBlobHashes.Add(BlobHash);
					}
				}
			}

			OnComplete({ Name, Key, UserData, Request->GetBytesSent(), NeededBlobHashes, EStatus::Ok });
			return FHttpRequest::ECompletionBehavior::Done;
		}

		if (!ShouldAbortForShutdown() && CacheStore.ShouldRetryOnError(HttpResult, ResponseCode) && ((Request->GetAttempts()+1) < UE_HTTPDDC_MAX_ATTEMPTS))
		{
			return FHttpRequest::ECompletionBehavior::Retry;
		}

		OnComplete({ Name, Key, UserData, Request->GetBytesSent(), {}, EStatus::Error });
		return FHttpRequest::ECompletionBehavior::Done;
	};

	if (bFinalize)
	{
		Request->EnqueueAsyncPost(Owner, Pool, *RefsUri, FCompositeBuffer(), MoveTemp(OnHttpRequestComplete), EHttpMediaType::FormUrlEncoded);
	}
	else
	{
		Request->AddHeader(TEXTVIEW("X-Jupiter-IoHash"), WriteToString<48>(ObjectHash));
		Request->EnqueueAsyncPut(Owner, Pool, *RefsUri, Object.GetBuffer(), MoveTemp(OnHttpRequestComplete), EHttpMediaType::CbObject);
	}
}

void FHttpCacheStore::FPutPackageOp::OnPackagePutRefComplete(
	FHttpCacheStore& CacheStore,
	IRequestOwner& Owner,
	const FSharedString& Name,
	const FCacheKey& Key,
	FCbPackage&& Package,
	FCacheRecordPolicy Policy,
	uint64 UserData,
	FOnCachePutPackageComplete&& OnComplete,
	FCachePutRefResponse&& Response)
{
	if (Response.Status != EStatus::Ok)
	{
		if (Response.Status == EStatus::Error)
		{
			UE_LOG(LogDerivedDataCache, Log, TEXT("%s: Failed to put reference object for put of %s from '%s'"),
				*CacheStore.Domain, *WriteToString<96>(Response.Key), *Response.Name);
		}
		return OnComplete(FCachePutPackageResponse{ Name, Key, UserData, Response.BytesSent, Response.Status });
	}

	struct FCompressedBlobUpload
	{
		FIoHash Hash;
		FSharedBuffer BlobBuffer;
		FCompressedBlobUpload(const FIoHash& InHash, FSharedBuffer&& InBlobBuffer) : Hash(InHash), BlobBuffer(InBlobBuffer)
		{
		}
	};

	TArray<FCompressedBlobUpload> CompressedBlobUploads;

	// TODO: blob uploading and finalization should be replaced with a single batch compressed blob upload endpoint in the future.
	TStringBuilder<128> ExpectedHashes;
	bool bExpectedHashesSerialized = false;

	// Needed blob upload (if any missing)
	for (const FIoHash& NeededBlobHash : Response.NeededBlobHashes)
	{
		if (const FCbAttachment* Attachment = Package.FindAttachment(NeededBlobHash))
		{
			FSharedBuffer TempBuffer;
			if (Attachment->IsCompressedBinary())
			{
				TempBuffer = Attachment->AsCompressedBinary().GetCompressed().ToShared();
			}
			else if (Attachment->IsBinary())
			{
				TempBuffer = FValue::Compress(Attachment->AsCompositeBinary()).GetData().GetCompressed().ToShared();
			}
			else
			{
				TempBuffer = FValue::Compress(Attachment->AsObject().GetBuffer()).GetData().GetCompressed().ToShared();
			}

			CompressedBlobUploads.Emplace(NeededBlobHash, MoveTemp(TempBuffer));
		}
		else
		{
			if (!bExpectedHashesSerialized)
			{
				bool bFirstHash = true;
				for (const FCbAttachment& PackageAttachment : Package.GetAttachments())
				{
					if (!bFirstHash)
					{
						ExpectedHashes << TEXT(", ");
					}
					ExpectedHashes << PackageAttachment.GetHash();
					bFirstHash = false;
				}
				bExpectedHashesSerialized = true;
			}
			UE_LOG(LogDerivedDataCache, Log, TEXT("%s: Server reported needed hash '%s' that is outside the set of expected hashes (%s) for put of %s from '%s'"),
				*CacheStore.Domain, *WriteToString<96>(NeededBlobHash), ExpectedHashes.ToString(), *WriteToString<96>(Response.Key), *Response.Name);
		}
	}

	if (CompressedBlobUploads.IsEmpty())
	{
		// No blobs need to be uploaded.  No finalization necessary.
		return OnComplete(FCachePutPackageResponse{ Name, Key, UserData, Response.BytesSent, EStatus::Ok });
	}

	// Having this be a ref ensures we don't have the op reach 0 ref count as we queue up multiple operations which MAY execute synchronously
	TRefCountPtr<FPutPackageOp> PutPackageOp = new FPutPackageOp(
		CacheStore,
		Owner,
		Response.Name,
		Response.Key,
		Response.UserData,
		Response.BytesSent,
		Package.GetObject(),
		Package.GetObjectHash(),
		(uint32)CompressedBlobUploads.Num(),
		MoveTemp(OnComplete)
	);

	FRequestBarrier Barrier(Owner);
	for (const FCompressedBlobUpload& CompressedBlobUpload : CompressedBlobUploads)
	{
		TStringBuilder<256> CompressedBlobsUri;
		CompressedBlobsUri << "api/v1/compressed-blobs/" << CacheStore.StructuredNamespace << "/" << CompressedBlobUpload.Hash;

		FHttpRequestPool* Pool = nullptr;
		FHttpRequest* Request = CacheStore.WaitForHttpRequestForOwner<OperationCategory::Put>(Owner, true /* bUnboundedOverflow */, Pool);
		Request->EnqueueAsyncPut(Owner, Pool, *CompressedBlobsUri, FCompositeBuffer(CompressedBlobUpload.BlobBuffer),
			[PutPackageOp](FHttpRequest::EResult HttpResult, FHttpRequest* Request)
			{
				return PutPackageOp->OnCompressedBlobUploadComplete(HttpResult, Request);
			},
			EHttpMediaType::CompressedBinary);
	}
}

FHttpRequest::ECompletionBehavior FHttpCacheStore::FPutPackageOp::OnCompressedBlobUploadComplete(
	FHttpRequest::EResult HttpResult,
	FHttpRequest* Request)
{
	int64 ResponseCode = Request->GetResponseCode();
	bool bIsSuccessResponse = FHttpRequest::IsSuccessResponse(ResponseCode);

	if (!bIsSuccessResponse && !ShouldAbortForShutdown() && !Owner.IsCanceled() && CacheStore.ShouldRetryOnError(HttpResult, ResponseCode) && ((Request->GetAttempts()+1) < UE_HTTPDDC_MAX_ATTEMPTS))
	{
		return FHttpRequest::ECompletionBehavior::Retry;
	}

	BytesSent.fetch_add(Request->GetBytesSent(), std::memory_order_relaxed);
	if (bIsSuccessResponse)
	{
		SuccessfulBlobUploads.fetch_add(1, std::memory_order_relaxed);
	}

	if (PendingBlobUploads.fetch_sub(1, std::memory_order_relaxed) == 1)
	{
		if (Owner.IsCanceled())
		{
			OnComplete(MakeResponse(BytesSent.load(std::memory_order_relaxed), EStatus::Canceled));
			return FHttpRequest::ECompletionBehavior::Done;
		}

		uint32 LocalSuccessfulBlobUploads = SuccessfulBlobUploads.load(std::memory_order_relaxed);
		if (LocalSuccessfulBlobUploads == TotalBlobUploads)
		{
			// Perform finalization
			PutRefAsync(CacheStore, Owner, Name, Key, PackageObject, PackageObjectHash, UserData, true,
				[PutPackageOp = TRefCountPtr<FPutPackageOp>(this)](FCachePutRefResponse&& Response)
				{
					return PutPackageOp->OnPutRefFinalizationComplete(MoveTemp(Response));
				});
		}
		else
		{
			uint32 FailedBlobUploads = (uint32)(TotalBlobUploads - LocalSuccessfulBlobUploads);
			UE_LOG(LogDerivedDataCache, Log, TEXT("%s: Failed to put %d/%d blobs for put of %s from '%s'"),
				*CacheStore.Domain, FailedBlobUploads, TotalBlobUploads, *WriteToString<96>(Key), *Name);
			OnComplete(MakeResponse(BytesSent.load(std::memory_order_relaxed), EStatus::Error));
		}
	}
	return FHttpRequest::ECompletionBehavior::Done;
}

void FHttpCacheStore::FPutPackageOp::OnPutRefFinalizationComplete(
	FCachePutRefResponse&& Response)
{
	BytesSent.fetch_add(Response.BytesSent, std::memory_order_relaxed);

	if (Response.Status == EStatus::Error)
	{
		UE_LOG(LogDerivedDataCache, Log, TEXT("%s: Failed to finalize reference object for put of %s from '%s'"),
			*CacheStore.Domain, *WriteToString<96>(Key), *Name);
	}

	return OnComplete(MakeResponse(BytesSent.load(std::memory_order_relaxed), Response.Status));
}

void FHttpCacheStore::FGetRecordOp::GetRecord(
	FHttpCacheStore& CacheStore,
	IRequestOwner& Owner,
	const FSharedString& Name,
	const FCacheKey& Key,
	const FCacheRecordPolicy& Policy,
	uint64 UserData,
	TUniqueFunction<void(FCacheGetResponse&& Response, uint64 BytesReceived)>&& OnComplete)
{
	CacheStore.GetCacheRecordOnlyAsync(Owner, Name, Key, Policy, UserData, [&CacheStore, &Owner, Policy = FCacheRecordPolicy(Policy), OnComplete = MoveTemp(OnComplete)](FGetCacheRecordOnlyResponse&& Response) mutable
	{
		OnOnlyRecordComplete(CacheStore, Owner, Policy, MoveTemp(OnComplete), MoveTemp(Response));
	});
}

template <typename ValueType, typename ValueIdGetterType>
void FHttpCacheStore::FGetRecordOp::GetDataBatch(
	FHttpCacheStore& CacheStore,
	IRequestOwner& Owner,
	FSharedString Name,
	const FCacheKey& Key,
	TConstArrayView<ValueType> Values,
	ValueIdGetterType ValueIdGetter,
	FOnGetCachedDataBatchComplete&& OnComplete)
{
	if (Values.IsEmpty())
	{
		return;
	}

	FRequestBarrier Barrier(Owner);
	TRefCountedUniqueFunction<FOnGetCachedDataBatchComplete>* CompletionFunction = new TRefCountedUniqueFunction<FOnGetCachedDataBatchComplete>(MoveTemp(OnComplete));
	TRefCountPtr<TRefCountedUniqueFunction<FOnGetCachedDataBatchComplete>> BatchOnCompleteRef(CompletionFunction);
	for (int32 ValueIndex = 0; ValueIndex < Values.Num(); ++ValueIndex)
	{
		const ValueType& Value = Values[ValueIndex];
		const FIoHash& RawHash = Value.GetRawHash();

		FHttpRequestPool* Pool = nullptr;
		FHttpRequest* Request = CacheStore.WaitForHttpRequestForOwner<OperationCategory::Get>(Owner, true /* bUnboundedOverflow */, Pool);

		auto OnHttpRequestComplete = [&CacheStore, &Owner, Name = FSharedString(Name), Key = FCacheKey(Key), ValueIndex, Value = Value.RemoveData(), ValueIdGetter, OnCompletePtr = TRefCountPtr<TRefCountedUniqueFunction<FOnGetCachedDataBatchComplete>>(CompletionFunction)]
		(FHttpRequest::EResult HttpResult, FHttpRequest* Request)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(HttpDDC_GetDataBatch_OnHttpRequestComplete);

			int64 ResponseCode = Request->GetResponseCode();
			bool bHit = false;
			FCompressedBuffer CompressedBuffer;
			if (FHttpRequest::IsSuccessResponse(ResponseCode))
			{
				FString ReceivedContentType;
				if (Request->GetHeader("Content-Type", ReceivedContentType))
				{
					if (ReceivedContentType == TEXT("application/x-ue-comp"))
					{
						CompressedBuffer = FCompressedBuffer::FromCompressed(Request->MoveResponseBufferToShared());
						bHit = true;
					}
					else if (ReceivedContentType == TEXT("application/octet-stream"))
					{
						CompressedBuffer = FValue::Compress(Request->MoveResponseBufferToShared()).GetData();
						bHit = true;
					}
					else
					{
						bHit = false;
					}
				}
				else
				{
					CompressedBuffer = FCompressedBuffer::FromCompressed(Request->MoveResponseBufferToShared());
					bHit = true;
				}
			}

			if (!ShouldAbortForShutdown() && !Owner.IsCanceled() && CacheStore.ShouldRetryOnError(HttpResult, ResponseCode) && ((Request->GetAttempts()+1) < UE_HTTPDDC_MAX_ATTEMPTS))
			{
				return FHttpRequest::ECompletionBehavior::Retry;
			}

			if (!bHit)
			{
				UE_LOG(LogDerivedDataCache, Verbose,
					TEXT("%s: Cache miss with missing value %s with hash %s for %s from '%s'"),
					*CacheStore.Domain, *ValueIdGetter(Value), *WriteToString<48>(Value.GetRawHash()), *WriteToString<96>(Key),
					*Name);
				OnCompletePtr->GetFunction()({ Name, Key, ValueIndex, Request->GetBytesReceived(), {}, EStatus::Error });
			}
			else if (CompressedBuffer.GetRawHash() != Value.GetRawHash())
			{
				UE_LOG(LogDerivedDataCache, Display,
					TEXT("%s: Cache miss with corrupted value %s with hash %s for %s from '%s'"),
					*CacheStore.Domain, *ValueIdGetter(Value), *WriteToString<48>(Value.GetRawHash()),
					*WriteToString<96>(Key), *Name);
				OnCompletePtr->GetFunction()({ Name, Key, ValueIndex, Request->GetBytesReceived(), {}, EStatus::Error });
			}
			else
			{
				OnCompletePtr->GetFunction()({ Name, Key, ValueIndex, Request->GetBytesReceived(), MoveTemp(CompressedBuffer), EStatus::Ok });
			}

			return FHttpRequest::ECompletionBehavior::Done;
		};

		TStringBuilder<256> CompressedBlobsUri;
		CompressedBlobsUri << "api/v1/compressed-blobs/" << CacheStore.StructuredNamespace << "/" << RawHash;
		Request->EnqueueAsyncDownload(Owner, Pool, *CompressedBlobsUri, MoveTemp(OnHttpRequestComplete), EHttpMediaType::Any, { 404 });
	}
}

FHttpCacheStore::FGetRecordOp::FGetRecordOp(
	FHttpCacheStore& InCacheStore,
	IRequestOwner& InOwner,
	const FSharedString& InName,
	const FCacheKey& InKey,
	uint64 InUserData,
	uint64 InBytesReceived,
	TArray<FValueWithId>&& InRequiredGets,
	TArray<FValueWithId>&& InRequiredHeads,
	FCacheRecordBuilder&& InRecordBuilder,
	TUniqueFunction<void(FCacheGetResponse&& Response, uint64 BytesReceived)>&& InOnComplete)
	: CacheStore(InCacheStore)
	, Owner(InOwner)
	, Name(InName)
	, Key(InKey)
	, UserData(InUserData)
	, BytesReceived(InBytesReceived)
	, RequiredGets(MoveTemp(InRequiredGets))
	, RequiredHeads(MoveTemp(InRequiredHeads))
	, RecordBuilder(MoveTemp(InRecordBuilder))
	, TotalOperations(RequiredGets.Num() + RequiredHeads.Num())
	, SuccessfulOperations(0)
	, PendingOperations(TotalOperations)
	, OnComplete(MoveTemp(InOnComplete))
{
	FetchedBuffers.AddDefaulted(RequiredGets.Num());
}

void FHttpCacheStore::FGetRecordOp::OnOnlyRecordComplete(
	FHttpCacheStore& CacheStore,
	IRequestOwner& Owner,
	const FCacheRecordPolicy& Policy,
	TUniqueFunction<void(FCacheGetResponse&& Response, uint64 BytesReceived)>&& OnComplete,
	FGetCacheRecordOnlyResponse&& Response)
{
	FCacheRecordBuilder RecordBuilder(Response.Key);
	if (Response.Status != EStatus::Ok)
	{
		return OnComplete({ Response.Name, RecordBuilder.Build(), Response.UserData, Response.Status }, Response.BytesReceived);
	}

	if (!EnumHasAnyFlags(Policy.GetRecordPolicy(), ECachePolicy::SkipMeta))
	{
		RecordBuilder.SetMeta(FCbObject(Response.Record.Get().GetMeta()));
	}

	// TODO: There is not currently a batched GET endpoint for Jupiter.  Once there is, all payload data should be fetched in one call.
	//		 In the meantime, we try to keep the code structured in a way that is friendly to future batching of GETs.

	TArray<FValueWithId> RequiredGets;
	TArray<FValueWithId> RequiredHeads;

	for (FValueWithId Value : Response.Record.Get().GetValues())
	{
		const ECachePolicy ValuePolicy = Policy.GetValuePolicy(Value.GetId());
		if (IsValueDataReady(Value, ValuePolicy))
		{
			RecordBuilder.AddValue(MoveTemp(Value));
		}
		else
		{
			if (EnumHasAnyFlags(ValuePolicy, ECachePolicy::SkipData))
			{
				RequiredHeads.Emplace(Value);
			}
			else
			{
				RequiredGets.Emplace(Value);
			}
		}
	}

	if (RequiredGets.IsEmpty() && RequiredHeads.IsEmpty())
	{
		return OnComplete({ Response.Name, RecordBuilder.Build(), Response.UserData, Response.Status }, Response.BytesReceived);
	}

	// Having this be a ref ensures we don't have the op reach 0 ref count in between the start of the exist batch operation and the get batch operation
	TRefCountPtr<FGetRecordOp> GetRecordOp = new FGetRecordOp(
		CacheStore,
		Owner,
		Response.Name,
		Response.Key,
		Response.UserData,
		Response.BytesReceived,
		MoveTemp(RequiredGets),
		MoveTemp(RequiredHeads),
		MoveTemp(RecordBuilder),
		MoveTemp(OnComplete)
	);

	auto IdGetter = [](const FValueWithId& Value)
	{
		return FString(WriteToString<16>(Value.GetId()));
	};

	{
		FRequestBarrier Barrier(Owner);
		GetRecordOp->DataProbablyExistsBatch(GetRecordOp->RequiredHeads, [GetRecordOp](FCachedDataProbablyExistsBatchResponse&& Response)
		{
			GetRecordOp->FinishDataStep(Response.Status == EStatus::Ok, 0);
		});

		GetDataBatch<FValueWithId>(CacheStore, Owner, Response.Name, Response.Key, GetRecordOp->RequiredGets, IdGetter, [GetRecordOp](FGetCachedDataBatchResponse&& Response)
		{
			GetRecordOp->FetchedBuffers[Response.ValueIndex] = MoveTemp(Response.DataBuffer);
			GetRecordOp->FinishDataStep(Response.Status == EStatus::Ok, Response.BytesReceived);
		});

	}
}

void FHttpCacheStore::FGetRecordOp::DataProbablyExistsBatch(
	TConstArrayView<FValueWithId> Values,
	FOnCachedDataProbablyExistsBatchComplete&& InOnComplete)
{
	if (Values.IsEmpty())
	{
		return;
	}

	FHttpRequestPool* Pool = nullptr;
	FHttpRequest* Request = CacheStore.WaitForHttpRequestForOwner<OperationCategory::Get>(Owner, true /* bUnboundedOverflow */, Pool);

	TStringBuilder<256> CompressedBlobsUri;
	CompressedBlobsUri << "api/v1/compressed-blobs/" << CacheStore.StructuredNamespace << "/exists?";
	bool bFirstItem = true;
	for (const FValueWithId& Value : Values)
	{
		if (!bFirstItem)
		{
			CompressedBlobsUri << "&";
		}
		CompressedBlobsUri << "id=" << Value.GetRawHash();
		bFirstItem = false;
	}

	auto OnHttpRequestComplete = [this, Values = TArray<FValueWithId>(Values), InOnComplete = MoveTemp(InOnComplete)](FHttpRequest::EResult HttpResult, FHttpRequest* Request)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(HttpDDC_DataProbablyExistsBatch_OnHttpRequestComplete);

		int64 ResponseCode = Request->GetResponseCode();
		if (FHttpRequest::IsSuccessResponse(ResponseCode))
		{
			if (TSharedPtr<FJsonObject> ResponseObject = Request->GetResponseAsJsonObject())
			{
				TArray<FString> NeedsArrayStrings;
				if (ResponseObject->TryGetStringArrayField(TEXT("needs"), NeedsArrayStrings))
				{
					if (NeedsArrayStrings.IsEmpty())
					{
						for (int32 ValueIndex = 0; ValueIndex < Values.Num(); ++ValueIndex)
						{
							const FValueWithId& Value = Values[ValueIndex];
							UE_LOG(LogDerivedDataCache, VeryVerbose,
								TEXT("%s: Cache exists hit for %s with hash %s for %s from '%s'"),
								*CacheStore.Domain, *WriteToString<16>(Value.GetId()), *WriteToString<48>(Value.GetRawHash()), *WriteToString<96>(Key),
								*Name);
							InOnComplete({ Name, Key, ValueIndex, EStatus::Ok });
						}
						return FHttpRequest::ECompletionBehavior::Done;
					}
				}

				TBitArray<> ResultStatus(true, Values.Num());
				for (const FString& NeedsString : NeedsArrayStrings)
				{
					const FIoHash NeedHash(NeedsString);
					for (int32 ValueIndex = 0; ValueIndex < Values.Num(); ++ValueIndex)
					{
						const FValueWithId& Value = Values[ValueIndex];
						if (ResultStatus[ValueIndex] && NeedHash == Value.GetRawHash())
						{
							ResultStatus[ValueIndex] = false;
							break;
						}
					}
				}

				for (int32 ValueIndex = 0; ValueIndex < Values.Num(); ++ValueIndex)
				{
					const FValueWithId& Value = Values[ValueIndex];

					if (ResultStatus[ValueIndex])
					{
						UE_LOG(LogDerivedDataCache, VeryVerbose,
							TEXT("%s: Cache exists hit for %s with hash %s for %s from '%s'"),
							*CacheStore.Domain, *WriteToString<16>(Value.GetId()), *WriteToString<48>(Value.GetRawHash()), *WriteToString<96>(Key),
							*Name);
						InOnComplete({ Name, Key, ValueIndex, EStatus::Ok });
					}
					else
					{
						UE_LOG(LogDerivedDataCache, Verbose,
							TEXT("%s: Cache exists miss with missing value %s with hash %s for %s from '%s'"),
							*CacheStore.Domain, *WriteToString<16>(Value.GetId()), *WriteToString<48>(Value.GetRawHash()), *WriteToString<96>(Key),
							*Name);
						InOnComplete({ Name, Key, ValueIndex, EStatus::Error });
					}
				}
			}
			else
			{
				for (int32 ValueIndex = 0; ValueIndex < Values.Num(); ++ValueIndex)
				{
					UE_LOG(LogDerivedDataCache, Log,
						TEXT("%s: Cache exists returned invalid results."),
						*CacheStore.Domain);
					InOnComplete({ Name, Key, ValueIndex, EStatus::Error });
				}
			}

			return FHttpRequest::ECompletionBehavior::Done;
		}

		if (!ShouldAbortForShutdown() && !Owner.IsCanceled() && CacheStore.ShouldRetryOnError(HttpResult, ResponseCode) && ((Request->GetAttempts()+1) < UE_HTTPDDC_MAX_ATTEMPTS))
		{
			return FHttpRequest::ECompletionBehavior::Retry;
		}

		for (int32 ValueIndex = 0; ValueIndex < Values.Num(); ++ValueIndex)
		{
			const FValueWithId& Value = Values[ValueIndex];
			UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Cache miss with failed HTTP request for %s from '%s'"),
				*CacheStore.Domain, *WriteToString<96>(Key), *Name);
			InOnComplete({Name, Key, ValueIndex, EStatus::Error});
		}
		return FHttpRequest::ECompletionBehavior::Done;
	};
	FCompositeBuffer DummyBuffer;
	Request->EnqueueAsyncPost(Owner, Pool, *CompressedBlobsUri, DummyBuffer, MoveTemp(OnHttpRequestComplete), EHttpMediaType::FormUrlEncoded);
}

void FHttpCacheStore::FGetRecordOp::FinishDataStep(bool bSuccess, uint64 InBytesReceived)
{
	BytesReceived.fetch_add(InBytesReceived, std::memory_order_relaxed);
	if (bSuccess)
	{
		SuccessfulOperations.fetch_add(1, std::memory_order_relaxed);
	}

	if (PendingOperations.fetch_sub(1, std::memory_order_acq_rel) == 1)
	{
		EStatus Status = EStatus::Error;
		uint32 LocalSuccessfulOperations = SuccessfulOperations.load(std::memory_order_relaxed);
		if (LocalSuccessfulOperations == TotalOperations)
		{
			for (int32 Index = 0; Index < RequiredHeads.Num(); ++Index)
			{
				RecordBuilder.AddValue(RequiredHeads[Index].RemoveData());
			}

			for (int32 Index = 0; Index < RequiredGets.Num(); ++Index)
			{
				RecordBuilder.AddValue(FValueWithId(RequiredGets[Index].GetId(), FetchedBuffers[Index]));
			}
			Status = EStatus::Ok;
		}
		OnComplete({Name, RecordBuilder.Build(), UserData, Status}, BytesReceived.load(std::memory_order_relaxed));
	}
}

FHttpCacheStore::FHttpCacheStore(const FHttpCacheStoreParams& Params)
	: Domain(Params.Host)
	, EffectiveDomain(Params.Host)
	, Namespace(Params.Namespace)
	, StructuredNamespace(Params.StructuredNamespace)
	, DefaultBucket(TEXT("default"))
	, OAuthProvider(Params.OAuthProvider)
	, OAuthClientId(Params.OAuthClientId)
	, OAuthSecret(Params.OAuthSecret)
	, OAuthScope(Params.OAuthScope)
	, OAuthProviderIdentifier(Params.OAuthProviderIdentifier)
	, OAuthAccessToken(Params.OAuthAccessToken)
	, Access(nullptr)
	, bIsUsable(false)
	, bReadOnly(Params.bReadOnly)
	, FailedLoginAttempts(0)
{
	SharedData = MakeUnique<FHttpSharedData>();
	if (IsServiceReady() && AcquireAccessToken())
	{
		FString OriginalDomainPrefix;
		TAnsiStringBuilder<64> DomainResolveName;

		if (Domain.StartsWith(TEXT("http://")))
		{
			DomainResolveName << Domain.RightChop(7);
			OriginalDomainPrefix = TEXT("http://");
		}
		else if (Domain.StartsWith(TEXT("https://")))
		{
			DomainResolveName << Domain.RightChop(8);
			OriginalDomainPrefix = TEXT("https://");
		}
		else
		{
			DomainResolveName << Domain;
		}

		addrinfo* AddrResult = nullptr;
		addrinfo AddrHints;
		FMemory::Memset(&AddrHints, 0, sizeof(AddrHints));
		AddrHints.ai_flags = AI_CANONNAME;
		AddrHints.ai_family = AF_UNSPEC;
		if (Params.bResolveHostCanonicalName && !::getaddrinfo(*DomainResolveName, nullptr, &AddrHints, &AddrResult))
		{
			if (AddrResult->ai_canonname)
			{
				// Swap the domain with a canonical name from DNS so that if we are using regional redirection, we pin to a region.
				EffectiveDomain = OriginalDomainPrefix + ANSI_TO_TCHAR(AddrResult->ai_canonname);

				UE_LOG(LogDerivedDataCache, Display,
					TEXT("%s: Pinned to %s based on DNS canonical name."),
					*Domain, *EffectiveDomain);
			}
			else
			{
				EffectiveDomain = Domain;
			}

			::freeaddrinfo(AddrResult);
		}
		else
		{
			EffectiveDomain = Domain;
		}

		GetRequestPools[0] = MakeUnique<FHttpRequestPool>(*Domain, *EffectiveDomain, Access.Get(), SharedData.Get(), UE_HTTPDDC_GET_REQUEST_POOL_SIZE);
		GetRequestPools[1] = MakeUnique<FHttpRequestPool>(*Domain, *EffectiveDomain, Access.Get(), SharedData.Get(), UE_HTTPDDC_GET_REQUEST_POOL_SIZE);
		PutRequestPools[0] = MakeUnique<FHttpRequestPool>(*Domain, *EffectiveDomain, Access.Get(), SharedData.Get(), UE_HTTPDDC_PUT_REQUEST_POOL_SIZE);
		PutRequestPools[1] = MakeUnique<FHttpRequestPool>(*Domain, *EffectiveDomain, Access.Get(), SharedData.Get(), UE_HTTPDDC_PUT_REQUEST_POOL_SIZE);
		// Allowing the non-blocking requests to overflow to double their pre-allocated size before we start waiting for one to free up.
		NonBlockingRequestPools = MakeUnique<FHttpRequestPool>(*Domain, *EffectiveDomain, Access.Get(), SharedData.Get(), UE_HTTPDDC_NONBLOCKING_REQUEST_POOL_SIZE, UE_HTTPDDC_NONBLOCKING_REQUEST_POOL_SIZE);
		bIsUsable = true;
	}

	AnyInstance = this;
}

FHttpCacheStore::~FHttpCacheStore()
{
	if (AnyInstance == this)
	{
		AnyInstance = nullptr;
	}
}

bool FHttpCacheStore::LegacyDebugOptions(FBackendDebugOptions& InOptions)
{
	DebugOptions = InOptions;
	return true;
}

bool FHttpCacheStore::IsServiceReady()
{
	FHttpRequest Request(*Domain, *Domain, nullptr, SharedData.Get(), false);
	FHttpRequest::EResult Result = Request.PerformBlockingDownload(TEXT("health/ready"), nullptr);
	
	if (Result == FHttpRequest::EResult::Success && Request.GetResponseCode() == 200)
	{
		UE_LOG(LogDerivedDataCache, Display, TEXT("%s: HTTP DDC service status: %s."), *Request.GetName(), *Request.GetResponseAsString());
		return true;
	}
	else
	{
		UE_LOG(LogDerivedDataCache, Warning, TEXT("%s: Unable to reach HTTP DDC service at %s. Status: %d . Response: %s"), *Request.GetName(), *Domain, Request.GetResponseCode(), *Request.GetResponseAsString());
	}

	return false;
}

bool FHttpCacheStore::AcquireAccessToken()
{
	if (Domain.StartsWith(TEXT("http://localhost")))
	{
		UE_LOG(LogDerivedDataCache, Log, TEXT("Connecting to a local host '%s', so skipping authorization"), *Domain);
		return true;
	}

	// Avoid spamming the this if the service is down
	if (FailedLoginAttempts > UE_HTTPDDC_MAX_FAILED_LOGIN_ATTEMPTS)
	{
		return false;
	}

	ensureMsgf(OAuthProvider.StartsWith(TEXT("http://")) || OAuthProvider.StartsWith(TEXT("https://")),
		TEXT("The OAuth provider %s is not valid. Needs to be a fully qualified url."),
		*OAuthProvider
	);

	// In case many requests wants to update the token at the same time
	// get the current serial while we wait to take the CS.
	const uint32 WantsToUpdateTokenSerial = Access.IsValid() ? Access->GetSerial() : 0u;

	if (bHttpEnableOidc && !OAuthProviderIdentifier.IsEmpty())
	{
		FString AccessTokenString;
		FDateTime TokenExpiresAt;
		
		{
			FScopeLock Lock(&AccessCs);

			// Check if someone has beaten us to update the token, then it 
			// should now be valid.
			if (Access.IsValid() && Access->GetSerial() > WantsToUpdateTokenSerial)
			{
				return true;
			}

			if (!OAuthAccessToken.IsEmpty())
			{
				if (!Access)
				{
					Access = MakeUnique<FHttpAccessToken>();
				}
				Access->SetToken(OAuthAccessToken);
				FailedLoginAttempts = 0;
				return true;
			}

			if (FDesktopPlatformModule::Get()->GetOidcAccessToken(FPaths::RootDir(), FPaths::GetProjectFilePath(), OAuthProviderIdentifier, /* Unattended */ true, GWarn, AccessTokenString, TokenExpiresAt))
			{
				int32 ExpiryTimeSeconds = (TokenExpiresAt - FDateTime::UtcNow()).GetTotalSeconds();
				if (!Access)
				{
					Access = MakeUnique<FHttpAccessToken>();
				}
				Access->SetToken(AccessTokenString);
				UE_LOG(LogDerivedDataCache, Display, TEXT("OidcToken: Logged in to HTTP DDC services. Expires at %s which is in %d seconds."), *TokenExpiresAt.ToString(), ExpiryTimeSeconds);

				//Schedule a refresh of the token ahead of expiry time (this will not work in commandlets)
				if (!IsRunningCommandlet())
				{
					FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
						[this](float DeltaTime)
						{
							this->AcquireAccessToken();
							return false;
						}
					), ExpiryTimeSeconds - 20.0f);
				}
				// Reset failed login attempts, the service is indeed alive.
				FailedLoginAttempts = 0;
				return true;
			}
			else
			{
				UE_LOG(LogDerivedDataCache, Error, TEXT("OidcToken: Failed to log in to HTTP services. "));
				FailedLoginAttempts++;
			}
		}
		
	}
	else // oidc not enabled or not configured
	{
		FScopeLock Lock(&AccessCs);

		// Check if someone has beaten us to update the token, then it 
		// should now be valid.
		if (Access.IsValid() && Access->GetSerial() > WantsToUpdateTokenSerial)
		{
			return true;
		}

		const uint32 SchemeEnd = OAuthProvider.Find(TEXT("://")) + 3;
		const uint32 DomainEnd = OAuthProvider.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, SchemeEnd);
		FString AuthDomain(DomainEnd, *OAuthProvider);
		FString Uri(*OAuthProvider + DomainEnd + 1);

		FHttpRequest Request(*AuthDomain, *AuthDomain, nullptr, SharedData.Get(), false);
		FHttpRequest::EResult Result = FHttpRequest::EResult::Success;
		if (OAuthProvider.StartsWith(TEXT("http://localhost")))
		{
			// Simple unauthenticated call to a local endpoint that mimics
			// the result from an OIDC provider.
			Result = Request.PerformBlockingDownload(*Uri, nullptr);
		}
		else
		{
			// Needs client id and secret to authenticate with an actual OIDC provider.

			// If contents of the secret string is a file path, resolve and read form data.
			if (OAuthSecret.StartsWith(TEXT("file://")))
			{
				FString FilePath = OAuthSecret.Mid(7, OAuthSecret.Len() - 7);
					FString SecretFileContents;
				if (FFileHelper::LoadFileToString(SecretFileContents, *FilePath))
				{
					// Overwrite the filepath with the actual content.
					OAuthSecret = SecretFileContents;
				}
				else
				{
					UE_LOG(LogDerivedDataCache, Warning, TEXT("%s: Failed to read OAuth form data file (%s)."), *Request.GetName(), *OAuthSecret);
					return false;
				}
			}

			FString OAuthFormData = FString::Printf(
				TEXT("client_id=%s&scope=%s&grant_type=client_credentials&client_secret=%s"),
				*OAuthClientId,
				*OAuthScope,
				*OAuthSecret
			);

			TArray64<uint8> FormData;
			auto OAuthFormDataUTF8 = FTCHARToUTF8(*OAuthFormData);
			FormData.Append((uint8*)OAuthFormDataUTF8.Get(), OAuthFormDataUTF8.Length());

			Result = Request.PerformBlockingPost(*Uri, FCompositeBuffer(FSharedBuffer::MakeView(FormData.GetData(), FormData.Num())), EHttpMediaType::FormUrlEncoded);
		}

		if (Result == FHttpRequest::EResult::Success && Request.GetResponseCode() == 200)
		{
			TSharedPtr<FJsonObject> ResponseObject = Request.GetResponseAsJsonObject();
			if (ResponseObject)
			{
				FString AccessTokenString;
				int32 ExpiryTimeSeconds = 0;
				int32 CurrentTimeSeconds = int32(FPlatformTime::ToSeconds(FPlatformTime::Cycles()));

				if (ResponseObject->TryGetStringField(TEXT("access_token"), AccessTokenString) &&
					ResponseObject->TryGetNumberField(TEXT("expires_in"), ExpiryTimeSeconds))
				{
					if (!Access)
					{
						Access = MakeUnique<FHttpAccessToken>();
					}
					Access->SetToken(AccessTokenString);
					UE_LOG(LogDerivedDataCache, Display, TEXT("%s: Logged in to HTTP DDC services. Expires in %d seconds."), *Request.GetName(), ExpiryTimeSeconds);

					//Schedule a refresh of the token ahead of expiry time (this will not work in commandlets)
					if (!IsRunningCommandlet())
					{
						FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
							[this](float DeltaTime)
							{
								this->AcquireAccessToken();
								return false;
							}
						), ExpiryTimeSeconds - 20.0f);
					}
					// Reset failed login attempts, the service is indeed alive.
					FailedLoginAttempts = 0;
					return true;
				}
			}
		}
		else
		{
			UE_LOG(LogDerivedDataCache, Warning, TEXT("%s: Failed to log in to HTTP services. Server responed with code %d."), *Request.GetName(), Request.GetResponseCode());
			FailedLoginAttempts++;
		}
	}
	return false;
}

bool FHttpCacheStore::ShouldRetryOnError(FHttpRequest::EResult Result, int64 ResponseCode)
{
	if (Result == FHttpRequest::EResult::FailedTimeout)
	{
		return true;
	}

	// Access token might have expired, request a new token and try again.
	if (ResponseCode == 401 && AcquireAccessToken())
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

void FHttpCacheStore::GetCacheRecordOnlyAsync(
	IRequestOwner& Owner,
	const FSharedString& Name,
	const FCacheKey& Key,
	const FCacheRecordPolicy& Policy,
	uint64 UserData,
	FOnGetCacheRecordOnlyComplete&& OnComplete)
{
	auto MakeResponse = [Name = FSharedString(Name), Key, UserData](uint64 BytesReceived, EStatus Status)
	{
		return FGetCacheRecordOnlyResponse{ Name, Key, UserData, BytesReceived, {}, Status };
	};

	if (!IsUsable())
	{
		UE_LOG(LogDerivedDataCache, VeryVerbose,
			TEXT("%s: Skipped get of %s from '%s' because this cache store is not available"),
			*Domain, *WriteToString<96>(Key), *Name);
		return OnComplete(MakeResponse(0, EStatus::Error));
	}

	// Skip the request if querying the cache is disabled.
	if (!EnumHasAnyFlags(Policy.GetRecordPolicy(), ECachePolicy::QueryRemote))
	{
		UE_LOG(LogDerivedDataCache, VeryVerbose, TEXT("%s: Skipped get of %s from '%s' due to cache policy"),
			*Domain, *WriteToString<96>(Key), *Name);
		return OnComplete(MakeResponse(0, EStatus::Error));
	}

	if (DebugOptions.ShouldSimulateGetMiss(Key))
	{
		UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Simulated miss for get of %s from '%s'"),
			*Domain, *WriteToString<96>(Key), *Name);
		return OnComplete(MakeResponse(0, EStatus::Error));
	}

	FString Bucket(Key.Bucket.ToString());
	Bucket.ToLowerInline();

	TStringBuilder<256> RefsUri;
	RefsUri << "api/v1/refs/" << StructuredNamespace << "/" << Bucket << "/" << Key.Hash;

	FHttpRequestPool* Pool = nullptr;
	FHttpRequest* Request = WaitForHttpRequestForOwner<OperationCategory::Get>(Owner, false /* bUnboundedOverflow */, Pool);

	auto OnHttpRequestComplete = [this, &Owner, Name = FSharedString(Name), Key, UserData, OnComplete = MoveTemp(OnComplete)]
	(FHttpRequest::EResult HttpResult, FHttpRequest* Request)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(HttpDDC_GetCacheRecordOnlyAsync_OnHttpRequestComplete);

		int64 ResponseCode = Request->GetResponseCode();
		if (FHttpRequest::IsSuccessResponse(ResponseCode))
		{
			FSharedBuffer ResponseBuffer = Request->MoveResponseBufferToShared();

			if (ValidateCompactBinary(ResponseBuffer, ECbValidateMode::Default) != ECbValidateError::None)
			{
				UE_LOG(LogDerivedDataCache, Log, TEXT("%s: Cache miss with invalid package for %s from '%s'"),
					*Domain, *WriteToString<96>(Key), *Name);
				OnComplete({ Name, Key, UserData, Request->GetBytesReceived(), {}, EStatus::Error });
				return FHttpRequest::ECompletionBehavior::Done;
			}

			FOptionalCacheRecord Record = FCacheRecord::Load(FCbPackage(FCbObject(ResponseBuffer)));
			if (Record.IsNull())
			{
				UE_LOG(LogDerivedDataCache, Log, TEXT("%s: Cache miss with record load failure for %s from '%s'"),
					*Domain, *WriteToString<96>(Key), *Name);
				OnComplete({ Name, Key, UserData, Request->GetBytesReceived(), {}, EStatus::Error });
				return FHttpRequest::ECompletionBehavior::Done;
			}

			OnComplete({ Name, Key, UserData, Request->GetBytesReceived(), MoveTemp(Record), EStatus::Ok });
			return FHttpRequest::ECompletionBehavior::Done;
		}

		if (!ShouldAbortForShutdown() && !Owner.IsCanceled() && ShouldRetryOnError(HttpResult, ResponseCode) && ((Request->GetAttempts()+1) < UE_HTTPDDC_MAX_ATTEMPTS))
		{
			return FHttpRequest::ECompletionBehavior::Retry;
		}

		UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Cache miss with missing package for %s from '%s'"),
			*Domain, *WriteToString<96>(Key), *Name);
		OnComplete({ Name, Key, UserData, Request->GetBytesReceived(), {}, EStatus::Error });
		return FHttpRequest::ECompletionBehavior::Done;
	};
	Request->EnqueueAsyncDownload(Owner, Pool, *RefsUri, MoveTemp(OnHttpRequestComplete), EHttpMediaType::CbObject, { 401, 404 });
}

void FHttpCacheStore::PutCacheRecordAsync(
	IRequestOwner& Owner,
	const FSharedString& Name,
	const FCacheRecord& Record,
	const FCacheRecordPolicy& Policy,
	uint64 UserData,
	TUniqueFunction<void(FCachePutResponse&& Response, uint64 BytesSent)>&& OnComplete)
{
	const FCacheKey& Key = Record.GetKey();
	auto MakeResponse = [Name = FSharedString(Name), Key = FCacheKey(Key), UserData](EStatus Status)
	{
		return FCachePutResponse{ Name, Key, UserData, Status };
	};

	if (bReadOnly)
	{
		UE_LOG(LogDerivedDataCache, VeryVerbose,
			TEXT("%s: Skipped put of %s from '%s' because this cache store is read-only"),
			*Domain, *WriteToString<96>(Key), *Name);
		return OnComplete(MakeResponse(EStatus::Error), 0);
	}

	// Skip the request if storing to the cache is disabled.
	const ECachePolicy RecordPolicy = Policy.GetRecordPolicy();
	if (!EnumHasAnyFlags(RecordPolicy, ECachePolicy::StoreRemote))
	{
		UE_LOG(LogDerivedDataCache, VeryVerbose, TEXT("%s: Skipped put of %s from '%s' due to cache policy"),
			*Domain, *WriteToString<96>(Key), *Name);
		return OnComplete(MakeResponse(EStatus::Error), 0);
	}

	if (DebugOptions.ShouldSimulatePutMiss(Key))
	{
		UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Simulated miss for put of %s from '%s'"),
			*Domain, *WriteToString<96>(Key), *Name);
		return OnComplete(MakeResponse(EStatus::Error), 0);
	}

	// TODO: Jupiter currently always overwrites.  It doesn't have a "write if not present" feature (for records or attachments),
	//		 but would require one to implement all policy correctly.

	FString Bucket(Key.Bucket.ToString());
	Bucket.ToLowerInline();

	FCbPackage Package = Record.Save();

	FPutPackageOp::PutPackage(*this, Owner, Name, Key, MoveTemp(Package), Policy, UserData, [MakeResponse = MoveTemp(MakeResponse), OnComplete = MoveTemp(OnComplete)](FPutPackageOp::FCachePutPackageResponse&& Response)
	{
		OnComplete(MakeResponse(Response.Status), Response.BytesSent);
	});
}

void FHttpCacheStore::PutCacheValueAsync(
	IRequestOwner& Owner,
	const FSharedString& Name,
	const FCacheKey& Key,
	const FValue& Value,
	const ECachePolicy Policy,
	uint64 UserData,
	TUniqueFunction<void(FCachePutValueResponse&& Response, uint64 BytesSent)>&& OnComplete)
{
	auto MakeResponse = [Name = FSharedString(Name), Key = FCacheKey(Key), UserData](EStatus Status)
	{
		return FCachePutValueResponse{ Name, Key, UserData, Status };
	};

	if (bReadOnly)
	{
		UE_LOG(LogDerivedDataCache, VeryVerbose,
			TEXT("%s: Skipped put of %s from '%s' because this cache store is read-only"),
			*Domain, *WriteToString<96>(Key), *Name);
		return OnComplete(MakeResponse(EStatus::Error), 0);
	}

	// Skip the request if storing to the cache is disabled.
	if (!EnumHasAnyFlags(Policy, ECachePolicy::StoreRemote))
	{
		UE_LOG(LogDerivedDataCache, VeryVerbose, TEXT("%s: Skipped put of %s from '%s' due to cache policy"),
			*Domain, *WriteToString<96>(Key), *Name);
		return OnComplete(MakeResponse(EStatus::Error), 0);
	}

	if (DebugOptions.ShouldSimulatePutMiss(Key))
	{
		UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Simulated miss for put of %s from '%s'"),
			*Domain, *WriteToString<96>(Key), *Name);
		return OnComplete(MakeResponse(EStatus::Error), 0);
	}

	// TODO: Jupiter currently always overwrites.  It doesn't have a "write if not present" feature (for records or attachments),
	//		 but would require one to implement all policy correctly.

	FString Bucket(Key.Bucket.ToString());
	Bucket.ToLowerInline();

	FCbWriter Writer;
	Writer.BeginObject();
	Writer.AddBinaryAttachment("RawHash", Value.GetRawHash());
	Writer.AddInteger("RawSize", Value.GetRawSize());
	Writer.EndObject();

	FCbPackage Package(Writer.Save().AsObject());
	Package.AddAttachment(FCbAttachment(Value.GetData()));

	FPutPackageOp::PutPackage(*this, Owner, Name, Key, MoveTemp(Package), Policy, UserData, [MakeResponse = MoveTemp(MakeResponse), OnComplete = MoveTemp(OnComplete)](FPutPackageOp::FCachePutPackageResponse&& Response)
	{
		OnComplete(MakeResponse(Response.Status), Response.BytesSent);
	});
}

void FHttpCacheStore::GetCacheValueAsync(
	IRequestOwner& Owner,
	FSharedString Name,
	const FCacheKey& Key,
	ECachePolicy Policy,
	uint64 UserData,
	FOnCacheGetValueComplete&& OnComplete)
{
	if (!IsUsable())
	{
		UE_LOG(LogDerivedDataCache, VeryVerbose,
			TEXT("%s: Skipped get of %s from '%s' because this cache store is not available"),
			*Domain, *WriteToString<96>(Key), *Name);
		OnComplete({Name, Key, {}, UserData, EStatus::Error});
		return;
	}

	// Skip the request if querying the cache is disabled.
	if (!EnumHasAnyFlags(Policy, ECachePolicy::QueryRemote))
	{
		UE_LOG(LogDerivedDataCache, VeryVerbose, TEXT("%s: Skipped get of %s from '%s' due to cache policy"),
			*Domain, *WriteToString<96>(Key), *Name);
		OnComplete({Name, Key, {}, UserData, EStatus::Error});
		return;
	}

	if (DebugOptions.ShouldSimulateGetMiss(Key))
	{
		UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Simulated miss for get of %s from '%s'"),
			*Domain, *WriteToString<96>(Key), *Name);
		OnComplete({Name, Key, {}, UserData, EStatus::Error});
		return;
	}

	const bool bSkipData = EnumHasAnyFlags(Policy, ECachePolicy::SkipData);

	FString Bucket(Key.Bucket.ToString());
	Bucket.ToLowerInline();

	TStringBuilder<256> RefsUri;
	RefsUri << "api/v1/refs/" << StructuredNamespace << "/" << Bucket << "/" << Key.Hash;

	FHttpRequestPool* Pool = nullptr;
	FHttpRequest* Request = WaitForHttpRequestForOwner<OperationCategory::Get>(Owner, false /* bUnboundedOverflow */, Pool);
	if (bSkipData)
	{
		Request->AddHeader(TEXT("Accept"), TEXT("application/x-ue-cb"));
	}
	else
	{
		Request->AddHeader(TEXT("Accept"), TEXT("application/x-jupiter-inline"));
	}

	auto OnHttpRequestComplete = [this, &Owner, Name, Key, UserData, bSkipData, OnComplete = MoveTemp(OnComplete)] (FHttpRequest::EResult HttpResult, FHttpRequest* Request)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(HttpDDC_GetCacheValueAsync_OnHttpRequestComplete);

		int64 ResponseCode = Request->GetResponseCode();
		if (FHttpRequest::IsSuccessResponse(ResponseCode))
		{
			FValue ResultValue;
			FSharedBuffer ResponseBuffer = Request->MoveResponseBufferToShared();

			if (bSkipData)
			{
				if (ValidateCompactBinary(ResponseBuffer, ECbValidateMode::Default) != ECbValidateError::None)
				{
					UE_LOG(LogDerivedDataCache, Display, TEXT("%s: Cache miss with invalid package for %s from '%s'"),
						*Domain, *WriteToString<96>(Key), *Name);
					OnComplete({Name, Key, {}, UserData, EStatus::Error});
					return FHttpRequest::ECompletionBehavior::Done;
				}

				const FCbObjectView Object = FCbObject(ResponseBuffer);
				const FIoHash RawHash = Object["RawHash"].AsHash();
				const uint64 RawSize = Object["RawSize"].AsUInt64(MAX_uint64);
				if (RawHash.IsZero() || RawSize == MAX_uint64)
				{
					UE_LOG(LogDerivedDataCache, Display, TEXT("%s: Cache miss with invalid value for %s from '%'"),
						*Domain, *WriteToString<96>(Key), *Name);
					OnComplete({Name, Key, {}, UserData, EStatus::Error});
					return FHttpRequest::ECompletionBehavior::Done;
				}
				ResultValue = FValue(RawHash, RawSize);
			}
			else
			{
				FCompressedBuffer CompressedBuffer = FCompressedBuffer::FromCompressed(ResponseBuffer);
				if (!CompressedBuffer)
				{
					FString ReceivedHashStr;
					if (Request->GetHeader("X-Jupiter-InlinePayloadHash", ReceivedHashStr))
					{
						FIoHash ReceivedHash(ReceivedHashStr);
						FIoHash ComputedHash = FIoHash::HashBuffer(ResponseBuffer.GetView());
						if (ReceivedHash == ComputedHash)
						{
							CompressedBuffer = FCompressedBuffer::Compress(ResponseBuffer);
						}
					}
				}

				if (!CompressedBuffer)
				{
					UE_LOG(LogDerivedDataCache, Display, TEXT("%s: Cache miss with invalid package for %s from '%s'"),
						*Domain, *WriteToString<96>(Key), *Name);
					OnComplete({Name, Key, {}, UserData, EStatus::Error});
					return FHttpRequest::ECompletionBehavior::Done;
				}
				ResultValue = FValue(CompressedBuffer);
			}
			OnComplete({Name, Key, ResultValue, UserData, EStatus::Ok});
			return FHttpRequest::ECompletionBehavior::Done;
		}

		if (!ShouldAbortForShutdown() && !Owner.IsCanceled() && ShouldRetryOnError(HttpResult, ResponseCode) && ((Request->GetAttempts()+1) < UE_HTTPDDC_MAX_ATTEMPTS))
		{
			return FHttpRequest::ECompletionBehavior::Retry;
		}

		UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Cache miss with failed HTTP request for %s from '%s'"),
			*Domain, *WriteToString<96>(Key), *Name);
		OnComplete({Name, Key, {}, UserData, EStatus::Error});
		return FHttpRequest::ECompletionBehavior::Done;
	};

	Request->EnqueueAsyncDownload(Owner, Pool, *RefsUri, MoveTemp(OnHttpRequestComplete), EHttpMediaType::UnspecifiedContentType, { 401, 404 });
}

void FHttpCacheStore::GetCacheRecordAsync(
	IRequestOwner& Owner,
	const FSharedString& Name,
	const FCacheKey& Key,
	const FCacheRecordPolicy& Policy,
	uint64 UserData,
	TUniqueFunction<void(FCacheGetResponse&& Response, uint64 BytesReceived)>&& OnComplete)
{
	FGetRecordOp::GetRecord(*this, Owner, Name, Key, Policy, UserData, MoveTemp(OnComplete));
}

void FHttpCacheStore::RefCachedDataProbablyExistsBatchAsync(
	IRequestOwner& Owner,
	TConstArrayView<FCacheGetValueRequest> ValueRefs,
	FOnCacheGetValueComplete&& OnComplete)
{
	if (ValueRefs.IsEmpty())
	{
		return;
	}

	if (!IsUsable())
	{
		for (const FCacheGetValueRequest& ValueRef : ValueRefs)
		{
			UE_LOG(LogDerivedDataCache, VeryVerbose,
				TEXT("%s: Skipped exists check of %s from '%s' because this cache store is not available"),
				*Domain, *WriteToString<96>(ValueRef.Key), *ValueRef.Name);
			OnComplete(ValueRef.MakeResponse(EStatus::Error));
		}
		return;
	}

	TStringBuilder<256> RefsUri;
	RefsUri << "api/v1/refs/" << StructuredNamespace;
	FCbWriter RequestWriter;
	RequestWriter.BeginObject();
	RequestWriter.BeginArray(ANSITEXTVIEW("ops"));
	uint32 OpIndex = 0;
	for (const FCacheGetValueRequest& ValueRef : ValueRefs)
	{
		RequestWriter.BeginObject();
		RequestWriter.AddInteger(ANSITEXTVIEW("opId"), OpIndex);
		RequestWriter.AddString(ANSITEXTVIEW("op"), ANSITEXTVIEW("GET"));
		FCacheKey Key = ValueRef.Key;
		FString Bucket(Key.Bucket.ToString());
		Bucket.ToLowerInline();
		RequestWriter.AddString(ANSITEXTVIEW("bucket"), Bucket);
		RequestWriter.AddString(ANSITEXTVIEW("key"), LexToString(Key.Hash));
		RequestWriter.AddBool(ANSITEXTVIEW("resolveAttachments"), true);
		RequestWriter.EndObject();
		++OpIndex;
	}
	RequestWriter.EndArray();
	RequestWriter.EndObject();
	FCbFieldIterator RequestFields = RequestWriter.Save();


	FHttpRequestPool* Pool = nullptr;
	FHttpRequest* Request = WaitForHttpRequestForOwner<OperationCategory::Get>(Owner, false /* bUnboundedOverflow */, Pool);

	Request->AddHeader(TEXT("Accept"), TEXT("application/x-ue-cb"));

	auto OnHttpRequestComplete = [this, &Owner, ValueRefs = TArray<FCacheGetValueRequest>(ValueRefs), OnComplete = MoveTemp(OnComplete)](FHttpRequest::EResult HttpResult, FHttpRequest* Request)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(HttpDDC_RefCachedDataProbablyExistsBatchAsync_OnHttpRequestComplete);

		int64 ResponseCode = Request->GetResponseCode();
		if (FHttpRequest::IsSuccessResponse(ResponseCode))
		{
			FMemoryView ResponseView = MakeMemoryView(Request->GetResponseBuffer().GetData(), Request->GetResponseBuffer().Num());
			if (ValidateCompactBinary(ResponseView, ECbValidateMode::Default) != ECbValidateError::None)
			{
				for (const FCacheGetValueRequest& ValueRef : ValueRefs)
				{
					UE_LOG(LogDerivedDataCache, Log,
						TEXT("%s: Cache exists returned invalid results."),
						*Domain);
						OnComplete(ValueRef.MakeResponse(EStatus::Error));
				}
				return FHttpRequest::ECompletionBehavior::Done;
			}

			const FCbObjectView ResponseObject = FCbObjectView(Request->GetResponseBuffer().GetData());

			FCbArrayView ResultsArrayView = ResponseObject[ANSITEXTVIEW("results")].AsArrayView();

			if (ResultsArrayView.Num() != ValueRefs.Num())
			{
				for (const FCacheGetValueRequest& ValueRef : ValueRefs)
				{
					UE_LOG(LogDerivedDataCache, Log,
						TEXT("%s: Cache exists returned unexpected quantity of results (expected %d, got %d)."),
						*Domain, ValueRefs.Num(), ResultsArrayView.Num());
						OnComplete(ValueRef.MakeResponse(EStatus::Error));
				}
				return FHttpRequest::ECompletionBehavior::Done;
			}

			for (FCbFieldView ResultFieldView : ResultsArrayView)
			{
				FCbObjectView ResultObjectView = ResultFieldView.AsObjectView();
				uint32 OpId = ResultObjectView[ANSITEXTVIEW("opId")].AsUInt32();
				FCbObjectView ResponseObjectView = ResultObjectView[ANSITEXTVIEW("response")].AsObjectView();
				int32 StatusCode = ResultObjectView[ANSITEXTVIEW("statusCode")].AsInt32();

				if (OpId >= (uint32)ValueRefs.Num())
				{
					UE_LOG(LogDerivedDataCache, Display, TEXT("%s: Encountered invalid opId %d while querying %d values"),
						*Domain, OpId, ValueRefs.Num());
					continue;
				}

				const FCacheGetValueRequest& ValueRef = ValueRefs[OpId];

				if (!FHttpRequest::IsSuccessResponse(StatusCode))
				{
					UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Cache miss with unsuccessful response code %d for %s from '%s'"),
						*Domain, StatusCode, *WriteToString<96>(ValueRef.Key), *ValueRef.Name);
					OnComplete(ValueRef.MakeResponse(EStatus::Error));
					continue;
				}

				if (!EnumHasAnyFlags(ValueRef.Policy, ECachePolicy::QueryRemote))
				{
					UE_LOG(LogDerivedDataCache, VeryVerbose, TEXT("%s: Skipped exists check of %s from '%s' due to cache policy"),
						*Domain, *WriteToString<96>(ValueRef.Key), *ValueRef.Name);
					OnComplete(ValueRef.MakeResponse(EStatus::Error));
					continue;
				}

				const FIoHash RawHash = ResponseObjectView[ANSITEXTVIEW("RawHash")].AsHash();
				const uint64 RawSize = ResponseObjectView[ANSITEXTVIEW("RawSize")].AsUInt64(MAX_uint64);
				if (RawHash.IsZero() || RawSize == MAX_uint64)
				{
					UE_LOG(LogDerivedDataCache, Display, TEXT("%s: Cache miss with invalid value for %s from '%s'"),
						*Domain, *WriteToString<96>(ValueRef.Key), *ValueRef.Name);
					OnComplete(ValueRef.MakeResponse(EStatus::Error));
					continue;
				}

				OnComplete({ValueRef.Name, ValueRef.Key, FValue(RawHash, RawSize), ValueRef.UserData, EStatus::Ok});
			}
			return FHttpRequest::ECompletionBehavior::Done;
		}

		if (!ShouldAbortForShutdown() && !Owner.IsCanceled() && ShouldRetryOnError(HttpResult, ResponseCode) && ((Request->GetAttempts()+1) < UE_HTTPDDC_MAX_ATTEMPTS))
		{
			return FHttpRequest::ECompletionBehavior::Retry;
		}

		for (const FCacheGetValueRequest& ValueRef : ValueRefs)
		{
			UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Cache miss with failed HTTP request for %s from '%s'"),
				*Domain, *WriteToString<96>(ValueRef.Key), *ValueRef.Name);
			OnComplete(ValueRef.MakeResponse(EStatus::Error));
		}
		return FHttpRequest::ECompletionBehavior::Done;
	};

	Request->EnqueueAsyncPost(Owner, Pool, *RefsUri, FCompositeBuffer(RequestFields.GetOuterBuffer()), MoveTemp(OnHttpRequestComplete), EHttpMediaType::CbObject);
}

void FHttpCacheStore::LegacyStats(FDerivedDataCacheStatsNode& OutNode)
{
	OutNode = {TEXT("Horde Storage"), FString::Printf(TEXT("%s (%s)"), *Domain, *Namespace), /*bIsLocal*/ false};
	OutNode.UsageStats.Add(TEXT(""), UsageStats);
}

void FHttpCacheStore::Put(
	const TConstArrayView<FCachePutRequest> Requests,
	IRequestOwner& Owner,
	FOnCachePutComplete&& OnComplete)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(HttpDDC_Put);
	FRequestBarrier Barrier(Owner);
	TRefCountedUniqueFunction<FOnCachePutComplete>* CompletionFunction = new TRefCountedUniqueFunction<FOnCachePutComplete>(MoveTemp(OnComplete));
	TRefCountPtr<TRefCountedUniqueFunction<FOnCachePutComplete>> BatchOnCompleteRef(CompletionFunction);
	for (const FCachePutRequest& Request : Requests)
	{
		PutCacheRecordAsync(Owner, Request.Name, Request.Record, Request.Policy, Request.UserData, [COOK_STAT(Timer = UsageStats.TimePut(), ) OnCompletePtr = TRefCountPtr<TRefCountedUniqueFunction<FOnCachePutComplete>>(CompletionFunction)](FCachePutResponse&& Response, uint64 BytesSent) mutable
		{
			TRACE_COUNTER_ADD(HttpDDC_BytesSent, BytesSent);
			if (Response.Status == EStatus::Ok)
			{
				COOK_STAT(if (BytesSent) { Timer.AddHit(BytesSent); });
			}
			OnCompletePtr->GetFunction()(MoveTemp(Response));
		});
	}
}

void FHttpCacheStore::Get(
	const TConstArrayView<FCacheGetRequest> Requests,
	IRequestOwner& Owner,
	FOnCacheGetComplete&& OnComplete)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(HttpDDC_Get);
	FRequestBarrier Barrier(Owner);
	TRefCountedUniqueFunction<FOnCacheGetComplete>* CompletionFunction = new TRefCountedUniqueFunction<FOnCacheGetComplete>(MoveTemp(OnComplete));
	TRefCountPtr<TRefCountedUniqueFunction<FOnCacheGetComplete>> BatchOnCompleteRef(CompletionFunction);
	for (const FCacheGetRequest& Request : Requests)
	{
		GetCacheRecordAsync(Owner, Request.Name, Request.Key, Request.Policy, Request.UserData, [COOK_STAT(Timer = UsageStats.TimePut(), ) OnCompletePtr = TRefCountPtr<TRefCountedUniqueFunction<FOnCacheGetComplete>>(CompletionFunction)](FCacheGetResponse&& Response, uint64 BytesReceived) mutable
		{
			TRACE_COUNTER_ADD(HttpDDC_BytesReceived, BytesReceived);
			if (Response.Status == EStatus::Ok)
			{
				COOK_STAT(Timer.AddHit(BytesReceived););
			}
			OnCompletePtr->GetFunction()(MoveTemp(Response));
		});
	}
}

void FHttpCacheStore::PutValue(
	const TConstArrayView<FCachePutValueRequest> Requests,
	IRequestOwner& Owner,
	FOnCachePutValueComplete&& OnComplete)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(HttpDDC_PutValue);
	FRequestBarrier Barrier(Owner);
	TRefCountedUniqueFunction<FOnCachePutValueComplete>* CompletionFunction = new TRefCountedUniqueFunction<FOnCachePutValueComplete>(MoveTemp(OnComplete));
	TRefCountPtr<TRefCountedUniqueFunction<FOnCachePutValueComplete>> BatchOnCompleteRef(CompletionFunction);
	for (const FCachePutValueRequest& Request : Requests)
	{
		PutCacheValueAsync(Owner, Request.Name, Request.Key, Request.Value, Request.Policy, Request.UserData, [COOK_STAT(Timer = UsageStats.TimePut(),) OnCompletePtr = TRefCountPtr<TRefCountedUniqueFunction<FOnCachePutValueComplete>>(CompletionFunction)](FCachePutValueResponse&& Response, uint64 BytesSent) mutable
		{
			TRACE_COUNTER_ADD(HttpDDC_BytesSent, BytesSent);
			if (Response.Status == EStatus::Ok)
			{
				COOK_STAT(if (BytesSent) { Timer.AddHit(BytesSent); });
			}
			OnCompletePtr->GetFunction()(MoveTemp(Response));
		});
	}
}

void FHttpCacheStore::GetValue(
	const TConstArrayView<FCacheGetValueRequest> Requests,
	IRequestOwner& Owner,
	FOnCacheGetValueComplete&& OnComplete)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(HttpDDC_GetValue);
	COOK_STAT(double StartTime = FPlatformTime::Seconds());
	COOK_STAT(bool bIsInGameThread = IsInGameThread());

	bool bBatchExistsCandidate = true;
	for (const FCacheGetValueRequest& Request : Requests)
	{
		if (!EnumHasAnyFlags(Request.Policy, ECachePolicy::SkipData))
		{
			bBatchExistsCandidate = false;
			break;
		}
	}
	if (bBatchExistsCandidate)
	{
		RefCachedDataProbablyExistsBatchAsync(Owner, Requests,
		[this, COOK_STAT(StartTime, bIsInGameThread, ) OnComplete = MoveTemp(OnComplete)](FCacheGetValueResponse&& Response)
		{
			if (Response.Status != EStatus::Ok)
			{
				COOK_STAT(UsageStats.GetStats.Accumulate(FCookStats::CallStats::EHitOrMiss::Miss, FCookStats::CallStats::EStatType::Counter, 1l, bIsInGameThread));
				OnComplete(MoveTemp(Response));
			}
			else
			{
				UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Cache hit for %s from '%s'"),
					*Domain, *WriteToString<96>(Response.Key), *Response.Name);
				COOK_STAT(UsageStats.GetStats.Accumulate(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Counter, 1l, bIsInGameThread));
				OnComplete(MoveTemp(Response));
			}

			COOK_STAT(const int64 CyclesUsed = int64((FPlatformTime::Seconds() - StartTime) / FPlatformTime::GetSecondsPerCycle()));
			COOK_STAT(UsageStats.GetStats.Accumulate(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Cycles, CyclesUsed, bIsInGameThread));
		});
	}
	else
	{
		FRequestBarrier Barrier(Owner);
		TRefCountedUniqueFunction<FOnCacheGetValueComplete>* CompletionFunction = new TRefCountedUniqueFunction<FOnCacheGetValueComplete>(MoveTemp(OnComplete));
		TRefCountPtr<TRefCountedUniqueFunction<FOnCacheGetValueComplete>> BatchOnCompleteRef(CompletionFunction);
		int64 HitBytes = 0;
		for (const FCacheGetValueRequest& Request : Requests)
		{
			GetCacheValueAsync(Owner, Request.Name, Request.Key, Request.Policy, Request.UserData,
			[this, COOK_STAT(StartTime, bIsInGameThread,) Policy = Request.Policy, OnCompletePtr = TRefCountPtr<TRefCountedUniqueFunction<FOnCacheGetValueComplete>>(CompletionFunction)] (FCacheGetValueResponse&& Response)
			{
				const FOnCacheGetValueComplete& OnComplete = OnCompletePtr->GetFunction();
				check(OnComplete);
				if (Response.Status != EStatus::Ok)
				{
					COOK_STAT(UsageStats.GetStats.Accumulate(FCookStats::CallStats::EHitOrMiss::Miss, FCookStats::CallStats::EStatType::Counter, 1l, bIsInGameThread));
					OnComplete(MoveTemp(Response));
				}
				else
				{
					if (!IsValueDataReady(Response.Value, Policy) && !EnumHasAnyFlags(Policy, ECachePolicy::SkipData))
					{
						// With inline fetching, expect we will always have a value we can use.  Even SkipData/Exists can rely on the blob existing if the ref is reported to exist.
						UE_LOG(LogDerivedDataCache, Log, TEXT("%s: Cache miss due to inlining failure for %s from '%s'"),
									*Domain, *WriteToString<96>(Response.Key), *Response.Name);
						COOK_STAT(UsageStats.GetStats.Accumulate(FCookStats::CallStats::EHitOrMiss::Miss, FCookStats::CallStats::EStatType::Counter, 1l, bIsInGameThread));
						OnComplete(MoveTemp(Response));
					}
					else
					{
						UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Cache hit for %s from '%s'"),
									*Domain, *WriteToString<96>(Response.Key), *Response.Name);
						uint64 ValueSize = Response.Value.GetData().GetCompressedSize();
						TRACE_COUNTER_ADD(HttpDDC_BytesReceived, ValueSize);
						COOK_STAT(UsageStats.GetStats.Accumulate(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Counter, 1l, bIsInGameThread));
						OnComplete({ Response.Name, Response.Key, Response.Value, Response.UserData, EStatus::Ok });
						
						COOK_STAT(UsageStats.GetStats.Accumulate(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Bytes, ValueSize, bIsInGameThread));
					}
				}
				COOK_STAT(const int64 CyclesUsed = int64((FPlatformTime::Seconds() - StartTime) / FPlatformTime::GetSecondsPerCycle()));
				COOK_STAT(UsageStats.GetStats.Accumulate(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Cycles, CyclesUsed, bIsInGameThread));
			});
		}
	}

}

void FHttpCacheStore::GetChunks(
	const TConstArrayView<FCacheGetChunkRequest> Requests,
	IRequestOwner& Owner,
	FOnCacheGetChunkComplete&& OnComplete)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(HttpDDC_GetChunks);
	// TODO: This is inefficient because Jupiter doesn't allow us to get only part of a compressed blob, so we have to
	//		 get the whole thing and then decompress only the portion we need.  Furthermore, because there is no propagation
	//		 between cache stores during chunk requests, the fetched result won't end up in the local store.
	//		 These efficiency issues will be addressed by changes to the Hierarchy that translate chunk requests that
	//		 are missing in local/fast stores and have to be retrieved from slow stores into record requests instead.  That
	//		 will make this code path unused/uncommon as Jupiter will most always be a slow store with a local/fast store in front of it.
	//		 Regardless, to adhere to the functional contract, this implementation must exist.
	TArray<FCacheGetChunkRequest, TInlineAllocator<16>> SortedRequests(Requests);
	SortedRequests.StableSort(TChunkLess());

	bool bHasValue = false;
	FValue Value;
	FValueId ValueId;
	FCacheKey ValueKey;
	FCompressedBuffer ValueBuffer;
	FCompressedBufferReader ValueReader;
	EStatus ValueStatus = EStatus::Error;
	FOptionalCacheRecord Record;
	for (const FCacheGetChunkRequest& Request : SortedRequests)
	{
		const bool bExistsOnly = EnumHasAnyFlags(Request.Policy, ECachePolicy::SkipData);
		COOK_STAT(auto Timer = bExistsOnly ? UsageStats.TimeProbablyExists() : UsageStats.TimeGet());
		if (!(bHasValue && ValueKey == Request.Key && ValueId == Request.Id) || ValueReader.HasSource() < !bExistsOnly)
		{
			ValueStatus = EStatus::Error;
			ValueReader.ResetSource();
			ValueKey = {};
			ValueId.Reset();
			Value.Reset();
			bHasValue = false;
			if (Request.Id.IsValid())
			{
				if (!(Record && Record.Get().GetKey() == Request.Key))
				{
					FCacheRecordPolicyBuilder PolicyBuilder(ECachePolicy::None);
					PolicyBuilder.AddValuePolicy(Request.Id, Request.Policy);
					Record.Reset();

					FRequestOwner BlockingOwner(EPriority::Blocking);
					GetCacheRecordOnlyAsync(BlockingOwner, Request.Name, Request.Key, PolicyBuilder.Build(), 0, [&Record](FGetCacheRecordOnlyResponse&& Response)
					{
						Record = MoveTemp(Response.Record);
					});
					BlockingOwner.Wait();
				}
				if (Record)
				{
					const FValueWithId& ValueWithId = Record.Get().GetValue(Request.Id);
					bHasValue = ValueWithId.IsValid();
					Value = ValueWithId;
					ValueId = Request.Id;
					ValueKey = Request.Key;

					if (IsValueDataReady(Value, Request.Policy))
					{
						ValueReader.SetSource(Value.GetData());
					}
					else
					{
						auto IdGetter = [](const FValueWithId& Value)
						{
							return FString(WriteToString<16>(Value.GetId()));
						};

						FRequestOwner BlockingOwner(EPriority::Blocking);
						bool bSucceeded = false;
						FCompressedBuffer NewBuffer;
						FGetRecordOp::GetDataBatch(*this, BlockingOwner, Request.Name, Request.Key, ::MakeArrayView({ ValueWithId }), IdGetter, [&bSucceeded, &NewBuffer](FGetRecordOp::FGetCachedDataBatchResponse&& Response)
						{
							if (Response.Status == EStatus::Ok)
							{
								bSucceeded = true;
								NewBuffer = MoveTemp(Response.DataBuffer);
							}
						});
						BlockingOwner.Wait();

						if (bSucceeded)
						{
							ValueBuffer = MoveTemp(NewBuffer);
							ValueReader.SetSource(ValueBuffer);
						}
						else
						{
							ValueBuffer.Reset();
							ValueReader.ResetSource();
						}
					}
				}
			}
			else
			{
				ValueKey = Request.Key;

				{
					FRequestOwner BlockingOwner(EPriority::Blocking);
					bool bSucceeded = false;
					GetCacheValueAsync(BlockingOwner, Request.Name, Request.Key, Request.Policy, 0, [&bSucceeded, &Value](FCacheGetValueResponse&& Response)
					{
						Value = MoveTemp(Response.Value);
						bSucceeded = Response.Status == EStatus::Ok;
					});
					BlockingOwner.Wait();
					bHasValue = bSucceeded;
				}

				if (bHasValue)
				{
					if (IsValueDataReady(Value, Request.Policy))
					{
						ValueReader.SetSource(Value.GetData());
					}
					else
					{
						auto IdGetter = [](const FValue& Value)
						{
							return FString(TEXT("Default"));
						};

						FRequestOwner BlockingOwner(EPriority::Blocking);
						bool bSucceeded = false;
						FCompressedBuffer NewBuffer;
						FGetRecordOp::GetDataBatch(*this, BlockingOwner, Request.Name, Request.Key, ::MakeArrayView({ Value }), IdGetter, [&bSucceeded, &NewBuffer](FGetRecordOp::FGetCachedDataBatchResponse&& Response)
						{
							if (Response.Status == EStatus::Ok)
							{
								bSucceeded = true;
								NewBuffer = MoveTemp(Response.DataBuffer);
							}
						});
						BlockingOwner.Wait();

						if (bSucceeded)
						{
							ValueBuffer = MoveTemp(NewBuffer);
							ValueReader.SetSource(ValueBuffer);
						}
						else
						{
							ValueBuffer.Reset();
							ValueReader.ResetSource();
						}
					}
				}
				else
				{
					ValueBuffer.Reset();
					ValueReader.ResetSource();
				}
			}
		}
		if (bHasValue)
		{
			const uint64 RawOffset = FMath::Min(Value.GetRawSize(), Request.RawOffset);
			const uint64 RawSize = FMath::Min(Value.GetRawSize() - RawOffset, Request.RawSize);
			UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s: Cache hit for %s from '%s'"),
				*Domain, *WriteToString<96>(Request.Key, '/', Request.Id), *Request.Name);
			COOK_STAT(Timer.AddHit(!bExistsOnly ? RawSize : 0));
			FSharedBuffer Buffer;
			if (!bExistsOnly)
			{
				Buffer = ValueReader.Decompress(RawOffset, RawSize);
			}
			const EStatus ChunkStatus = bExistsOnly || Buffer.GetSize() == RawSize ? EStatus::Ok : EStatus::Error;
			OnComplete({Request.Name, Request.Key, Request.Id, Request.RawOffset,
				RawSize, Value.GetRawHash(), MoveTemp(Buffer), Request.UserData, ChunkStatus});
			continue;
		}

		OnComplete(Request.MakeResponse(EStatus::Error));
	}
}

void FHttpCacheStoreParams::Parse(const TCHAR* NodeName, const TCHAR* Config)
{
	FString ServerId;
	if (FParse::Value(Config, TEXT("ServerID="), ServerId))
	{
		FString ServerEntry;
		const TCHAR* ServerSection = TEXT("HordeStorageServers");
		if (GConfig->GetString(ServerSection, *ServerId, ServerEntry, GEngineIni))
		{
			Parse(NodeName, *ServerEntry);
		}
		else
		{
			UE_LOG(LogDerivedDataCache, Warning, TEXT("%s: Using ServerID=%s which was not found in [%s]"), NodeName, *ServerId, ServerSection);
		}
	}

	FString OverrideName;

	// Host Params

	FParse::Value(Config, TEXT("Host="), Host);
	if (FParse::Value(Config, TEXT("EnvHostOverride="), OverrideName))
	{
		FString HostEnv = FPlatformMisc::GetEnvironmentVariable(*OverrideName);
		if (!HostEnv.IsEmpty())
		{
			Host = HostEnv;
			UE_LOG(LogDerivedDataCache, Log, TEXT("%s: Found environment override for Host %s=%s"), NodeName, *OverrideName, *Host);
		}
	}
	if (FParse::Value(Config, TEXT("CommandLineHostOverride="), OverrideName))
	{
		if (FParse::Value(FCommandLine::Get(), *(OverrideName + TEXT("=")), Host))
		{
			UE_LOG(LogDerivedDataCache, Log, TEXT("%s: Found command line override for Host %s=%s"), NodeName, *OverrideName, *Host);
		}
	}

	FParse::Bool(Config, TEXT("ResolveHostCanonicalName="), bResolveHostCanonicalName);

	// Namespace Params

	FParse::Value(Config, TEXT("Namespace="), Namespace);
	FParse::Value(Config, TEXT("StructuredNamespace="), StructuredNamespace);

	// OAuth Params

	FParse::Value(Config, TEXT("OAuthProvider="), OAuthProvider);

	if (FParse::Value(Config, TEXT("CommandLineOAuthProviderOverride="), OverrideName))
	{
		if (FParse::Value(FCommandLine::Get(), *(OverrideName + TEXT("=")), OAuthProvider))
		{
			UE_LOG(LogDerivedDataCache, Log, TEXT("%s: Found command line override for OAuthProvider %s=%s"), NodeName, *OverrideName, *OAuthProvider);
		}
	}

	FParse::Value(Config, TEXT("OAuthClientId="), OAuthClientId);
	FParse::Value(Config, TEXT("OAuthSecret="), OAuthSecret);

	if (FParse::Value(Config, TEXT("CommandLineOAuthSecretOverride="), OverrideName))
	{
		if (FParse::Value(FCommandLine::Get(), *(OverrideName + TEXT("=")), OAuthSecret))
		{
			UE_LOG(LogDerivedDataCache, Log, TEXT("%s: Found command line override for OAuthSecret %s=%s"), NodeName, *OverrideName, *OAuthSecret);
		}
	}

	FParse::Value(Config, TEXT("OAuthScope="), OAuthScope);

	FParse::Value(Config, TEXT("OAuthProviderIdentifier="), OAuthProviderIdentifier);

	if (FParse::Value(Config, TEXT("OAuthAccessTokenEnvOverride="), OverrideName))
	{
		FString AccessToken = FPlatformMisc::GetEnvironmentVariable(*OverrideName);
		if (!AccessToken.IsEmpty())
		{
			OAuthAccessToken = AccessToken;
			// we do not echo the access token as its sensitive information
			UE_LOG(LogDerivedDataCache, Log, TEXT("%s: Found oauth access token in %s ."), NodeName, *OverrideName);
		}
	}

	// Cache Params

	FParse::Bool(Config, TEXT("ReadOnly="), bReadOnly);
}

} // UE::DerivedData

#endif // WITH_HTTP_DDC_BACKEND

namespace UE::DerivedData
{

TTuple<ILegacyCacheStore*, ECacheStoreFlags> CreateHttpCacheStore(const TCHAR* NodeName, const TCHAR* Config)
{
#if WITH_HTTP_DDC_BACKEND
	FHttpCacheStoreParams Params;
	Params.Parse(NodeName, Config);

	if (Params.Host.IsEmpty())
	{
		UE_LOG(LogDerivedDataCache, Error, TEXT("%s: Missing required parameter 'Host'"), NodeName);
		return MakeTuple(nullptr, ECacheStoreFlags::None);
	}

	if (Params.Host == TEXT("None"))
	{
		UE_LOG(LogDerivedDataCache, Log, TEXT("%s: Disabled because Host is set to 'None'"), NodeName);
		return MakeTuple(nullptr, ECacheStoreFlags::None);
	}

	if (Params.Namespace.IsEmpty())
	{
		Params.Namespace = FApp::GetProjectName();
		UE_LOG(LogDerivedDataCache, Warning, TEXT("%s: Missing required parameter 'Namespace', falling back to '%s'"), NodeName, *Params.Namespace);
	}

	if (Params.StructuredNamespace.IsEmpty())
	{
		Params.StructuredNamespace = Params.Namespace;
		UE_LOG(LogDerivedDataCache, Warning, TEXT("%s: Missing required parameter 'StructuredNamespace', falling back to '%s'"), NodeName, *Params.StructuredNamespace);
	}

	if (Params.OAuthProvider.IsEmpty())
	{
		UE_LOG(LogDerivedDataCache, Error, TEXT("%s: Missing required parameter 'OAuthProvider'"), NodeName);
		return MakeTuple(nullptr, ECacheStoreFlags::None);
	}

	// No need for OAuth client id and secret if using a local provider.
	if (!Params.OAuthProvider.StartsWith(TEXT("http://localhost")))
	{
		if (Params.OAuthClientId.IsEmpty())
		{
			UE_LOG(LogDerivedDataCache, Error, TEXT("%s: Missing required parameter 'OAuthClientId'"), NodeName);
			return MakeTuple(nullptr, ECacheStoreFlags::None);
		}

		if (Params.OAuthSecret.IsEmpty())
		{
			UE_LOG(LogDerivedDataCache, Error, TEXT("%s: Missing required parameter 'OAuthSecret'"), NodeName);
			return MakeTuple(nullptr, ECacheStoreFlags::None);
		}
	}

	if (Params.OAuthScope.IsEmpty())
	{
		Params.OAuthScope = TEXTVIEW("cache_access");
	}

	TUniquePtr<FHttpCacheStore> Backend = MakeUnique<FHttpCacheStore>(Params);
	if (!Backend->IsUsable())
	{
		UE_LOG(LogDerivedDataCache, Warning, TEXT("%s: Failed to contact the service (%s), will not use it."), NodeName, *Params.Host);
		Backend.Reset();
	}

	return MakeTuple(Backend.Release(), ECacheStoreFlags::Remote | ECacheStoreFlags::Query | (Params.bReadOnly ? ECacheStoreFlags::None : ECacheStoreFlags::Store));
#else
	UE_LOG(LogDerivedDataCache, Warning, TEXT("%s: HTTP cache is not yet supported in the current build configuration."), NodeName);
	return MakeTuple(nullptr, ECacheStoreFlags::None);
#endif
}

ILegacyCacheStore* GetAnyHttpCacheStore(
	FString& OutDomain,
	FString& OutOAuthProvider,
	FString& OutOAuthClientId,
	FString& OutOAuthSecret,
	FString& OutOAuthScope,
	FString& OAuthProviderIdentifier,
	FString& OAuthAccessToken,
	FString& OutNamespace,
	FString& OutStructuredNamespace)
{
#if WITH_HTTP_DDC_BACKEND
	if (FHttpCacheStore* HttpBackend = FHttpCacheStore::GetAny())
	{
		OutDomain = HttpBackend->GetDomain();
		OutOAuthProvider = HttpBackend->GetOAuthProvider();
		OutOAuthClientId = HttpBackend->GetOAuthClientId();
		OutOAuthSecret = HttpBackend->GetOAuthSecret();
		OutOAuthScope = HttpBackend->GetOAuthScope();
		OAuthProviderIdentifier = HttpBackend->GetOAuthProviderIdentifier();
		OAuthAccessToken = HttpBackend->GetOAuthAccessToken();
		OutNamespace = HttpBackend->GetNamespace();
		OutStructuredNamespace = HttpBackend->GetStructuredNamespace();

		return HttpBackend;
	}
	return nullptr;
#else
	return nullptr;
#endif
}

} // UE::DerivedData

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/Array.h"
#include "Containers/ArrayView.h"
#include "Containers/DepletableMpscQueue.h"
#include "Containers/StringFwd.h"
#include "Containers/UnrealString.h"
#include "Dom/JsonObject.h"
#include "Experimental/Async/LazyEvent.h"
#include "Experimental/Containers/FAAArrayQueue.h"
#include "HAL/CriticalSection.h"
#include "HAL/PlatformProcess.h"
#include "HAL/Thread.h"
#include "Memory/CompositeBuffer.h"
#include "Memory/MemoryView.h"
#include "Templates/PimplPtr.h"
#include "Templates/RefCounting.h"
#include "Templates/SharedPointer.h"
#include "Templates/UniquePtr.h"
#include <atomic>

struct curl_slist;
namespace UE { class FHttpRequest; }
namespace UE::DerivedData { class IRequestOwner; }
namespace UE::Http::Private { struct FAsyncRequestData; }
namespace UE::Http::Private { struct FCurlStringListDeleter; }
namespace UE::Http::Private { struct FHttpRequestStatics; }
namespace UE::Http::Private { struct FHttpSharedDataInternals; }
namespace UE::Http::Private { struct FHttpSharedDataStatics; }
namespace UE::Http::Private { using FCurlStringList = TUniquePtr<curl_slist, FCurlStringListDeleter>; }

namespace UE
{

class FHttpRequestPool;

enum class EHttpMethod : uint8
{
	Get,
	Put,
	Post,
	Head,
	Delete,
};

enum class EHttpMediaType : int16
{
	// Negative integers reserved for future custom string content types
	UnspecifiedContentType = 0,
	Any,
	Binary,
	Text,
	Json,
	Yaml,
	CbObject,
	CbPackage,
	CbPackageOffer,
	CompressedBinary,
	FormUrlEncoded,
};

inline FStringView GetHttpMimeType(EHttpMediaType Type)
{
	switch (Type)
	{
		case EHttpMediaType::UnspecifiedContentType:
			checkNoEntry();
			return FStringView();
		case EHttpMediaType::Any:
			return TEXTVIEW("*/*");
		case EHttpMediaType::Binary:
			return TEXTVIEW("application/octet-stream");
		case EHttpMediaType::Text:
			return TEXTVIEW("text/plain");
		case EHttpMediaType::Json:
			return TEXTVIEW("application/json");
		case EHttpMediaType::Yaml:
			return TEXTVIEW("text/yaml");
		case EHttpMediaType::CbObject:
			return TEXTVIEW("application/x-ue-cb");
		case EHttpMediaType::CbPackage:
			return TEXTVIEW("application/x-ue-cbpkg");
		case EHttpMediaType::CbPackageOffer:
			return TEXTVIEW("application/x-ue-offer");
		case EHttpMediaType::CompressedBinary:
			return TEXTVIEW("application/x-ue-comp");
		case EHttpMediaType::FormUrlEncoded:
			return TEXTVIEW("application/x-www-form-urlencoded");
		default:
			return TEXTVIEW("unknown");
	}
}

/**
 * Encapsulation for access token shared by all requests.
 */
class FHttpAccessToken
{
public:
	void SetToken(FStringView Token);
	inline uint32 GetSerial() const { return Serial.load(std::memory_order_relaxed); }
	friend FAnsiStringBuilderBase& operator<<(FAnsiStringBuilderBase& Builder, const FHttpAccessToken& Token);

private:
	mutable FRWLock Lock;
	TArray<ANSICHAR> Header;
	std::atomic<uint32> Serial;
};

class FHttpSharedData
{
public:
	FHttpSharedData(uint32 OverrideMaxConnections = 0);
	~FHttpSharedData();

private:
	void ProcessAsyncRequests();
	void AddRequest(void* Curl);
	void* GetCurlShare() const;

	friend FHttpRequest;
	friend Http::Private::FHttpSharedDataStatics;

	FLazyEvent PendingRequestEvent;
	FThread AsyncServiceThread;
	std::atomic<bool> bAsyncThreadStarting = false;
	std::atomic<bool> bAsyncThreadShutdownRequested = false;

	TPimplPtr<Http::Private::FHttpSharedDataInternals> Internals;
};


/**
 * Minimal HTTP request type wrapping CURL without the need for managers. This request
 * is written to allow reuse of request objects, in order to allow connections to be reused.
 *
 * CURL has a global library initialization (curl_global_init). We rely on this happening in 
 * the Online/HTTP library which is a dependency on this module.
 */
class FHttpRequest
{
public:

	/**
	 * Convenience result type interpreted from HTTP response code.
	 */
	enum class EResult
	{
		Success,
		Failed,
		FailedTimeout
	};

	enum class ECompletionBehavior : uint8
	{
		Done,
		Retry
	};

	using FResultCode = int;

	using FOnHttpRequestComplete = TUniqueFunction<ECompletionBehavior(EResult HttpResult, FHttpRequest* Request)>;

	FHttpRequest(
		FStringView InDomain,
		FStringView InEffectiveDomain,
		const FHttpAccessToken* InAuthorizationToken,
		FHttpSharedData* InSharedData,
		bool bInLogErrors);
	~FHttpRequest();

	/**
	 * Resets all options on the request except those that should always be set.
	 */
	void Reset();

	void PrepareToRetry();

	/** Gets the domain name for this request */
	const FString& GetName() const
	{
		return Domain;
	}

	/** Gets the domain name for this request */
	const FString& GetDomain() const
	{
		return Domain;
	}

	/** Gets the effective domain name for this request */
	const FString& GetEffectiveDomain() const
	{
		return EffectiveDomain;
	}

	/** Returns the HTTP response code.*/
	const int64 GetResponseCode() const
	{
		return ResponseCode;
	}

	/** Returns the number of bytes received this request (headers withstanding). */
	const size_t GetBytesReceived() const
	{
		return BytesReceived;
	}

	/** Returns the number of attempts we've made issuing this request (currently tracked for async requests only). */
	const size_t GetAttempts() const
	{
		return Attempts;
	}

	/** Returns the number of bytes sent during this request (headers withstanding). */
	const size_t GetBytesSent() const
	{
		return BytesSent;
	}

	/**
	 * Upload buffer using the request, using either "Put" or "Post" verbs.
	 * @param Uri Url to use.
	 * @param Buffer Data to upload
	 * @return Result of the request
	 */
	EResult PerformBlockingPut(
		FStringView Uri,
		const FCompositeBuffer& Buffer,
		EHttpMediaType ContentType = EHttpMediaType::UnspecifiedContentType,
		TConstArrayView<long> ExpectedErrorCodes = {});
	EResult PerformBlockingPost(FStringView Uri,
		const FCompositeBuffer& Buffer,
		EHttpMediaType ContentType = EHttpMediaType::UnspecifiedContentType,
		EHttpMediaType AcceptType = EHttpMediaType::UnspecifiedContentType,
		TConstArrayView<long> ExpectedErrorCodes = {});
	void EnqueueAsyncPut(
		DerivedData::IRequestOwner& Owner,
		FHttpRequestPool* Pool,
		FStringView Uri,
		const FCompositeBuffer& Buffer,
		FOnHttpRequestComplete&& OnComplete,
		EHttpMediaType ContentType = EHttpMediaType::UnspecifiedContentType,
		TConstArrayView<long> ExpectedErrorCodes = {});
	void EnqueueAsyncPost(
		DerivedData::IRequestOwner& Owner,
		FHttpRequestPool* Pool,
		FStringView Uri,
		const FCompositeBuffer& Buffer,
		FOnHttpRequestComplete&& OnComplete,
		EHttpMediaType ContentType = EHttpMediaType::UnspecifiedContentType,
		EHttpMediaType AcceptType = EHttpMediaType::UnspecifiedContentType,
		TConstArrayView<long> ExpectedErrorCodes = {});

	/**
	 * Download an url into a buffer using the request.
	 * @param Uri Url to use.
	 * @param Buffer Optional buffer where data should be downloaded to. If empty downloaded data will
	 * be stored in an internal buffer and accessed GetResponse* methods.
	 * @return Result of the request
	 */
	EResult PerformBlockingDownload(
		FStringView Uri,
		TArray64<uint8>* Buffer,
		EHttpMediaType AcceptType = EHttpMediaType::UnspecifiedContentType,
		TConstArrayView<long> ExpectedErrorCodes = {400});
	void EnqueueAsyncDownload(
		DerivedData::IRequestOwner& Owner,
		FHttpRequestPool* Pool,
		FStringView Uri,
		FOnHttpRequestComplete&& OnComplete,
		EHttpMediaType AcceptType = EHttpMediaType::UnspecifiedContentType,
		TConstArrayView<long> ExpectedErrorCodes = {400});

	/**
	 * Query an url using the request. Queries can use either "Head" or "Delete" verbs.
	 * @param Uri Url to use.
	 * @return Result of the request
	 */
	EResult PerformBlockingHead(
		FStringView Uri,
		EHttpMediaType AcceptType = EHttpMediaType::UnspecifiedContentType,
		TConstArrayView<long> ExpectedErrorCodes = {400});
	EResult PerformBlockingDelete(
		FStringView Uri,
		TConstArrayView<long> ExpectedErrorCodes = {400});
	void EnqueueAsyncHead(
		DerivedData::IRequestOwner& Owner,
		FHttpRequestPool* Pool,
		FStringView Uri,
		FOnHttpRequestComplete&& OnComplete,
		EHttpMediaType AcceptType = EHttpMediaType::UnspecifiedContentType,
		TConstArrayView<long> ExpectedErrorCodes = {400});
	void EnqueueAsyncDelete(
		DerivedData::IRequestOwner& Owner,
		FHttpRequestPool* Pool,
		FStringView Uri,
		FOnHttpRequestComplete&& OnComplete,
		TConstArrayView<long> ExpectedErrorCodes = {400});

	/**
	 * Set a header to send with the request.
	 */
	void AddHeader(FStringView Header, FStringView Value);

	/**
	 * Attempts to find the header from the response. Returns false if header is not present.
	 */
	bool GetHeader(FAnsiStringView Header, FString& OutValue) const;

	/**
	 * Returns the response buffer. Note that is the request is performed
	 * with an external buffer as target buffer this string will be empty.
	 */
	const TArray64<uint8>& GetResponseBuffer() const
	{
		return ResponseBuffer;
	}

	FSharedBuffer MoveResponseBufferToShared()
	{
		return MakeSharedBufferFromArray(MoveTemp(ResponseBuffer));
	}

	/**
	 * Returns the response buffer as a string. Note that is the request is performed
	 * with an external buffer as target buffer this string will be empty.
	 */
	FString GetResponseAsString() const
	{
		return GetAnsiBufferAsString(ResponseBuffer);
	}

	/**
	 * Returns the response header as a string.
	 */
	FString GetResponseHeaderAsString()
	{
		return GetAnsiBufferAsString(ResponseHeader);
	}

	/**
	 * Tries to parse the response buffer as a JsonObject. Return empty pointer if 
	 * parse error occurs.
	 */
	TSharedPtr<FJsonObject> GetResponseAsJsonObject() const;

	/**
	 * Tries to parse the response buffer as a JsonArray. Return empty array if
	 * parse error occurs.
	 */
	TArray<TSharedPtr<FJsonValue>> GetResponseAsJsonArray() const;

	void CompleteAsync(FResultCode Result);

	/** Will return true if the response code is considered a success */
	static bool IsSuccessResponse(long ResponseCode)
	{
		// We consider anything in the 1XX or 2XX range a success
		return ResponseCode >= 100 && ResponseCode < 300;
	}

	static bool AllowAsync();

private:
	friend struct Http::Private::FAsyncRequestData;
	friend struct Http::Private::FHttpRequestStatics;

	void* Curl;
	FResultCode ResultCode;
	FHttpSharedData* SharedData;
	Http::Private::FAsyncRequestData* AsyncData;
	long ResponseCode;
	size_t BytesSent;
	size_t BytesReceived;
	size_t Attempts;
	bool bLogErrors;

	FCompositeBuffer ReadCompositeBuffer;
	TArray64<uint8>* WriteDataBufferPtr;
	TArray64<uint8>* WriteHeaderBufferPtr;
	
	TArray64<uint8> ResponseHeader;
	TArray64<uint8> ResponseBuffer;
	TArray<FString> Headers;
	FString Domain;
	FString EffectiveDomain;
	const FHttpAccessToken* AuthorizationToken;

	void AddContentTypeHeader(FStringView Header, EHttpMediaType Type);
	Http::Private::FCurlStringList PrepareToIssueRequest(FStringView Uri, EHttpMethod Verb, uint64 ContentLength);

	/**
	 * Performs the request, blocking until finished.
	 * @param Uri Address on the domain to query
	 * @param Verb HTTP verb to use
	 * @param ContentLength The number of bytes being uploaded for the body of this request.
	 * @param ExpectedErrorCodes An array of expected return codes outside of the success range that should NOT be logged as an abnormal/exceptional outcome.
	 * If unset the response body will be stored in the request.
	 */
	EResult PerformBlocking(FStringView Uri, EHttpMethod Verb, uint64 ContentLength, TConstArrayView<long> ExpectedErrorCodes);

	void EnqueueAsync(
		DerivedData::IRequestOwner& Owner,
		FHttpRequestPool* Pool,
		FStringView Uri,
		EHttpMethod Verb,
		uint64 ContentLength,
		FOnHttpRequestComplete&& OnComplete,
		TConstArrayView<long> ExpectedErrorCodes);

	void LogResult(FResultCode EResult, FStringView Uri, EHttpMethod Verb, TConstArrayView<long> ExpectedErrorCodes) const;

	static FString GetAnsiBufferAsString(TConstArrayView64<uint8> Buffer)
	{
		// Content is NOT null-terminated; we need to specify lengths here
		static_assert(sizeof(UTF8CHAR) == sizeof(uint8));
		return FString(Buffer.Num(), reinterpret_cast<const UTF8CHAR*>(Buffer.GetData()));
	}
};

/**
 * Pool that manages a fixed set of requests. Users are required to release requests that have been 
 * acquired. Usable with \ref FScopedRequestPtr which handles this automatically.
 */
class FHttpRequestPool
{
public:
	FHttpRequestPool(
		FStringView InServiceUrl,
		FStringView InEffectiveServiceUrl,
		const FHttpAccessToken* InAuthorizationToken,
		FHttpSharedData* InSharedData,
		uint32 PoolSize,
		uint32 InOverflowLimit = 0);
	~FHttpRequestPool();

	/**
	 * Attempts to get a request is free. Once a request has been returned it is
	 * "owned by the caller and need to release it to the pool when work has been completed.
	 * @return Usable request instance if one is available, otherwise null.
	 */
	FHttpRequest* GetFreeRequest(bool bUnboundedOverflow = false);

	class FWaiter : public FThreadSafeRefCountedObject
	{
	public:
		std::atomic<FHttpRequest*> Request = nullptr;

		FWaiter(FHttpRequestPool* InPool)
			: Event(FPlatformProcess::GetSynchEventFromPool(true))
			, Pool(InPool)
		{
		}
		
		bool Wait(uint32 TimeMS)
		{
			return Event->Wait(TimeMS);
		}

		void Trigger()
		{
			Event->Trigger();
		}

	private:
		~FWaiter()
		{
			FPlatformProcess::ReturnSynchEventToPool(Event);

			if (Request)
			{
				Pool->ReleaseRequestToPool(Request.exchange(nullptr));
			}
		}

		FEvent* Event;
		FHttpRequestPool* Pool;
	};

	/**
	 * Block until a request is free. Once a request has been returned it is 
	 * "owned by the caller and need to release it to the pool when work has been completed.
	 * @return Usable request instance.
	 */
	FHttpRequest* WaitForFreeRequest(bool bUnboundedOverflow = false);

	/**
	 * Release request to the pool.
	 * @param Request Request that should be freed. Note that any buffer owened by the request can now be reset.
	 */
	void ReleaseRequestToPool(FHttpRequest* Request);

	/**
	 * While holding a request, make it shared across many users.
	 */
	void MakeRequestShared(FHttpRequest* Request, uint8 Users);

private:

	struct FEntry
	{
		std::atomic<uint8> Usage;
		FHttpRequest* Request;
	};

	struct FInitData
	{
		FString ServiceUrl;
		FString EffectiveServiceUrl;
		const FHttpAccessToken* AccessToken;
		FHttpSharedData* SharedData;

		FInitData(
			FStringView InServiceUrl,
			FStringView InEffectiveServiceUrl,
			const FHttpAccessToken* InAccessToken,
			FHttpSharedData* InSharedData)
			: ServiceUrl(InServiceUrl)
			, EffectiveServiceUrl(InEffectiveServiceUrl)
			, AccessToken(InAccessToken)
			, SharedData(InSharedData)
		{
		}
	};

	TArray<FEntry> Pool;
	TArray<FHttpRequest> Requests;
	FAAArrayQueue<FWaiter> Waiters;
	std::atomic<uint32> ActiveOverflowRequests;
	TUniquePtr<const FInitData> InitData;
	const uint32 OverflowLimit;
};

/**
 * Utility class to manage requesting and releasing requests from the \ref FHttpRequestPool.
 */
class FScopedHttpPoolRequestPtr
{
public:
	UE_NONCOPYABLE(FScopedHttpPoolRequestPtr)

	explicit FScopedHttpPoolRequestPtr(FHttpRequestPool* InPool)
		: Request(InPool->WaitForFreeRequest())
		, Pool(InPool)
	{
	}

	~FScopedHttpPoolRequestPtr()
	{
		Pool->ReleaseRequestToPool(Request);
	}

	bool IsValid() const
	{
		return Request != nullptr;
	}

	inline explicit operator bool() const { return IsValid(); }

	FHttpRequest* Get() const
	{
		check(IsValid());
		return Request;
	}

	FHttpRequest* operator->()
	{
		check(IsValid());
		return Request;
	}

private:
	FHttpRequest* Request;
	FHttpRequestPool* Pool;
};

} // UE

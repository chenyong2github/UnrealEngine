// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#define UE_WITH_HTTP_CLIENT PLATFORM_DESKTOP

#if UE_WITH_HTTP_CLIENT
#include "CoreTypes.h"
#include "Containers/Array.h"
#include "Containers/ArrayView.h"
#include "Containers/DepletableMpscQueue.h"
#include "Containers/UnrealString.h"
#include "DerivedDataRequest.h"
#include "DerivedDataRequestOwner.h"
#include "DerivedDataRequestTypes.h"
#include "Dom/JsonObject.h"
#include "Experimental/Async/LazyEvent.h"
#include "Experimental/Containers/FAAArrayQueue.h"
#include "HAL/CriticalSection.h"
#include "HAL/PlatformProcess.h"
#include "HAL/Thread.h"
#include "Memory/MemoryView.h"
#include "Memory/SharedBuffer.h"
#include "Templates/PimplPtr.h"
#include "Templates/SharedPointer.h"
#include <atomic>


struct curl_slist;
namespace UE::Http::Private { struct FAsyncRequestData; }
namespace UE::Http::Private { struct FHttpRequestStatics; }
namespace UE::Http::Private { struct FHttpSharedDataInternals; }
namespace UE::Http::Private { struct FHttpSharedDataStatics; }

namespace UE
{

class FHttpRequestPool;

/**
 * Encapsulation for access token shared by all requests.
 */
class FHttpAccessToken
{
public:
	FHttpAccessToken() = default;
	FString GetHeader();
	void SetHeader(const TCHAR*);
	uint32 GetSerial() const;
private:
	FRWLock Lock;
	FString Token;
	uint32 Serial;
};

class FHttpSharedData
{
public:
	FHttpSharedData();
	~FHttpSharedData();
private:
	friend class FHttpRequest;
	friend struct Http::Private::FHttpSharedDataStatics;

	UE::FLazyEvent PendingRequestEvent;
	FThread AsyncServiceThread;
	std::atomic<bool> bAsyncThreadStarting = false;
	std::atomic<bool> bAsyncThreadShutdownRequested = false;

	TPimplPtr<Http::Private::FHttpSharedDataInternals> Internals;

	void ProcessAsyncRequests();
	void AddRequest(void* Curl);
	void* GetCurlShare() const;
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
	 * Supported request verbs
	 */
	enum RequestVerb
	{
		Get,
		Put,
		PutCompactBinary,
		PutCompressedBlob,
		Post,
		PostCompactBinary,
		PostJson,
		Delete,
		Head
	};

	/**
	 * Convenience result type interpreted from HTTP response code.
	 */
	enum Result
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

	using FOnHttpRequestComplete = TUniqueFunction<ECompletionBehavior(Result HttpResult, FHttpRequest* Request)>;

	FHttpRequest(const TCHAR* InDomain, const TCHAR* InEffectiveDomain, FHttpAccessToken* InAuthorizationToken, FHttpSharedData* InSharedData, bool bInLogErrors);
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
	template<RequestVerb V>
	Result PerformBlockingUpload(const TCHAR* Uri, TArrayView<const uint8> Buffer, TConstArrayView<long> ExpectedErrorCodes = {});
	
	template<RequestVerb V>
	void EnqueueAsyncUpload(UE::DerivedData::IRequestOwner& Owner, FHttpRequestPool* Pool, const TCHAR* Uri, FSharedBuffer Buffer, FOnHttpRequestComplete&& OnComplete, TConstArrayView<long> ExpectedErrorCodes = {});

	/**
	 * Download an url into a buffer using the request.
	 * @param Uri Url to use.
	 * @param Buffer Optional buffer where data should be downloaded to. If empty downloaded data will
	 * be stored in an internal buffer and accessed GetResponse* methods.
	 * @return Result of the request
	 */
	Result PerformBlockingDownload(const TCHAR* Uri, TArray<uint8>* Buffer, TConstArrayView<long> ExpectedErrorCodes = {400});

	void EnqueueAsyncDownload(UE::DerivedData::IRequestOwner& Owner, FHttpRequestPool* Pool, const TCHAR* Uri, FOnHttpRequestComplete&& OnComplete, TConstArrayView<long> ExpectedErrorCodes = {400});

	/**
	 * Query an url using the request. Queries can use either "Head" or "Delete" verbs.
	 * @param Uri Url to use.
	 * @return Result of the request
	 */
	template<RequestVerb V>
	Result PerformBlockingQuery(const TCHAR* Uri, TConstArrayView<long> ExpectedErrorCodes = {400});

	template<RequestVerb V>
	void EnqueueAsyncQuery(UE::DerivedData::IRequestOwner& Owner, FHttpRequestPool* Pool, const TCHAR* Uri, FOnHttpRequestComplete&& OnComplete, TConstArrayView<long> ExpectedErrorCodes = {400});

	/**
	 * Set a header to send with the request.
	 */
	void SetHeader(const TCHAR* Header, const TCHAR* Value);

	/**
	 * Attempts to find the header from the response. Returns false if header is not present.
	 */
	bool GetHeader(const ANSICHAR* Header, FString& OutValue) const;

	/**
	 * Returns the response buffer. Note that is the request is performed
	 * with an external buffer as target buffer this string will be empty.
	 */
	const TArray<uint8>& GetResponseBuffer() const
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

	FSharedBuffer ReadSharedBuffer;
	FMemoryView ReadDataView;
	TArray<uint8>* WriteDataBufferPtr;
	TArray<uint8>* WriteHeaderBufferPtr;
	
	TArray<uint8> ResponseHeader;
	TArray<uint8> ResponseBuffer;
	TArray<FString> Headers;
	FString Domain;
	FString EffectiveDomain;
	FHttpAccessToken* AuthorizationToken;

	curl_slist* PrepareToIssueRequest(const TCHAR* Uri, uint64 ContentLength);

	/**
	 * Performs the request, blocking until finished.
	 * @param Uri Address on the domain to query
	 * @param Verb HTTP verb to use
	 * @param ContentLength The number of bytes being uploaded for the body of this request.
	 * @param ExpectedErrorCodes An array of expected return codes outside of the success range that should NOT be logged as an abnormal/exceptional outcome.
	 * If unset the response body will be stored in the request.
	 */
	Result PerformBlocking(const TCHAR* Uri, RequestVerb Verb, uint64 ContentLength, TConstArrayView<long> ExpectedErrorCodes);

	void EnqueueAsync(UE::DerivedData::IRequestOwner& Owner, FHttpRequestPool* Pool, const TCHAR* Uri, RequestVerb Verb, uint64 ContentLength, FOnHttpRequestComplete&& OnComplete, TConstArrayView<long> ExpectedErrorCodes);

	void LogResult(FResultCode Result, const TCHAR* Uri, RequestVerb Verb, TConstArrayView<long> ExpectedErrorCodes) const;

	FString GetAnsiBufferAsString(const TArray<uint8>& Buffer) const
	{
		// Content is NOT null-terminated; we need to specify lengths here
		FUTF8ToTCHAR TCHARData(reinterpret_cast<const ANSICHAR*>(Buffer.GetData()), Buffer.Num());
		return FString(TCHARData.Length(), TCHARData.Get());
	}

};

#define UE_HTTP_FOREACH_UPLOAD_VERB(op) \
	op(FHttpRequest::Put); \
	op(FHttpRequest::PutCompactBinary); \
	op(FHttpRequest::PutCompressedBlob); \
	op(FHttpRequest::Post); \
	op(FHttpRequest::PostCompactBinary); \
	op(FHttpRequest::PostJson);

#define UE_HTTP_FOREACH_QUERY_VERB(op) \
	op(FHttpRequest::Head) \
	op(FHttpRequest::Delete)

#define UE_HTTP_DECLARE_EXTERN_TEMPLATE(Verb) extern template FHttpRequest::Result FHttpRequest::PerformBlockingUpload<Verb>(const TCHAR* Uri, TArrayView<const uint8> Buffer, TConstArrayView<long> ExpectedErrorCodes);
UE_HTTP_FOREACH_UPLOAD_VERB(UE_HTTP_DECLARE_EXTERN_TEMPLATE)
#undef UE_HTTP_DECLARE_EXTERN_TEMPLATE

#define UE_HTTP_DECLARE_EXTERN_TEMPLATE(Verb) extern template void FHttpRequest::EnqueueAsyncUpload<Verb>(UE::DerivedData::IRequestOwner& Owner, FHttpRequestPool* Pool, const TCHAR* Uri, FSharedBuffer Buffer, FOnHttpRequestComplete&& OnComplete, TConstArrayView<long> ExpectedErrorCodes);
UE_HTTP_FOREACH_UPLOAD_VERB(UE_HTTP_DECLARE_EXTERN_TEMPLATE)
#undef UE_HTTP_DECLARE_EXTERN_TEMPLATE


#define UE_HTTP_DECLARE_EXTERN_TEMPLATE(Verb) extern template FHttpRequest::Result FHttpRequest::PerformBlockingQuery<Verb>(const TCHAR* Uri, TConstArrayView<long> ExpectedErrorCodes);
UE_HTTP_FOREACH_QUERY_VERB(UE_HTTP_DECLARE_EXTERN_TEMPLATE)
#undef UE_HTTP_DECLARE_EXTERN_TEMPLATE

#define UE_HTTP_DECLARE_EXTERN_TEMPLATE(Verb) extern template void FHttpRequest::EnqueueAsyncQuery<Verb>(UE::DerivedData::IRequestOwner& Owner, FHttpRequestPool* Pool, const TCHAR* Uri, FOnHttpRequestComplete&& OnComplete, TConstArrayView<long> ExpectedErrorCodes);
UE_HTTP_FOREACH_QUERY_VERB(UE_HTTP_DECLARE_EXTERN_TEMPLATE)
#undef UE_HTTP_DECLARE_EXTERN_TEMPLATE


/**
 * Pool that manages a fixed set of requests. Users are required to release requests that have been 
 * acquired. Usable with \ref FScopedRequestPtr which handles this automatically.
 */
class FHttpRequestPool
{
public:
	FHttpRequestPool(const TCHAR* InServiceUrl, const TCHAR* InEffectiveServiceUrl, FHttpAccessToken* InAuthorizationToken, FHttpSharedData* InSharedData, uint32 PoolSize, uint32 InOverflowLimit = 0);
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
		std::atomic<FHttpRequest*> Request{ nullptr };

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

	TArray<FEntry> Pool;
	TArray<FHttpRequest> Requests;
	FAAArrayQueue<FWaiter> Waiters;
	std::atomic<uint32> ActiveOverflowRequests;
	struct FInitData
	{
		FString ServiceUrl;
		FString EffectiveServiceUrl;
		FHttpAccessToken* AccessToken;
		FHttpSharedData* SharedData;

		FInitData(const TCHAR* InServiceUrl, const TCHAR* InEffectiveServiceUrl, FHttpAccessToken* InAccessToken, FHttpSharedData* InSharedData)
		: ServiceUrl(InServiceUrl)
		, EffectiveServiceUrl(InEffectiveServiceUrl)
		, AccessToken(InAccessToken)
		, SharedData(InSharedData)
		{
		}
	};
	TUniquePtr<const FInitData> InitData;
	const uint32 OverflowLimit;

	FHttpRequestPool() = delete;
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

#endif // UE_WITH_HTTP_CLIENT

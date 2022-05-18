// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#define WITH_HTTP_CLIENT PLATFORM_DESKTOP

#if WITH_HTTP_CLIENT
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
#include "HAL/CriticalSection.h"
#include "HAL/Thread.h"
#include "Memory/MemoryView.h"
#include "Memory/SharedBuffer.h"
#include "Templates/SharedPointer.h"
#include <atomic>

#if WITH_SSL
#include "Ssl.h"
#include <openssl/ssl.h>
#endif

#if PLATFORM_MICROSOFT
#include "Microsoft/AllowMicrosoftPlatformTypes.h"
#endif

// if there is a platform-specific include then it must be used in place of forward declation in case it defines CURL_STRICTER
#if defined(PLATFORM_CURL_INCLUDE)
#include PLATFORM_CURL_INCLUDE
#else
#include "curl/curl.h"
#endif //defined(PLATFORM_CURL_INCLUDE)

#if PLATFORM_MICROSOFT
#include "Microsoft/HideMicrosoftPlatformTypes.h"
#endif

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
	FRWLock		Lock;
	FString		Token;
	uint32		Serial;
};

class FHttpSharedData
{
public:
	FHttpSharedData();
	~FHttpSharedData();

	void AddRequest(CURL* Curl);
	CURLSH* GetCurlShare() const { return CurlShare; }

private:
	CURLSH* CurlShare;
	CURLM* CurlMulti;
	UE::TDepletableMpscQueue<CURL*> PendingRequestAdditions;
	UE::FLazyEvent PendingRequestEvent;
	FThread AsyncServiceThread;
	std::atomic<bool> bAsyncThreadStarting = false;
	std::atomic<bool> bAsyncThreadShutdownRequested = false;

	FRWLock Locks[CURL_LOCK_DATA_LAST];
	bool WriteLocked[CURL_LOCK_DATA_LAST]{};

	static void LockFn(CURL* Handle, curl_lock_data Data, curl_lock_access Access, void* User);
	static void UnlockFn(CURL* Handle, curl_lock_data Data, void* User);
	void ProcessAsyncRequests();
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
	Result PerformBlockingUpload(const TCHAR* Uri, TArrayView<const uint8> Buffer, TConstArrayView<long> ExpectedErrorCodes = {})
	{
		static_assert(V == Put || V == PutCompactBinary || V == PutCompressedBlob || V == Post || V == PostCompactBinary || V == PostJson, "Upload should use either Put or Post verbs.");
		
		uint64 ContentLength = 0u;

		if constexpr (V == Put || V == PutCompactBinary || V == PutCompressedBlob)
		{
			curl_easy_setopt(Curl, CURLOPT_UPLOAD, 1L);
			curl_easy_setopt(Curl, CURLOPT_INFILESIZE, Buffer.Num());
			curl_easy_setopt(Curl, CURLOPT_READDATA, this);
			curl_easy_setopt(Curl, CURLOPT_READFUNCTION, StaticReadFn);
			if constexpr (V == PutCompactBinary)
			{
				Headers.Add(FString(TEXT("Content-Type: application/x-ue-cb")));
			}
			else if constexpr (V == PutCompressedBlob)
			{
				Headers.Add(FString(TEXT("Content-Type: application/x-ue-comp")));
			}
			else
			{
				Headers.Add(FString(TEXT("Content-Type: application/octet-stream")));
			}
			ContentLength = Buffer.Num();
			ReadDataView = MakeMemoryView(Buffer);
		}
		else if constexpr (V == Post || V == PostCompactBinary || V == PostJson)
		{
			curl_easy_setopt(Curl, CURLOPT_POST, 1L);
			curl_easy_setopt(Curl, CURLOPT_INFILESIZE, Buffer.Num());
			curl_easy_setopt(Curl, CURLOPT_READDATA, this);
			curl_easy_setopt(Curl, CURLOPT_READFUNCTION, StaticReadFn);
			if constexpr (V == PostCompactBinary)
			{
				Headers.Add(FString(TEXT("Content-Type: application/x-ue-cb")));
			}
			else if constexpr (V == PostJson)
			{
				Headers.Add(FString(TEXT("Content-Type: application/json")));
			}
			else
			{
				Headers.Add(FString(TEXT("Content-Type: application/x-www-form-urlencoded")));
			}
			ContentLength = Buffer.Num();
			ReadDataView = MakeMemoryView(Buffer);
		}

		return PerformBlocking(Uri, V, ContentLength, ExpectedErrorCodes);
	}
	
	template<RequestVerb V>
	void EnqueueAsyncUpload(UE::DerivedData::IRequestOwner& Owner, FHttpRequestPool* Pool, const TCHAR* Uri, FSharedBuffer Buffer, FOnHttpRequestComplete&& OnComplete, TConstArrayView<long> ExpectedErrorCodes = {})
	{
		static_assert(V == Put || V == PutCompactBinary || V == PutCompressedBlob || V == Post || V == PostCompactBinary || V == PostJson, "Upload should use either Put or Post verbs.");
		
		uint64 ContentLength = 0u;

		if constexpr (V == Put || V == PutCompactBinary || V == PutCompressedBlob)
		{
			curl_easy_setopt(Curl, CURLOPT_UPLOAD, 1L);
			curl_easy_setopt(Curl, CURLOPT_INFILESIZE, Buffer.GetSize());
			curl_easy_setopt(Curl, CURLOPT_READDATA, this);
			curl_easy_setopt(Curl, CURLOPT_READFUNCTION, StaticReadFn);
			if constexpr (V == PutCompactBinary)
			{
				Headers.Add(FString(TEXT("Content-Type: application/x-ue-cb")));
			}
			else if constexpr (V == PutCompressedBlob)
			{
				Headers.Add(FString(TEXT("Content-Type: application/x-ue-comp")));
			}
			else
			{
				Headers.Add(FString(TEXT("Content-Type: application/octet-stream")));
			}
			ReadSharedBuffer = Buffer;
			ContentLength = Buffer.GetSize();
			ReadDataView = Buffer.GetView();
		}
		else if constexpr (V == Post || V == PostCompactBinary || V == PostJson)
		{
			curl_easy_setopt(Curl, CURLOPT_POST, 1L);
			curl_easy_setopt(Curl, CURLOPT_INFILESIZE, Buffer.GetSize());
			curl_easy_setopt(Curl, CURLOPT_READDATA, this);
			curl_easy_setopt(Curl, CURLOPT_READFUNCTION, StaticReadFn);
			if constexpr (V == PostCompactBinary)
			{
				Headers.Add(FString(TEXT("Content-Type: application/x-ue-cb")));
			}
			else if constexpr (V == PostJson)
			{
				Headers.Add(FString(TEXT("Content-Type: application/json")));
			}
			else
			{
				Headers.Add(FString(TEXT("Content-Type: application/x-www-form-urlencoded")));
			}
			ReadSharedBuffer = Buffer;
			ContentLength = Buffer.GetSize();
			ReadDataView = Buffer.GetView();
		}

		return EnqueueAsync(Owner, Pool, Uri, V, ContentLength, MoveTemp(OnComplete), ExpectedErrorCodes);
	}

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
	Result PerformBlockingQuery(const TCHAR* Uri, TConstArrayView<long> ExpectedErrorCodes = {400})
	{
		static_assert(V == Head || V == Delete, "Queries should use either Head or Delete verbs.");

		if (V == Delete)
		{
			curl_easy_setopt(Curl, CURLOPT_CUSTOMREQUEST, "DELETE");
		}
		else if (V == Head)
		{
			curl_easy_setopt(Curl, CURLOPT_NOBODY, 1L);
		}

		return PerformBlocking(Uri, V, 0u, ExpectedErrorCodes);
	}

	template<RequestVerb V>
	void EnqueueAsyncQuery(UE::DerivedData::IRequestOwner& Owner, FHttpRequestPool* Pool, const TCHAR* Uri, FOnHttpRequestComplete&& OnComplete, TConstArrayView<long> ExpectedErrorCodes = {400})
	{
		static_assert(V == Head || V == Delete, "Queries should use either Head or Delete verbs.");

		if (V == Delete)
		{
			curl_easy_setopt(Curl, CURLOPT_CUSTOMREQUEST, "DELETE");
		}
		else if (V == Head)
		{
			curl_easy_setopt(Curl, CURLOPT_NOBODY, 1L);
		}

		return EnqueueAsync(Owner, Pool, Uri, V, 0u, MoveTemp(OnComplete), ExpectedErrorCodes);
	}

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

	void CompleteAsync(CURLcode Result);

	/** Will return true if the response code is considered a success */
	static bool IsSuccessResponse(long ResponseCode)
	{
		// We consider anything in the 1XX or 2XX range a success
		return ResponseCode >= 100 && ResponseCode < 300;
	}

	static bool AllowAsync();

private:

	struct FAsyncRequestData final : public UE::DerivedData::FRequestBase
	{
		UE::DerivedData::IRequestOwner* Owner = nullptr;
		FHttpRequestPool* Pool = nullptr;
		curl_slist* CurlHeaders = nullptr;
		FString Uri;
		RequestVerb Verb;
		TArray<long, TInlineAllocator<4>> ExpectedErrorCodes;
		FOnHttpRequestComplete OnComplete;
		UE::FLazyEvent Event {EEventMode::ManualReset};

		void Reset()
		{
			if (CurlHeaders)
			{
				curl_slist_free_all(CurlHeaders);
				CurlHeaders = nullptr;
			}
			Uri.Empty();
			ExpectedErrorCodes.Empty();
		}

		void SetPriority(UE::DerivedData::EPriority Priority) final {}

		void Cancel() final
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(HttpDDC_Cancel);
			Event.Wait();
		}

		void Wait() final
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(HttpDDC_Wait);
			Event.Wait();
		}
	};

	CURL*					Curl;
	CURLcode				CurlResult;
	FHttpSharedData*		SharedData;
	FAsyncRequestData*		AsyncData;
	long					ResponseCode;
	size_t					BytesSent;
	size_t					BytesReceived;
	size_t					Attempts;
	bool					bLogErrors;

	FSharedBuffer			ReadSharedBuffer;
	FMemoryView				ReadDataView;
	TArray<uint8>*			WriteDataBufferPtr;
	TArray<uint8>*			WriteHeaderBufferPtr;
	
	TArray<uint8>			ResponseHeader;
	TArray<uint8>			ResponseBuffer;
	TArray<FString>			Headers;
	FString					Domain;
	FString					EffectiveDomain;
	FHttpAccessToken*		AuthorizationToken;

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

	void LogResult(CURLcode Result, const TCHAR* Uri, RequestVerb Verb, TConstArrayView<long> ExpectedErrorCodes) const;

	FString GetAnsiBufferAsString(const TArray<uint8>& Buffer) const
	{
		// Content is NOT null-terminated; we need to specify lengths here
		FUTF8ToTCHAR TCHARData(reinterpret_cast<const ANSICHAR*>(Buffer.GetData()), Buffer.Num());
		return FString(TCHARData.Length(), TCHARData.Get());
	}

	//----------------------------------------------------------------------------------------------------------
	// Certificate checking
	//----------------------------------------------------------------------------------------------------------
	#if WITH_SSL
	static int SslCertVerify(int PreverifyOk, X509_STORE_CTX* Context);
	static CURLcode StaticSSLCTXFn(CURL * curl, void * sslctx, void * parm);
	#endif //#if WITH_SSL

	static size_t StaticDebugCallback(CURL * Handle, curl_infotype DebugInfoType, char * DebugInfo, size_t DebugInfoSize, void* UserData);
	static size_t StaticReadFn(void* Ptr, size_t SizeInBlocks, size_t BlockSizeInBytes, void* UserData);
	static size_t StaticWriteHeaderFn(void* Ptr, size_t SizeInBlocks, size_t BlockSizeInBytes, void* UserData);
	static size_t StaticWriteBodyFn(void* Ptr, size_t SizeInBlocks, size_t BlockSizeInBytes, void* UserData);
	static size_t StaticSeekFn(void* UserData, curl_off_t Offset, int Origin);

};

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

#endif // WITH_HTTP_CLIENT

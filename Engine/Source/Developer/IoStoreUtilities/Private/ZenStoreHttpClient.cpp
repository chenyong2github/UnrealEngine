// Copyright Epic Games, Inc. All Rights Reserved.

#include "ZenStoreHttpClient.h"

#if PLATFORM_WINDOWS

#if PLATFORM_WINDOWS || PLATFORM_HOLOLENS
#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#endif
#include "curl/curl.h"
#if PLATFORM_WINDOWS || PLATFORM_HOLOLENS
#include "Windows/HideWindowsPlatformTypes.h"
#endif

#include "Async/Async.h"
#include "HAL/PlatformFileManager.h"
#include "IO/IoHash.h"
#include "Misc/ScopeLock.h"
#include "Misc/StringBuilder.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/CompactBinaryWriter.h"
#include "Serialization/LargeMemoryWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogZenStore, Log, All);

namespace UE { namespace Zen {

#define	UE_ZENHTTP_BACKEND_WAIT_INTERVAL			0.01f
#define	UE_ZENHTTP_HTTP_REQUEST_TIMEOUT_SECONDS		30L
#define UE_ZENHTTP_HTTP_REQUEST_TIMOUT_ENABLED		1
#define UE_ZENHTTP_HTTP_DEBUG						0
#define UE_ZENHTTP_MAX_FAILED_LOGIN_ATTEMPTS		16
#define UE_ZENHTTP_MAX_ATTEMPTS						4
#define UE_ZENHTTP_MAX_BUFFER_RESERVE				104857600u

/**
 * Minimal HTTP request type wrapping CURL without the need for managers. This request
 * is written to allow reuse of request objects, in order to allow connections to be reused.
 *
 * CURL has a global library initialization (curl_global_init). We rely on this happening in
 * the Online/HTTP library which is a dependency on this module.
 */
class FRequest
{
public:
	/**
	 * Supported request verbs
	 */
	enum RequestVerb
	{
		Get,
		Put,
		Post,
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

	FRequest(FStringView InDomain, bool bInLogErrors)
		: bLogErrors(bInLogErrors)
		, Domain(InDomain)
	{
		Curl = curl_easy_init();
		Reset();
	}

	~FRequest()
	{
		curl_easy_cleanup(Curl);
	}

	/**
	 * Resets all options on the request except those that should always be set.
	 */
	void Reset()
	{
		Headers.Reset();
		ResponseHeader.Reset();
		ResponseBuffer.Reset();
		ResponseCode = 0;
		ReadDataView = TConstArrayView64<uint8>();
		WriteDataBufferPtr = nullptr;
		WriteHeaderBufferPtr = nullptr;
		BytesSent = 0;
		BytesReceived = 0;
		CurlResult = CURL_LAST;

		curl_easy_reset(Curl);

		// Options that are always set for all connections.
#if UE_ZENHTTP_HTTP_REQUEST_TIMOUT_ENABLED
		curl_easy_setopt(Curl, CURLOPT_CONNECTTIMEOUT, UE_ZENHTTP_HTTP_REQUEST_TIMEOUT_SECONDS);
#endif
		curl_easy_setopt(Curl, CURLOPT_FOLLOWLOCATION, 1L);
		curl_easy_setopt(Curl, CURLOPT_NOSIGNAL, 1L);
		// Response functions
		curl_easy_setopt(Curl, CURLOPT_HEADERDATA, this);
		curl_easy_setopt(Curl, CURLOPT_HEADERFUNCTION, &FRequest::StaticWriteHeaderFn);
		curl_easy_setopt(Curl, CURLOPT_WRITEDATA, this);
		curl_easy_setopt(Curl, CURLOPT_WRITEFUNCTION, StaticWriteBodyFn);
		// Rewind method, handle special error case where request need to rewind data stream
		curl_easy_setopt(Curl, CURLOPT_SEEKFUNCTION, StaticSeekFn);
		curl_easy_setopt(Curl, CURLOPT_SEEKDATA, this);
		// Debug hooks
#if UE_ZENHTTP_HTTP_DEBUG
		curl_easy_setopt(Curl, CURLOPT_DEBUGDATA, this);
		curl_easy_setopt(Curl, CURLOPT_DEBUGFUNCTION, StaticDebugCallback);
		curl_easy_setopt(Curl, CURLOPT_VERBOSE, 1L);
#endif
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

	/** Returns the number of bytes sent during this request (headers withstanding). */
	const size_t  GetBytesSent() const
	{
		return BytesSent;
	}

	template<RequestVerb V>
	Result PerformBlockingUpload(FStringView Uri, FCbObjectView Obj)
	{
		FLargeMemoryWriter Out;
		Obj.CopyTo(Out);

		TConstArrayView64<uint8> Payload { Out.GetData(), Out.TotalSize()};
		return PerformBlockingUpload<V>(Uri, Payload);
	}

	Result PerformBlockingPost(FStringView Uri, FCbObjectView Obj)
	{
		FLargeMemoryWriter Out;
		Obj.CopyTo(Out);

		return PerformBlockingPost(Uri, Out.GetView());
	}

	Result PerformBlockingPost(FStringView Uri, FMemoryView Payload)
	{
		uint64 ContentLength = 0u;

		curl_easy_setopt(Curl, CURLOPT_POST, 1L);
		curl_easy_setopt(Curl, CURLOPT_INFILESIZE, Payload.GetSize());
		curl_easy_setopt(Curl, CURLOPT_READDATA, this);
		curl_easy_setopt(Curl, CURLOPT_READFUNCTION, StaticReadFn);

		// TODO: proper Content-Type: header

		ContentLength = Payload.GetSize();
		ReadDataView = { reinterpret_cast<const uint8*>(Payload.GetData()), (int64)Payload.GetSize() };

		return PerformBlocking(Uri, Post, ContentLength);
	}

	/**
	 * Upload buffer using the request, using either "Put" or "Post" verbs.
	 * @param Uri Url to use.
	 * @param Buffer Data to upload
	 * @return Result of the request
	 */
	template<RequestVerb V>
	Result PerformBlockingUpload(FStringView Uri, TConstArrayView64<uint8> Buffer)
	{
		static_assert(V == Put || V == Post, "Upload should use either Put or Post verbs.");

		uint64 ContentLength = 0u;

		if (V == Put)
		{
			curl_easy_setopt(Curl, CURLOPT_UPLOAD, 1L);
			curl_easy_setopt(Curl, CURLOPT_INFILESIZE, Buffer.Num());
			curl_easy_setopt(Curl, CURLOPT_READDATA, this);
			curl_easy_setopt(Curl, CURLOPT_READFUNCTION, StaticReadFn);
			Headers.Add(FString(TEXT("Content-Type: application/octet-stream")));
			ContentLength = Buffer.Num();
			ReadDataView = Buffer;
		}
		else if (V == Post)
		{
			curl_easy_setopt(Curl, CURLOPT_POST, 1L);
			curl_easy_setopt(Curl, CURLOPT_INFILESIZE, Buffer.Num());
			curl_easy_setopt(Curl, CURLOPT_READDATA, this);
			curl_easy_setopt(Curl, CURLOPT_READFUNCTION, StaticReadFn);
			
			//
			// TODO: proper Content-Type: header
			//Headers.Add(V == Post ? FString(TEXT("Content-Type: application/x-www-form-urlencoded")) : FString(TEXT("Content-Type: application/json")));

			ContentLength = Buffer.Num();
			ReadDataView = Buffer;
		}

		return PerformBlocking(Uri, V, ContentLength);
	}

	/**
	 * Download an url into a buffer using the request.
	 * @param Uri Url to use.
	 * @param Buffer Optional buffer where data should be downloaded to. If empty downloaded data will
	 * be stored in an internal buffer and accessed GetResponse* methods.
	 * @return Result of the request
	 */
	Result PerformBlockingDownload(FStringView Uri, TArray64<uint8>* Buffer)
	{
		curl_easy_setopt(Curl, CURLOPT_HTTPGET, 1L);
		WriteDataBufferPtr = Buffer;

		return PerformBlocking(Uri, Get, 0u);
	}

	/**
	 * Query an url using the request. Queries can use either "Head" or "Delete" verbs.
	 * @param Uri Url to use.
	 * @return Result of the request
	 */
	template<RequestVerb V>
	Result PerformBlockingQuery(FStringView Uri)
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

		return PerformBlocking(Uri, V, 0u);
	}

	/**
	 * Set a header to send with the request.
	 */
	void SetHeader(const TCHAR* Header, const TCHAR* Value)
	{
		check(CurlResult == CURL_LAST); // Cannot set header after request is sent
		Headers.Add(FString::Printf(TEXT("%s: %s"), Header, Value));
	}

	/**
	 * Attempts to find the header from the response. Returns false if header is not present.
	 */
	bool GetHeader(const ANSICHAR* Header, FString& OutValue) const
	{
		check(CurlResult != CURL_LAST);  // Cannot query headers before request is sent

		const ANSICHAR* HeadersBuffer = (const ANSICHAR*)ResponseHeader.GetData();
		size_t HeaderLen = strlen(Header);

		// Find the header key in the (ANSI) response buffer. If not found we can exist immediately
		if (const ANSICHAR* Found = strstr(HeadersBuffer, Header))
		{
			const ANSICHAR* Linebreak = strchr(Found, '\r');
			const ANSICHAR* ValueStart = Found + HeaderLen + 2; //colon and space
			const size_t ValueSize = Linebreak - ValueStart;
			FUTF8ToTCHAR TCHARData(ValueStart, ValueSize);
			OutValue = FString(TCHARData.Length(), TCHARData.Get());
			return true;
		}
		return false;
	}

	/**
	 * Returns the response buffer. Note that is the request is performed
	 * with an external buffer as target buffer this string will be empty.
	 */
	const TArray64<uint8>& GetResponseBuffer() const
	{
		return ResponseBuffer;
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

	/** Will return true if the response code is considered a success */
	static bool IsSuccessResponse(long ResponseCode)
	{
		// We consider anything in the 1XX or 2XX range a success
		return ResponseCode >= 100 && ResponseCode < 300;
	}

private:

	CURL* Curl;
	CURLcode				CurlResult;
	long					ResponseCode;
	size_t					BytesSent;
	size_t					BytesReceived;
	bool					bLogErrors;

	TConstArrayView64<uint8>	ReadDataView;
	TArray64<uint8>*			WriteDataBufferPtr;
	TArray64<uint8>*			WriteHeaderBufferPtr;

	TArray64<uint8>			ResponseHeader;
	TArray64<uint8>			ResponseBuffer;
	TArray<FString>			Headers;
	FString					Domain;

	/**
	 * Performs the request, blocking until finished.
	 * @param Uri Address on the domain to query
	 * @param Verb HTTP verb to use
	 * @param Buffer Optional buffer to directly receive the result of the request.
	 * If unset the response body will be stored in the request.
	 */
	Result PerformBlocking(FStringView Uri, RequestVerb Verb, uint64 ContentLength)
	{
		static const char* CommonHeaders[] = {
			"User-Agent: UE",
			nullptr
		};

		TRACE_CPUPROFILER_EVENT_SCOPE(ZenStore_CurlPerform);

		// Setup request options
		TStringBuilder<128> Url;
		Url << Domain << Uri;
		
		curl_easy_setopt(Curl, CURLOPT_URL, TCHAR_TO_ANSI(*Url));

		// Setup response header buffer. If caller has not setup a response data buffer, use interal.
		WriteHeaderBufferPtr = &ResponseHeader;
		if (WriteDataBufferPtr == nullptr)
		{
			WriteDataBufferPtr = &ResponseBuffer;
		}

		// Content-Length should always be set
		Headers.Add(FString::Printf(TEXT("Content-Length: %llu"), ContentLength));

		// Build headers list
		curl_slist* CurlHeaders = nullptr;
		// Add common headers
		for (uint8 i = 0; CommonHeaders[i] != nullptr; ++i)
		{
			CurlHeaders = curl_slist_append(CurlHeaders, CommonHeaders[i]);
		}
		// Setup added headers
		for (const FString& Header : Headers)
		{
			CurlHeaders = curl_slist_append(CurlHeaders, TCHAR_TO_ANSI(*Header));
		}
		curl_easy_setopt(Curl, CURLOPT_HTTPHEADER, CurlHeaders);

		// Shots fired!
		CurlResult = curl_easy_perform(Curl);

		// Get response code
		bool bRedirected = false;
		if (CURLE_OK == curl_easy_getinfo(Curl, CURLINFO_RESPONSE_CODE, &ResponseCode))
		{
			bRedirected = (ResponseCode >= 300 && ResponseCode < 400);
		}

		LogResult(CurlResult, Uri, Verb);

		// Clean up
		curl_slist_free_all(CurlHeaders);

		return CurlResult == CURLE_OK ? Success : Failed;
	}

	void LogResult(CURLcode Result, FStringView Uri, RequestVerb Verb) const
	{
		if (Result == CURLE_OK)
		{
			bool bSuccess = false;
			const TCHAR* VerbStr = nullptr;
			FString AdditionalInfo;

			switch (Verb)
			{
			case Head:
				bSuccess = (ResponseCode == 400 || IsSuccessResponse(ResponseCode));
				VerbStr = TEXT("querying");
				break;
			case Get:
				bSuccess = (ResponseCode == 400 || IsSuccessResponse(ResponseCode));
				VerbStr = TEXT("fetching");
				AdditionalInfo = FString::Printf(TEXT("Received: %d bytes."), BytesReceived);
				break;
			case Put:
				bSuccess = IsSuccessResponse(ResponseCode);
				VerbStr = TEXT("updating");
				AdditionalInfo = FString::Printf(TEXT("Sent: %d bytes."), BytesSent);
				break;
			case Post:
				bSuccess = IsSuccessResponse(ResponseCode);
				VerbStr = TEXT("posting");
				break;
			case Delete:
				bSuccess = IsSuccessResponse(ResponseCode);
				VerbStr = TEXT("deleting");
				break;
			}

			if (bSuccess)
			{
				UE_LOG(
					LogZenStore,
					Verbose,
					TEXT("Finished %s Zen store HTTP operation (response %d) from %s. %s"),
					VerbStr,
					ResponseCode,
					*FString(Uri),
					*AdditionalInfo
				);
			}
			else if (bLogErrors)
			{
				// Print the response body if we got one, otherwise print header.
				FString Response = GetAnsiBufferAsString(ResponseBuffer.Num() > 0 ? ResponseBuffer : ResponseHeader);
				Response.ReplaceCharInline('\n', ' ');
				Response.ReplaceCharInline('\r', ' ');
				// Dont log access denied as error, since tokens can expire mid session
				if (ResponseCode == 401)
				{
					UE_LOG(
						LogZenStore,
						Verbose,
						TEXT("Failed %s Zen store HTTP operation (response %d) from %s. Response: %s"),
						VerbStr,
						ResponseCode,
						*FString(Uri),
						*Response
					);
				}
				else if (ResponseCode == 404)
				{
					// 404 is not an error, it is an expected and valid result in most cases
				}
				else
				{
					UE_LOG(
						LogZenStore,
						Display,
						TEXT("Failed %s Zen store HTTP operation (response %d) from %s. Response: %s"),
						VerbStr,
						ResponseCode,
						*FString(Uri),
						*Response
					);
				}
			}
		}
		else if (bLogErrors)
		{
			UE_LOG(
				LogZenStore,
				Display,
				TEXT("Error while connecting to %s: %s"),
				*Domain,
				ANSI_TO_TCHAR(curl_easy_strerror(Result))
			);
		}
	}

	FString GetAnsiBufferAsString(const TArray64<uint8>& Buffer) const
	{
		// Content is NOT null-terminated; we need to specify lengths here
		FUTF8ToTCHAR TCHARData(reinterpret_cast<const ANSICHAR*>(Buffer.GetData()), Buffer.Num());
		return FString(TCHARData.Length(), TCHARData.Get());
	}

	static size_t StaticDebugCallback(CURL* Handle, curl_infotype DebugInfoType, char* DebugInfo, size_t DebugInfoSize, void* UserData)
	{
		FRequest* Request = static_cast<FRequest*>(UserData);

		switch (DebugInfoType)
		{
		case CURLINFO_TEXT:
		{
			// Truncate at 1023 characters. This is just an arbitrary number based on a buffer size seen in
			// the libcurl code.
			DebugInfoSize = FMath::Min(DebugInfoSize, (size_t)1023);

			// Calculate the actual length of the string due to incorrect use of snprintf() in lib/vtls/openssl.c.
			char* FoundNulPtr = (char*)memchr(DebugInfo, 0, DebugInfoSize);
			int CalculatedSize = FoundNulPtr != nullptr ? FoundNulPtr - DebugInfo : DebugInfoSize;

			auto ConvertedString = StringCast<TCHAR>(static_cast<const ANSICHAR*>(DebugInfo), CalculatedSize);
			FString DebugText(ConvertedString.Length(), ConvertedString.Get());
			DebugText.ReplaceInline(TEXT("\n"), TEXT(""), ESearchCase::CaseSensitive);
			DebugText.ReplaceInline(TEXT("\r"), TEXT(""), ESearchCase::CaseSensitive);
			UE_LOG(LogZenStore, VeryVerbose, TEXT("%p: '%s'"), Request, *DebugText);
		}
		break;

		case CURLINFO_HEADER_IN:
			UE_LOG(LogZenStore, VeryVerbose, TEXT("%p: Received header (%d bytes)"), Request, DebugInfoSize);
			break;

		case CURLINFO_DATA_IN:
			UE_LOG(LogZenStore, VeryVerbose, TEXT("%p: Received data (%d bytes)"), Request, DebugInfoSize);
			break;

		case CURLINFO_DATA_OUT:
			UE_LOG(LogZenStore, VeryVerbose, TEXT("%p: Sent data (%d bytes)"), Request, DebugInfoSize);
			break;

		case CURLINFO_SSL_DATA_IN:
			UE_LOG(LogZenStore, VeryVerbose, TEXT("%p: Received SSL data (%d bytes)"), Request, DebugInfoSize);
			break;

		case CURLINFO_SSL_DATA_OUT:
			UE_LOG(LogZenStore, VeryVerbose, TEXT("%p: Sent SSL data (%d bytes)"), Request, DebugInfoSize);
			break;
		}

		return 0;
	}

	static size_t StaticReadFn(void* Ptr, size_t SizeInBlocks, size_t BlockSizeInBytes, void* UserData)
	{
		FRequest* Request = static_cast<FRequest*>(UserData);
		TConstArrayView64<uint8>& ReadDataView = Request->ReadDataView;

		const size_t Offset = Request->BytesSent;
		const size_t ReadSize = FMath::Min((size_t)ReadDataView.Num() - Offset, SizeInBlocks * BlockSizeInBytes);
		check((size_t)ReadDataView.Num() >= Offset + ReadSize);

		FMemory::Memcpy(Ptr, ReadDataView.GetData() + Offset, ReadSize);
		Request->BytesSent += ReadSize;
		return ReadSize;

		return 0;
	}

	static size_t StaticWriteHeaderFn(void* Ptr, size_t SizeInBlocks, size_t BlockSizeInBytes, void* UserData)
	{
		FRequest* Request = static_cast<FRequest*>(UserData);
		const size_t WriteSize = SizeInBlocks * BlockSizeInBytes;
		TArray64<uint8>* WriteHeaderBufferPtr = Request->WriteHeaderBufferPtr;
		if (WriteHeaderBufferPtr && WriteSize > 0)
		{
			const size_t CurrentBufferLength = WriteHeaderBufferPtr->Num();
			if (CurrentBufferLength > 0)
			{
				// Remove the previous zero termination
				(*WriteHeaderBufferPtr)[CurrentBufferLength - 1] = ' ';
			}

			// Write the header
			WriteHeaderBufferPtr->Append((const uint8*)Ptr, WriteSize + 1);
			(*WriteHeaderBufferPtr)[WriteHeaderBufferPtr->Num() - 1] = 0; // Zero terminate string
			return WriteSize;
		}
		return 0;
	}

	static size_t StaticWriteBodyFn(void* Ptr, size_t SizeInBlocks, size_t BlockSizeInBytes, void* UserData)
	{
		FRequest* Request = static_cast<FRequest*>(UserData);
		const size_t WriteSize = SizeInBlocks * BlockSizeInBytes;
		TArray64<uint8>* WriteDataBufferPtr = Request->WriteDataBufferPtr;

		if (WriteDataBufferPtr && WriteSize > 0)
		{
			// If this is the first part of the body being received, try to reserve 
			// memory if content length is defined in the header.
			if (Request->BytesReceived == 0 && Request->WriteHeaderBufferPtr)
			{
				static const ANSICHAR* ContentLengthHeaderStr = "Content-Length: ";
				const ANSICHAR* Header = (const ANSICHAR*)Request->WriteHeaderBufferPtr->GetData();

				if (const ANSICHAR* ContentLengthHeader = FCStringAnsi::Strstr(Header, ContentLengthHeaderStr))
				{
					size_t ContentLength = (size_t)FCStringAnsi::Atoi64(ContentLengthHeader + strlen(ContentLengthHeaderStr));
					if (ContentLength > 0u && ContentLength < UE_ZENHTTP_MAX_BUFFER_RESERVE)
					{
						WriteDataBufferPtr->Reserve(ContentLength);
					}
				}
			}

			// Write to the target buffer
			WriteDataBufferPtr->Append((const uint8*)Ptr, WriteSize);
			Request->BytesReceived += WriteSize;
			return WriteSize;
		}

		return 0;
	}

	static size_t StaticSeekFn(void* UserData, curl_off_t Offset, int Origin)
	{
		FRequest* Request = static_cast<FRequest*>(UserData);
		size_t NewPosition = 0;

		switch (Origin)
		{
		case SEEK_SET: NewPosition = Offset; break;
		case SEEK_CUR: NewPosition = Request->BytesSent + Offset; break;
		case SEEK_END: NewPosition = Request->ReadDataView.Num() + Offset; break;
		}

		// Make sure we don't seek outside of the buffer
		if (NewPosition < 0 || NewPosition >= (size_t)Request->ReadDataView.Num())
		{
			return CURL_SEEKFUNC_FAIL;
		}

		// Update the used offset
		Request->BytesSent = NewPosition;
		return CURL_SEEKFUNC_OK;
	}

};

//----------------------------------------------------------------------------------------------------------
// Request pool
//----------------------------------------------------------------------------------------------------------

/**
 * Pool that manages a fixed set of requests. Users are required to release requests that have been
 * acquired. Usable with \ref FScopedRequestPtr which handles this automatically.
 */
struct FRequestPool
{
	FRequestPool(FStringView InServiceUrl, uint32 PoolSize)
	{
		Pool.AddUninitialized(PoolSize);
		for (uint8 i = 0; i < Pool.Num(); ++i)
		{
			Pool[i].Usage = 0u;
			Pool[i].Request = new FRequest(InServiceUrl, true);
		}
	}

	~FRequestPool()
	{
		for (uint8 i = 0; i < Pool.Num(); ++i)
		{
			// No requests should be in use by now.
			check(Pool[i].Usage.load(std::memory_order_acquire) == 0u);
			delete Pool[i].Request;
		}
	}

	/**
	 * Attempts to get a request is free. Once a request has been returned it is
	 * "owned by the caller and need to release it to the pool when work has been completed.
	 * @return Usable request instance if one is available, otherwise null.
	 */
	FRequest* GetFreeRequest()
	{
		for (uint8 i = 0; i < Pool.Num(); ++i)
		{
			if (!Pool[i].Usage.load(std::memory_order_relaxed))
			{
				uint8 Expected = 0u;
				if (Pool[i].Usage.compare_exchange_strong(Expected, 1u))
				{
					Pool[i].Request->Reset();
					return Pool[i].Request;
				}
			}
		}
		return nullptr;
	}

	/**
	 * Block until a request is free. Once a request has been returned it is
	 * "owned by the caller and need to release it to the pool when work has been completed.
	 * @return Usable request instance.
	 */
	FRequest* WaitForFreeRequest()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ZenStore_WaitForConnPool);
		FRequest* Request = nullptr;
		while (true)
		{
			Request = GetFreeRequest();
			if (Request != nullptr)
				break;
			FPlatformProcess::Sleep(UE_ZENHTTP_BACKEND_WAIT_INTERVAL);
		}
		return Request;
	}

	/**
	 * Release request to the pool.
	 * @param Request Request that should be freed. Note that any buffer owned by the request can now be reset.
	 */
	void ReleaseRequestToPool(FRequest* Request)
	{
		for (uint8 i = 0; i < Pool.Num(); ++i)
		{
			if (Pool[i].Request == Request)
			{

				Pool[i].Usage--;
				return;
			}
		}
		check(false);
	}

	/**
	 * While holding a request, make it shared across many users.
	 */
	void MakeRequestShared(FRequest* Request, uint8 Users)
	{
		check(Users != 0);
		for (uint8 i = 0; i < Pool.Num(); ++i)
		{
			if (Pool[i].Request == Request)
			{

				Pool[i].Usage = Users;
				return;
			}
		}
		check(false);
	}

private:

	struct FEntry
	{
		std::atomic<uint8> Usage;
		FRequest* Request;
	};

	TArray<FEntry> Pool;

	FRequestPool() = delete;
};

//----------------------------------------------------------------------------------------------------------
// FScopedRequestPtr
//----------------------------------------------------------------------------------------------------------

/**
 * Utility class to manage requesting and releasing requests from the \ref FRequestPool.
 */
struct FScopedRequestPtr
{
public:
	FScopedRequestPtr(FRequestPool* InPool)
		: Request(InPool->WaitForFreeRequest())
		, Pool(InPool)
	{}

	~FScopedRequestPtr()
	{
		Pool->ReleaseRequestToPool(Request);
	}

	bool IsValid() const
	{
		return Request != nullptr;
	}

	FRequest* Get() const
	{
		check(IsValid());
		return Request;
	}

	FRequest* operator->()
	{
		check(IsValid());
		return Request;
	}

private:
	FRequest* Request;
	FRequestPool* Pool;
};

} /* Zen */ 

FZenStoreHttpClient::FZenStoreHttpClient(const FStringView InHostName, uint16 InPort)
: HostName(InHostName)
, Port(InPort)
{
	TStringBuilder<64> Uri;
	Uri.AppendAnsi("http://");
	Uri.Append(InHostName);
	Uri.AppendAnsi(":");
	Uri << InPort;

	RequestPool = MakeUnique<Zen::FRequestPool>(Uri, 32);
}

FZenStoreHttpClient::~FZenStoreHttpClient()
{
}

void 
FZenStoreHttpClient::Initialize(FStringView InProjectId, 
	FStringView InOplogId, 
	FStringView ServerRoot,
	FStringView EngineRoot,
	FStringView ProjectRoot,
	bool		IsCleanBuild)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ZenStoreHttp_Initialize);

	UE_LOG(LogZenStore, Display, TEXT("Establishing oplog %s / %s"), *FString(InProjectId), *FString(InOplogId));

	// Establish project

	{
		UE::Zen::FScopedRequestPtr Request(RequestPool.Get());

		TStringBuilder<128> ProjectUri;
		ProjectUri << "/prj/" << InProjectId;
		TArray64<uint8> GetBuffer;
		UE::Zen::FRequest::Result Res = Request->PerformBlockingDownload(ProjectUri, &GetBuffer);

		// TODO: how to handle failure here? This is probably the most likely point of failure
		// if the service is not up or not responding

		if (Res == Zen::FRequest::Success && Request->GetResponseCode() == 200)
		{
			UE_LOG(LogZenStore, Display, TEXT("Zen project '%s' already exists"), *FString(InProjectId));
		}
		else
		{
			Request->Reset();

			FCbWriter ProjInfo;
			ProjInfo.BeginObject();
			ProjInfo << "id" << InProjectId;
			ProjInfo << "root" << ServerRoot;
			ProjInfo << "engine" << EngineRoot;
			ProjInfo << "project" << ProjectRoot;
			ProjInfo.EndObject();

			Res = Request->PerformBlockingPost(ProjectUri, ProjInfo.Save().AsObject());

			if (Res != Zen::FRequest::Success)
			{
				UE_LOG(LogZenStore, Error, TEXT("Zen project '%s' creation FAILED"), *FString(InProjectId));

				// TODO: how to recover / handle this?
			}
			else if (Request->GetResponseCode() == 201)
			{
				UE_LOG(LogZenStore, Display, TEXT("Zen project '%s' created"), *FString(InProjectId));
			}
			else
			{
				UE_LOG(LogZenStore, Warning, TEXT("Zen project '%s' creation returned success but not HTTP 201"), *FString(InProjectId));
			}
		}
	}

	// Establish oplog

	{
		UE::Zen::FScopedRequestPtr Request(RequestPool.Get());

		TStringBuilder<128> OplogUri;
		OplogUri << "/prj/" << InProjectId << "/oplog/" << InOplogId;

		OplogPath = OplogUri;

		if (IsCleanBuild)
		{
			UE_LOG(LogZenStore, Display, TEXT("Deleting oplog '%s'/'%s' if it exists"), *FString(InProjectId), *FString(InOplogId));
			Request->PerformBlockingQuery<Zen::FRequest::Delete>(OplogUri);
			Request->Reset();
		}

		TArray64<uint8> GetBuffer;
		UE::Zen::FRequest::Result Res = Request->PerformBlockingDownload(OplogUri, &GetBuffer);
		FCbObjectView OplogInfo;

		if (Res == Zen::FRequest::Success && Request->GetResponseCode() == 200)
		{
			UE_LOG(LogZenStore, Display, TEXT("Zen oplog '%s'/'%s' already exists"), *FString(InProjectId), *FString(InOplogId));

			OplogInfo = FCbObjectView(GetBuffer.GetData());
		}
		else
		{
			TConstArrayView64<uint8> Payload;

			Request->Reset();
			Res = Request->PerformBlockingUpload<Zen::FRequest::Post>(OplogUri, Payload);

			if (Res != Zen::FRequest::Success)
			{
				UE_LOG(LogZenStore, Error, TEXT("Zen oplog '%s'/'%s' creation FAILED"), *FString(InProjectId), *FString(InOplogId));

				// TODO: how to recover / handle this?
			}
			else if (Request->GetResponseCode() == 201)
			{
				UE_LOG(LogZenStore, Display, TEXT("Zen oplog '%s'/'%s' created"), *FString(InProjectId), *FString(InOplogId));
			}
			else
			{
				UE_LOG(LogZenStore, Warning, TEXT("Zen oplog '%s'/'%s' creation returned success but not HTTP 201"), *FString(InProjectId), *FString(InOplogId));
			}

			// Issue another GET to retrieve information

			GetBuffer.Reset();
			Request->Reset();
			Res = Request->PerformBlockingDownload(OplogUri, &GetBuffer);

			if (Res == Zen::FRequest::Success && Request->GetResponseCode() == 200)
			{
				OplogInfo = FCbObjectView(GetBuffer.GetData());
			}
		}

		TempDirPath = FUTF8ToTCHAR(OplogInfo["tempdir"].AsString());
	}

	{
		TStringBuilder<128> OplogUri;
		OplogUri << "/prj/" << InProjectId << "/oplog/" << InOplogId << "/new";

		OplogNewEntryPath = OplogUri;
	}

	OplogPrepNewEntryPath = TStringBuilder<128>().AppendAnsi("/prj/").Append(InProjectId).AppendAnsi("/oplog/").Append(InOplogId).AppendAnsi("/prep");

	bAllowRead = true;
	bAllowEdit = true;
}

void FZenStoreHttpClient::InitializeReadOnly(FStringView InProjectId, FStringView InOplogId)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ZenStoreHttp_InitializeReadOnly);

	UE_LOG(LogZenStore, Display, TEXT("Establishing oplog %s / %s"), *FString(InProjectId), *FString(InOplogId));

	// Establish project

	{
		UE::Zen::FScopedRequestPtr Request(RequestPool.Get());

		TStringBuilder<128> ProjectUri;
		ProjectUri << "/prj/" << InProjectId;
		TArray64<uint8> GetBuffer;
		UE::Zen::FRequest::Result Res = Request->PerformBlockingDownload(ProjectUri, &GetBuffer);

		// TODO: how to handle failure here? This is probably the most likely point of failure
		// if the service is not up or not responding

		if (Res != Zen::FRequest::Success || Request->GetResponseCode() != 200)
		{
			UE_LOG(LogZenStore, Fatal, TEXT("Zen project '%s' not found"), *FString(InProjectId));
		}
	}

	// Establish oplog

	{
		UE::Zen::FScopedRequestPtr Request(RequestPool.Get());

		TStringBuilder<128> OplogUri;
		OplogUri << "/prj/" << InProjectId << "/oplog/" << InOplogId;

		OplogPath = OplogUri;

		TArray64<uint8> GetBuffer;
		UE::Zen::FRequest::Result Res = Request->PerformBlockingDownload(OplogUri, &GetBuffer);

		if (Res != Zen::FRequest::Success || Request->GetResponseCode() != 200)
		{
			UE_LOG(LogZenStore, Fatal, TEXT("Zen oplog '%s'/'%s' not found"), *FString(InProjectId), *FString(InOplogId));
		}
	}

	bAllowRead = true;
}

static std::atomic<uint32> GOpCounter;

TFuture<TIoStatusOr<uint64>> FZenStoreHttpClient::AppendOp(FCbPackage OpEntry)
{
	check(bAllowEdit);

	TRACE_CPUPROFILER_EVENT_SCOPE(ZenStoreHttp_AppendOp);

	return Async(EAsyncExecution::LargeThreadPool, [this, OpEntry]
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Zen_AppendOp_Async);
		FLargeMemoryWriter SerializedPackage;

		const uint32 Salt = ++GOpCounter;
		bool IsUsingTempFiles = false;

		if (TempDirPath.IsEmpty())
		{
			// Old-style with all attachments by value

			OpEntry.Save(SerializedPackage);
		}
		else
		{
			TConstArrayView<FCbAttachment> Attachments = OpEntry.GetAttachments();

			// Prep phase

			TSet<FIoHash> NeedChunks;

			{
				FCbWriter Writer;
				Writer.BeginObject();
				Writer.BeginArray("have");

				for (const FCbAttachment& Attachment : Attachments)
				{
					Writer.AddHash(Attachment.GetHash());
				}

				Writer.EndArray();
				Writer.EndObject();

				FCbFieldIterator Prep = Writer.Save();

				UE::Zen::FScopedRequestPtr Request(RequestPool.Get());

				bool IsOk = false;
				
				const Zen::FRequest::Result Res = Request->PerformBlockingUpload<Zen::FRequest::Post>(OplogPrepNewEntryPath, Prep.AsObjectView());

				if (Res == Zen::FRequest::Success)
				{
					FCbObjectView NeedObject;

					if (Res == Zen::FRequest::Success && Request->GetResponseCode() == 200)
					{
						NeedObject = FCbObjectView(Request->GetResponseBuffer().GetData());

						for (auto& Entry : NeedObject["need"])
						{
							NeedChunks.Add(Entry.AsHash());
						}

						IsOk = true;
					}
				}
			}

			// This uses a slight variation for package attachment serialization
			// by writing larger attachments to a file and referencing it in the
			// core object. Small attachments are serialized inline as normal

			FCbWriter Writer;

			FCbObject PackageObj = OpEntry.GetObject();
			const FIoHash PackageObjHash = PackageObj.GetHash();

			Writer.AddObject(PackageObj);
			Writer.AddObjectAttachment(PackageObjHash);

			// Send phase

			for (const FCbAttachment& Attachment : Attachments)
			{
				bool IsSerialized = false;

				const FIoHash AttachmentHash = Attachment.GetHash();

				if (NeedChunks.Contains(AttachmentHash))
				{
					if (FSharedBuffer AttachView = Attachment.AsBinary())
					{
						if (AttachView.GetSize() >= StandaloneThresholdBytes)
						{
							// Write to temporary file. To avoid race conditions we derive
							// the file name from a salt value and the attachment hash

							FIoHash AttachmentSpec[] { FIoHash::HashBuffer(&Salt, sizeof Salt), AttachmentHash };
							FIoHash AttachmentId = FIoHash::HashBuffer(MakeMemoryView(AttachmentSpec));

							FString TempFilePath = TempDirPath / LexToString(AttachmentId);
							IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

							if (IFileHandle* FileHandle = PlatformFile.OpenWrite(*TempFilePath))
							{
								FileHandle->Write((const uint8*)AttachView.GetData(), AttachView.GetSize());
								delete FileHandle;

								Writer.AddHash(AttachmentHash);

								IsSerialized = true;
								IsUsingTempFiles = true;
							}
							else
							{
								// Take the slow path if we can't open the payload file in the
								// large attachment directory

								UE_LOG(LogZenStore, Warning, TEXT("Could not create file '%s', taking slow path for large attachment"), *TempFilePath);
							}
						}
					}

					if (!IsSerialized)
					{
						Attachment.Save(Writer);
					}
				}
				else
				{
					Writer.AddHash(AttachmentHash);
				}
			}
			Writer.AddNull();

			Writer.Save(SerializedPackage);
		}

		UE_LOG(LogZenStore, Verbose, TEXT("Package size: %" UINT64_FMT), SerializedPackage.TotalSize());

		UE::Zen::FScopedRequestPtr Request(RequestPool.Get());

		TStringBuilder<64> NewOpPostUri;
		NewOpPostUri << OplogNewEntryPath;

		if (IsUsingTempFiles)
		{
			NewOpPostUri << "?salt=" << Salt;
		}

		if (UE::Zen::FRequest::Success == Request->PerformBlockingPost(NewOpPostUri, SerializedPackage.GetView()))
		{
			return TIoStatusOr<uint64>(SerializedPackage.TotalSize());
		}
		else
		{
			return TIoStatusOr<uint64>((FIoStatus)(FIoStatusBuilder(EIoErrorCode::Unknown) << TEXT("Append OpLog failed, NewOpLogPath='") << OplogNewEntryPath << TEXT("'")));
		}
	});
}

TIoStatusOr<uint64> FZenStoreHttpClient::GetChunkSize(const FIoChunkId& Id)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ZenStoreHttp_GetChunkSize);

	check(bAllowRead);

	UE::Zen::FScopedRequestPtr Request(RequestPool.Get());
	TArray64<uint8> GetBuffer;
	TStringBuilder<128> ChunkUri;
	ChunkUri << OplogPath << '/' << Id;
	UE::Zen::FRequest::Result Res = Request->PerformBlockingQuery<Zen::FRequest::Head>(ChunkUri);
	FString ContentLengthStr;
	if (Res == Zen::FRequest::Success && Request->GetResponseCode() == 200 && Request->GetHeader("Content-Length", ContentLengthStr))
	{
		return FCStringWide::Atoi64(*ContentLengthStr);
	}
	return FIoStatus(EIoErrorCode::NotFound);
}

TIoStatusOr<FIoBuffer> FZenStoreHttpClient::ReadChunk(const FIoChunkId& Id, uint64 Offset, uint64 Size)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ZenStoreHttp_ReadChunk);
	TStringBuilder<128> ChunkUri;
	ChunkUri << OplogPath << '/' << Id;
	return ReadOpLogUri(ChunkUri, Offset, Size);
}

TIoStatusOr<FIoBuffer> FZenStoreHttpClient::ReadOpLogAttachment(FStringView Id, uint64 Offset, uint64 Size)
{
	TStringBuilder<128> ChunkUri;
	ChunkUri << OplogPath << '/' << Id;
	return ReadOpLogUri(ChunkUri, Offset, Size);
}

TIoStatusOr<FIoBuffer> FZenStoreHttpClient::ReadOpLogUri(FStringBuilderBase& ChunkUri, uint64 Offset, uint64 Size)
{
	check(bAllowRead);

	UE::Zen::FScopedRequestPtr Request(RequestPool.Get());
	TArray64<uint8> GetBuffer;

	bool bHaveQuery = false;

	auto AppendQueryDelimiter = [&bHaveQuery, &ChunkUri]
	{
		if (bHaveQuery)
		{
			ChunkUri.Append(TEXT("&"_WSV));
		}
		else
		{
			ChunkUri.Append(TEXT("?"_WSV));
			bHaveQuery = true;
		}
	};

	if (Offset)
	{
		AppendQueryDelimiter();
		ChunkUri.Appendf(TEXT("offset=%" UINT64_FMT), Offset);
	}

	if (Size != ~uint64(0))
	{
		AppendQueryDelimiter();
		ChunkUri.Appendf(TEXT("size=%" UINT64_FMT), Size);
	}


	UE::Zen::FRequest::Result Res = Request->PerformBlockingDownload(ChunkUri, &GetBuffer);

	if (Res == Zen::FRequest::Success && Request->GetResponseCode() == 200)
	{
		return FIoBuffer(FIoBuffer::Clone, GetBuffer.GetData(), GetBuffer.Num());
	}
	return FIoStatus(EIoErrorCode::NotFound);
}

TFuture<TIoStatusOr<FCbObject>> FZenStoreHttpClient::GetOplog()
{
	return Async(EAsyncExecution::LargeThreadPool, [this]
	{
		UE::Zen::FScopedRequestPtr Request(RequestPool.Get());
		
		TStringBuilder<128> Uri;
		Uri << OplogPath << "/entries";

		TArray64<uint8> GetBuffer;
		UE::Zen::FRequest::Result Res = Request->PerformBlockingDownload(Uri, &GetBuffer);

		if (Res == Zen::FRequest::Success && Request->GetResponseCode() == 200)
		{
			FCbObjectView Response(GetBuffer.GetData());
			return TIoStatusOr<FCbObject>(FCbObject::Clone(Response));
		}
		else
		{
			return TIoStatusOr<FCbObject>(FIoStatus(EIoErrorCode::NotFound));
		}
	});
}

TFuture<TIoStatusOr<FCbObject>> FZenStoreHttpClient::GetFiles()
{
	return Async(EAsyncExecution::LargeThreadPool, [this]
	{
		UE::Zen::FScopedRequestPtr Request(RequestPool.Get());
		
		TStringBuilder<128> Uri;
		Uri << OplogPath << "/files";

		TArray64<uint8> GetBuffer;
		UE::Zen::FRequest::Result Res = Request->PerformBlockingDownload(Uri, &GetBuffer);

		if (Res == Zen::FRequest::Success && Request->GetResponseCode() == 200)
		{
			FCbObjectView Response(GetBuffer.GetData());
			return TIoStatusOr<FCbObject>(FCbObject::Clone(Response));
		}
		else
		{
			return TIoStatusOr<FCbObject>(FIoStatus(EIoErrorCode::NotFound));
		}
	});
}

void 
FZenStoreHttpClient::StartBuildPass()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ZenStoreHttp_StartBuildPass);

	check(bAllowEdit);
}

TIoStatusOr<uint64>
FZenStoreHttpClient::EndBuildPass(FCbPackage OpEntry)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ZenStoreHttp_EndBuildPass);

	check(bAllowEdit);

	FLargeMemoryWriter SerializedPackage;
	OpEntry.Save(SerializedPackage);

	UE_LOG(LogZenStore, Verbose, TEXT("Package size: %lld"), SerializedPackage.TotalSize());

	UE::Zen::FScopedRequestPtr Request(RequestPool.Get());

	TConstArrayView64<uint8> Payload { SerializedPackage.GetData(), SerializedPackage.TotalSize()};
	
	if (UE::Zen::FRequest::Success == Request->PerformBlockingUpload<Zen::FRequest::Post>(OplogNewEntryPath, Payload))
	{
		return static_cast<uint64>(Payload.Num());
	}
	else
	{
		return (FIoStatus)(FIoStatusBuilder(EIoErrorCode::Unknown) << TEXT("End build pass failed, NewOpLogPath='") << OplogNewEntryPath << TEXT("'"));
	}
}

} // UE

#else // not PLATFORM_WINDOWS, dummy implementation stub for now

namespace UE {
namespace Zen {
	struct FRequestPool
	{
	};
}

FZenStoreHttpClient::FZenStoreHttpClient(const FStringView InHostName, uint16 InPort)
{
}

FZenStoreHttpClient::~FZenStoreHttpClient()
{
}

void FZenStoreHttpClient::Initialize(
	FStringView InProjectId,
	FStringView InOplogId,
	FStringView ServerRoot,
	FStringView EngineRoot,
	FStringView ProjectRoot,
	bool		IsCleanBuild)
{
}

void FZenStoreHttpClient::InitializeReadOnly(FStringView InProjectId, FStringView InOplogId)
{
}

TIoStatusOr<uint64> FZenStoreHttpClient::GetChunkSize(const FIoChunkId& Id)
{
	return 0;
}

TIoStatusOr<FIoBuffer> FZenStoreHttpClient::ReadChunk(const FIoChunkId& Id, uint64 Offset, uint64 Size)
{
	return FIoBuffer();
}

TIoStatusOr<FIoBuffer> FZenStoreHttpClient::ReadOpLogAttachment(FStringView Id, uint64 Offset, uint64 Size)
{
	return FIoBuffer();
}

TIoStatusOr<FIoBuffer> FZenStoreHttpClient::ReadOpLogUri(FStringBuilderBase& ChunkUri, uint64 Offset, uint64 Size)
{
	return FIoBuffer();
}

void FZenStoreHttpClient::StartBuildPass()
{
}

TIoStatusOr<uint64> FZenStoreHttpClient::EndBuildPass(FCbPackage OpEntry)
{
	return FIoStatus(EIoErrorCode::Unknown);
}

TFuture<TIoStatusOr<uint64>> FZenStoreHttpClient::AppendOp(FCbPackage OpEntry)
{
	return TFuture<TIoStatusOr<uint64>>();
}

TFuture<TIoStatusOr<FCbObject>> FZenStoreHttpClient::GetOplog()
{
	return TFuture<TIoStatusOr<FCbObject>>();
}

TFuture<TIoStatusOr<FCbObject>> FZenStoreHttpClient::GetFiles()
{
	return TFuture<TIoStatusOr<FCbObject>>();
}

}

#endif // PLATFORM_WINDOWS

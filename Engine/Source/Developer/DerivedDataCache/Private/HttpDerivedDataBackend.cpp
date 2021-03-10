// Copyright Epic Games, Inc. All Rights Reserved.
#include "HttpDerivedDataBackend.h"

#if WITH_HTTP_DDC_BACKEND

#if PLATFORM_WINDOWS || PLATFORM_HOLOLENS
#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#endif
#include "curl/curl.h"
#if PLATFORM_WINDOWS || PLATFORM_HOLOLENS
#include "Windows/HideWindowsPlatformTypes.h"
#endif
#include "Containers/StaticArray.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopeLock.h"
#include "Misc/SecureHash.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#define UE_HTTPDDC_BACKEND_WAIT_INTERVAL 0.01f
#define UE_HTTPDDC_HTTP_REQUEST_TIMEOUT_SECONDS 30L
#define UE_HTTPDDC_HTTP_REQUEST_TIMOUT_ENABLED 1
#define UE_HTTPDDC_HTTP_DEBUG 0
#define UE_HTTPDDC_REQUEST_POOL_SIZE 16
#define UE_HTTPDDC_MAX_FAILED_LOGIN_ATTEMPTS 16
#define UE_HTTPDDC_MAX_ATTEMPTS 4
#define UE_HTTPDDC_MAX_BUFFER_RESERVE 104857600u

TRACE_DECLARE_INT_COUNTER(HttpDDC_Exist, TEXT("HttpDDC Exist"));
TRACE_DECLARE_INT_COUNTER(HttpDDC_ExistHit, TEXT("HttpDDC Exist Hit"));
TRACE_DECLARE_INT_COUNTER(HttpDDC_Get, TEXT("HttpDDC Get"));
TRACE_DECLARE_INT_COUNTER(HttpDDC_GetHit, TEXT("HttpDDC Get Hit"));
TRACE_DECLARE_INT_COUNTER(HttpDDC_Put, TEXT("HttpDDC Put"));
TRACE_DECLARE_INT_COUNTER(HttpDDC_PutHit, TEXT("HttpDDC Put Hit"));
TRACE_DECLARE_INT_COUNTER(HttpDDC_BytesReceived, TEXT("HttpDDC Bytes Received"));
TRACE_DECLARE_INT_COUNTER(HttpDDC_BytesSent, TEXT("HttpDDC Bytes Sent"));

static CURLcode sslctx_function(CURL * curl, void * sslctx, void * parm);

/**
 * Encapsulation for access token shared by all requests.
 */
struct FHttpAccessToken
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

	FRequest(const TCHAR* InDomain, FHttpAccessToken* InAuthorizationToken, bool bInLogErrors)
		: bLogErrors(bInLogErrors)
		, Domain(InDomain)
		, AuthorizationToken(InAuthorizationToken)
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
		ReadDataView = TArrayView<const uint8>();
		WriteDataBufferPtr = nullptr;
		WriteHeaderBufferPtr = nullptr;
		BytesSent = 0;
		BytesReceived = 0;
		CurlResult = CURL_LAST;

		curl_easy_reset(Curl);

		// Options that are always set for all connections.
#if UE_HTTPDDC_HTTP_REQUEST_TIMOUT_ENABLED
		curl_easy_setopt(Curl, CURLOPT_CONNECTTIMEOUT, UE_HTTPDDC_HTTP_REQUEST_TIMEOUT_SECONDS);
#endif
		curl_easy_setopt(Curl, CURLOPT_FOLLOWLOCATION, 1L);
		curl_easy_setopt(Curl, CURLOPT_NOSIGNAL, 1L);
		// SSL options
		curl_easy_setopt(Curl, CURLOPT_USE_SSL, CURLUSESSL_ALL);
		curl_easy_setopt(Curl, CURLOPT_SSL_VERIFYPEER, 1);
		curl_easy_setopt(Curl, CURLOPT_SSL_VERIFYHOST, 1);
		curl_easy_setopt(Curl, CURLOPT_SSLCERTTYPE, "PEM");
		// Response functions
		curl_easy_setopt(Curl, CURLOPT_HEADERDATA, this);
		curl_easy_setopt(Curl, CURLOPT_HEADERFUNCTION, &FRequest::StaticWriteHeaderFn);
		curl_easy_setopt(Curl, CURLOPT_WRITEDATA, this);
		curl_easy_setopt(Curl, CURLOPT_WRITEFUNCTION, StaticWriteBodyFn);
		// SSL certification verification
		curl_easy_setopt(Curl, CURLOPT_CAINFO, nullptr);
		curl_easy_setopt(Curl, CURLOPT_SSL_CTX_FUNCTION, *sslctx_function);
		curl_easy_setopt(Curl, CURLOPT_SSL_CTX_DATA, this);
		// Allow compressed data
		curl_easy_setopt(Curl, CURLOPT_ACCEPT_ENCODING, "gzip");
		// Rewind method, handle special error case where request need to rewind data stream
		curl_easy_setopt(Curl, CURLOPT_SEEKFUNCTION, StaticSeekFn);
		curl_easy_setopt(Curl, CURLOPT_SEEKDATA, this);
		// Debug hooks
#if UE_HTTPDDC_HTTP_DEBUG
		curl_easy_setopt(Curl, CURLOPT_DEBUGDATA, this);
		curl_easy_setopt(Curl, CURLOPT_DEBUGFUNCTION, StaticDebugCallback);
		curl_easy_setopt(Curl, CURLOPT_VERBOSE, 1L);
#endif
	}

	/** Gets the domain name for this request */
	const FString& GetDomain() const
	{
		return Domain;
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

	/**
	 * Upload buffer using the request, using either "Put" or "Post" verbs.
	 * @param Uri Url to use.
	 * @param Buffer Data to upload
	 * @return Result of the request
	 */
	template<RequestVerb V>
	Result PerformBlockingUpload(const TCHAR* Uri, TArrayView<const uint8> Buffer)
	{
		static_assert(V == Put || V == Post || V == PostJson, "Upload should use either Put or Post verbs.");
		
		uint32 ContentLength = 0u;

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
		else if (V == Post || V == PostJson)
		{
			curl_easy_setopt(Curl, CURLOPT_POST, 1L);
			curl_easy_setopt(Curl, CURLOPT_INFILESIZE, Buffer.Num());
			curl_easy_setopt(Curl, CURLOPT_READDATA, this);
			curl_easy_setopt(Curl, CURLOPT_READFUNCTION, StaticReadFn);
			Headers.Add(V == Post ? FString(TEXT("Content-Type: application/x-www-form-urlencoded")) : FString(TEXT("Content-Type: application/json")));
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
	Result PerformBlockingDownload(const TCHAR* Uri, TArray<uint8>* Buffer)
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
	Result PerformBlockingQuery(const TCHAR* Uri)
	{
		static_assert(V == Head || V == Delete, "Queries should use either Head or Delete verbs.");

		if (V == Delete)
		{
			curl_easy_setopt(Curl, CURLOPT_POST, 1L);
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

		const ANSICHAR* HeadersBuffer = (const ANSICHAR*) ResponseHeader.GetData();
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
	const TArray<uint8>& GetResponseBuffer() const
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

	/**
	 * Tries to parse the response buffer as a JsonObject. Return empty pointer if 
	 * parse error occurs.
	 */
	TSharedPtr<FJsonObject> GetResponseAsJsonObject() const
	{
		FString Response = GetAnsiBufferAsString(ResponseBuffer);

		TSharedPtr<FJsonObject> JsonObject;
		TSharedRef<TJsonReader<> > JsonReader = TJsonReaderFactory<>::Create(Response);
		if (!FJsonSerializer::Deserialize(JsonReader, JsonObject) || !JsonObject.IsValid())
		{
			return TSharedPtr<FJsonObject>(nullptr);
		}

		return JsonObject;
	}

private:

	CURL*					Curl;
	CURLcode				CurlResult;
	long					ResponseCode;
	size_t					BytesSent;
	size_t					BytesReceived;
	bool					bLogErrors;

	TArrayView<const uint8>	ReadDataView;
	TArray<uint8>*			WriteDataBufferPtr;
	TArray<uint8>*			WriteHeaderBufferPtr;
	
	TArray<uint8>			ResponseHeader;
	TArray<uint8>			ResponseBuffer;
	TArray<FString>			Headers;
	FString					Domain;
	FHttpAccessToken*		AuthorizationToken;

	/**
	 * Performs the request, blocking until finished.
	 * @param Uri Address on the domain to query
	 * @param Verb HTTP verb to use
	 * @param Buffer Optional buffer to directly receive the result of the request.
	 * If unset the response body will be stored in the request.
	 */
	Result PerformBlocking(const TCHAR* Uri, RequestVerb Verb, uint32 ContentLength)
	{
		static const char* CommonHeaders[] = {
			"User-Agent: UE4",
			nullptr
		};

		TRACE_CPUPROFILER_EVENT_SCOPE(HttpDDC_CurlPerform);

		// Setup request options
		FString Url = FString::Printf(TEXT("%s/%s"), *Domain, Uri);
		curl_easy_setopt(Curl, CURLOPT_URL, TCHAR_TO_ANSI(*Url));

		// Setup response header buffer. If caller has not setup a response data buffer, use interal.
		WriteHeaderBufferPtr = &ResponseHeader;
		if (WriteDataBufferPtr == nullptr)
		{
			WriteDataBufferPtr = &ResponseBuffer;
		}

		// Content-Length should always be set
		Headers.Add(FString::Printf(TEXT("Content-Length: %d"), ContentLength));

		// And auth token if it's set
		if (AuthorizationToken)
		{
			Headers.Add(AuthorizationToken->GetHeader());
		}

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

	void LogResult(CURLcode Result, const TCHAR* Uri, RequestVerb Verb) const
	{
		if (Result == CURLE_OK)
		{
			bool bSuccess = false;
			const TCHAR* VerbStr = nullptr;
			FString AdditionalInfo;

			switch (Verb)
			{
			case Head:
				bSuccess = (ResponseCode == 400 || ResponseCode == 200);
				VerbStr = TEXT("querying");
				break;
			case Get:
				bSuccess = (ResponseCode == 400 || ResponseCode == 200);
				VerbStr = TEXT("fetching");
				AdditionalInfo = FString::Printf(TEXT("Received: %d bytes."), BytesReceived);
				break;
			case Put:
				bSuccess = ResponseCode == 200;
				VerbStr = TEXT("updating");
				AdditionalInfo = FString::Printf(TEXT("Sent: %d bytes."), BytesSent);
				break;
			case Post:
			case PostJson:
				bSuccess = ResponseCode == 200;
				VerbStr = TEXT("posting");
				break;
			case Delete:
				bSuccess = ResponseCode == 200;
				VerbStr = TEXT("deleting");
				break;
			}

			if (bSuccess)
			{
				UE_LOG(
					LogDerivedDataCache, 
					Verbose, 
					TEXT("Finished %s HTTP cache entry (response %d) from %s. %s"), 
					VerbStr, 
					ResponseCode, 
					Uri,
					*AdditionalInfo
				);
			}
			else if(bLogErrors)
			{
				// Print the response body if we got one, otherwise print header.
				FString Response = GetAnsiBufferAsString(ResponseBuffer.Num() > 0 ? ResponseBuffer : ResponseHeader);
				Response.ReplaceCharInline('\n', ' ');
				Response.ReplaceCharInline('\r', ' ');
				UE_LOG(
					LogDerivedDataCache, 
					Error, 
					TEXT("Failed %s HTTP cache entry (response %d) from %s. Response: %s"),
					VerbStr, 
					ResponseCode, 
					Uri,
					*Response
				);
			}
		}
		else if(bLogErrors)
		{
			UE_LOG(
				LogDerivedDataCache, 
				Error, 
				TEXT("Error while connecting to %s: %s"), 
				*Domain, 
				ANSI_TO_TCHAR(curl_easy_strerror(Result))
			);
		}
	}

	FString GetAnsiBufferAsString(const TArray<uint8>& Buffer) const
	{
		// Content is NOT null-terminated; we need to specify lengths here
		FUTF8ToTCHAR TCHARData(reinterpret_cast<const ANSICHAR*>(Buffer.GetData()), Buffer.Num());
		return FString(TCHARData.Length(), TCHARData.Get());
	}

	static size_t StaticDebugCallback(CURL * Handle, curl_infotype DebugInfoType, char * DebugInfo, size_t DebugInfoSize, void* UserData)
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
			UE_LOG(LogDerivedDataCache, VeryVerbose, TEXT("%p: '%s'"), Request, *DebugText);
		}
		break;

		case CURLINFO_HEADER_IN:
			UE_LOG(LogDerivedDataCache, VeryVerbose, TEXT("%p: Received header (%d bytes)"), Request, DebugInfoSize);
			break;

		case CURLINFO_DATA_IN:
			UE_LOG(LogDerivedDataCache, VeryVerbose, TEXT("%p: Received data (%d bytes)"), Request, DebugInfoSize);
			break;

		case CURLINFO_DATA_OUT:
			UE_LOG(LogDerivedDataCache, VeryVerbose, TEXT("%p: Sent data (%d bytes)"), Request, DebugInfoSize);
			break;

		case CURLINFO_SSL_DATA_IN:
			UE_LOG(LogDerivedDataCache, VeryVerbose, TEXT("%p: Received SSL data (%d bytes)"), Request, DebugInfoSize);
			break;

		case CURLINFO_SSL_DATA_OUT:
			UE_LOG(LogDerivedDataCache, VeryVerbose, TEXT("%p: Sent SSL data (%d bytes)"), Request, DebugInfoSize);
			break;
		}

		return 0;
	}

	static size_t StaticReadFn(void* Ptr, size_t SizeInBlocks, size_t BlockSizeInBytes, void* UserData)
	{
		FRequest* Request = static_cast<FRequest*>(UserData);
		TArrayView<const uint8>& ReadDataView = Request->ReadDataView;

		const size_t Offset = Request->BytesSent;
		const size_t ReadSize = FMath::Min((size_t)ReadDataView.Num() - Offset, SizeInBlocks * BlockSizeInBytes);
		check(ReadDataView.Num() >= Offset + ReadSize);

		FMemory::Memcpy(Ptr, ReadDataView.GetData() + Offset, ReadSize);
		Request->BytesSent += ReadSize;
		return ReadSize;
		
		return 0;
	}

	static size_t StaticWriteHeaderFn(void* Ptr, size_t SizeInBlocks, size_t BlockSizeInBytes, void* UserData)
	{
		FRequest* Request = static_cast<FRequest*>(UserData);
		const size_t WriteSize = SizeInBlocks * BlockSizeInBytes;
		TArray<uint8>* WriteHeaderBufferPtr = Request->WriteHeaderBufferPtr;
		if (WriteHeaderBufferPtr && WriteSize > 0)
		{
			const size_t CurrentBufferLength = WriteHeaderBufferPtr->Num();
			if (CurrentBufferLength > 0)
			{
				// Remove the previous zero termination
				(*WriteHeaderBufferPtr)[CurrentBufferLength-1] = ' ';
			}

			// Write the header
			WriteHeaderBufferPtr->Append((const uint8*)Ptr, WriteSize + 1);
			(*WriteHeaderBufferPtr)[WriteHeaderBufferPtr->Num()-1] = 0; // Zero terminate string
			return WriteSize;
		}
		return 0;
	}

	static size_t StaticWriteBodyFn(void* Ptr, size_t SizeInBlocks, size_t BlockSizeInBytes, void* UserData)
	{
		FRequest* Request = static_cast<FRequest*>(UserData);
		const size_t WriteSize = SizeInBlocks * BlockSizeInBytes;
		TArray<uint8>* WriteDataBufferPtr = Request->WriteDataBufferPtr;

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
					if (ContentLength > 0u && ContentLength < UE_HTTPDDC_MAX_BUFFER_RESERVE)
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
		if (NewPosition < 0 || NewPosition >= Request->ReadDataView.Num())
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
	FRequestPool(const TCHAR* InServiceUrl, FHttpAccessToken* InAuthorizationToken)
	{
		for (uint8 i = 0; i < Pool.Num(); ++i)
		{
			Pool[i].Usage = 0u;
			Pool[i].Request = new FRequest(InServiceUrl, InAuthorizationToken, true);
		}
	}

	~FRequestPool()
	{
		for (uint8 i = 0; i < Pool.Num(); ++i)
		{
			// No requests should be in use by now.
			check(Pool[i].Usage.Load(EMemoryOrder::Relaxed) == 0u);
			delete Pool[i].Request;
		}
	}

	/**
	 * Block until a request is free. Once a request has been returned it is 
	 * "owned by the caller and need to release it to the pool when work has been completed.
	 * @return Usable request instance.
	 */
	FRequest* WaitForFreeRequest()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(HttpDDC_WaitForConnPool);
		while (true)
		{
			for (uint8 i = 0; i < Pool.Num(); ++i)
			{
				if (!Pool[i].Usage.Load(EMemoryOrder::Relaxed))
				{
					uint8 Expected = 0u;
					if (Pool[i].Usage.CompareExchange(Expected, 1u))
					{
						return Pool[i].Request;
					}
				}
			}
			FPlatformProcess::Sleep(UE_HTTPDDC_BACKEND_WAIT_INTERVAL);
		}
	}

	/**
	 * Release request to the pool.
	 * @param Request Request that should be freed. Note that any buffer owened by the request can now be reset.
	 */
	void ReleaseRequestToPool(FRequest* Request)
	{
		for (uint8 i = 0; i < Pool.Num(); ++i)
		{
			if (Pool[i].Request == Request)
			{
				Request->Reset();
				uint8 Expected = 1u;
				Pool[i].Usage.CompareExchange(Expected, 0u);
				return;
			}
		}
		check(false);
	}

private:

	struct FEntry
	{
		TAtomic<uint8> Usage;
		FRequest* Request;
	};
	
	TStaticArray<FEntry, UE_HTTPDDC_REQUEST_POOL_SIZE> Pool;

	FRequestPool() {}
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



//----------------------------------------------------------------------------------------------------------
// Certificate checking
//----------------------------------------------------------------------------------------------------------

#if WITH_SSL
#include "Ssl.h"

#include <openssl/ssl.h>

static int SslCertVerify(int PreverifyOk, X509_STORE_CTX* Context)
{
	if (PreverifyOk == 1)
	{
		SSL* Handle = static_cast<SSL*>(X509_STORE_CTX_get_ex_data(Context, SSL_get_ex_data_X509_STORE_CTX_idx()));
		check(Handle);

		SSL_CTX* SslContext = SSL_get_SSL_CTX(Handle);
		check(SslContext);

		FRequest* Request = static_cast<FRequest*>(SSL_CTX_get_app_data(SslContext));
		check(Request);

		const FString& Domain = Request->GetDomain();

		if (!FSslModule::Get().GetCertificateManager().VerifySslCertificates(Context, Domain))
		{
			PreverifyOk = 0;
		}
	}

	return PreverifyOk;
}

static CURLcode sslctx_function(CURL * curl, void * sslctx, void * parm)
{
	SSL_CTX* Context = static_cast<SSL_CTX*>(sslctx);
	const ISslCertificateManager& CertificateManager = FSslModule::Get().GetCertificateManager();

	CertificateManager.AddCertificatesToSslContext(Context);
	SSL_CTX_set_verify(Context, SSL_CTX_get_verify_mode(Context), SslCertVerify);
	SSL_CTX_set_app_data(Context, parm);

	/* all set to go */
	return CURLE_OK;
}

#endif //#if WITH_SSL

//----------------------------------------------------------------------------------------------------------
// Content parsing and checking
//----------------------------------------------------------------------------------------------------------


/**
 * Verifies the integrity of the recieved data using supplied checksum.
 * @param Hash Recieved hash value.
 * @param Payload Payload recieved.
 * @return True if the data is correct, false if checksums doesn't match.
 */
bool VerifyPayload(FSHAHash Hash, const TArray<uint8>& Payload)
{
	FSHAHash PayloadHash;
	FSHA1::HashBuffer(Payload.GetData(), Payload.Num(), PayloadHash.Hash);

	if (Hash != PayloadHash)
	{
		UE_LOG(LogDerivedDataCache,
			Warning,
			TEXT("Checksum from server did not match recieved data (%s vs %s). Discarding cached result."),
			*Hash.ToString(),
			*PayloadHash.ToString()
		);
		return false;
	}

	return true;
}


/**
 * Verifies the integrity of the recieved data using supplied checksum.
 * @param Request Request that the data was be recieved with.
 * @param Payload Payload recieved.
 * @return True if the data is correct, false if checksums doesn't match.
 */
bool VerifyRequest(const FRequest* Request, const TArray<uint8>& Payload)
{
	FString RecievedHashStr;
	FSHAHash PayloadHash;
	FSHA1::HashBuffer(Payload.GetData(), Payload.Num(), PayloadHash.Hash);
	if (Request->GetHeader("X-Jupiter-Sha1", RecievedHashStr))
	{
		FSHAHash RecievedHash;
		RecievedHash.FromString(RecievedHashStr);
		return VerifyPayload(RecievedHash, Payload);
	}
	UE_LOG(LogDerivedDataCache, Warning, TEXT("HTTP server did not send a content hash. Wrong server version?"));
	return true;
}

/**
 * Adds a checksum (as request header) for a given payload. Jupiter will use this to verify the integrity
 * of the recieved data.
 * @param Request Request that the data will be sent with.
 * @param Payload Payload that will be sent.
 * @return True on success, false on failure.
 */
bool HashPayload(FRequest* Request, const TArrayView<const uint8> Payload)
{
	FSHAHash PayloadHash;
	FSHA1::HashBuffer(Payload.GetData(), Payload.Num(), PayloadHash.Hash);
	Request->SetHeader(TEXT("X-Jupiter-Sha1"), *PayloadHash.ToString());
	return true;
}

//----------------------------------------------------------------------------------------------------------
// FHttpAccessToken
//----------------------------------------------------------------------------------------------------------

FString FHttpAccessToken::GetHeader()
{
	Lock.ReadLock();
	FString Header = FString::Printf(TEXT("Authorization: Bearer %s"), *Token);
	Lock.ReadUnlock();
	return Header;
}

void FHttpAccessToken::SetHeader(const TCHAR* InToken)
{
	Lock.WriteLock();
	Token = InToken;
	Serial++;
	Lock.WriteUnlock();
}

uint32 FHttpAccessToken::GetSerial() const
{
	return Serial;
}

//----------------------------------------------------------------------------------------------------------
// FHttpDerivedDataBackend
//----------------------------------------------------------------------------------------------------------

FHttpDerivedDataBackend::FHttpDerivedDataBackend(
	const TCHAR* InServiceUrl, 
	const TCHAR* InNamespace, 
	const TCHAR* InOAuthProvider,
	const TCHAR* InOAuthClientId,
	const TCHAR* InOAuthSecret)
	: Domain(InServiceUrl)
	, Namespace(InNamespace)
	, DefaultBucket(TEXT("default"))
	, OAuthProvider(InOAuthProvider)
	, OAuthClientId(InOAuthClientId)
	, OAuthSecret(InOAuthSecret)
	, Access(nullptr)
	, bIsUsable(false)
	, FailedLoginAttempts(0)
{
	if (IsServiceReady() && AcquireAccessToken())
	{
		RequestPool = MakeUnique<FRequestPool>(InServiceUrl, Access.Get());
		bIsUsable = true;
	}
}

FHttpDerivedDataBackend::~FHttpDerivedDataBackend()
{
}

FString FHttpDerivedDataBackend::GetName() const
{
	return Domain;
}

bool FHttpDerivedDataBackend::TryToPrefetch(const TCHAR* CacheKey)
{
	return false;
}

bool FHttpDerivedDataBackend::WouldCache(const TCHAR* CacheKey, TArrayView<const uint8> InData)
{
	return true;
}

FHttpDerivedDataBackend::ESpeedClass FHttpDerivedDataBackend::GetSpeedClass() const
{
	return ESpeedClass::Slow;
}

bool FHttpDerivedDataBackend::ApplyDebugOptions(FBackendDebugOptions& InOptions)
{
	return false;
}


bool FHttpDerivedDataBackend::IsServiceReady()
{
	FRequest Request(*Domain, nullptr, false);
	FRequest::Result Result = Request.PerformBlockingDownload(TEXT("health/ready"), nullptr);
	
	if (Result == FRequest::Success && Request.GetResponseCode() == 200)
	{
		UE_LOG(LogDerivedDataCache, Display, TEXT("HTTP DDC service status: %s."), *Request.GetResponseAsString());
		return true;
	}
	else
	{
		UE_LOG(LogDerivedDataCache, Warning, TEXT("Unable to reach HTTP DDC service at %s. Status: %d . Response: %s"), *Domain, Request.GetResponseCode(), *Request.GetResponseAsString());
	}

	return false;
}

bool FHttpDerivedDataBackend::AcquireAccessToken()
{
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

		FRequest Request(*AuthDomain, nullptr, false);

		// If contents of the secret string is a file path, resolve and read form data.
		if (OAuthSecret.StartsWith(TEXT("\\\\")))
		{
			FString SecretFileContents;
			if (FFileHelper::LoadFileToString(SecretFileContents, *OAuthSecret))
			{
				// Overwrite the filepath with the actual content.
				OAuthSecret = SecretFileContents;
			}
			else
			{
				UE_LOG(LogDerivedDataCache, Warning, TEXT("Failed to read OAuth form data file (%s)."), *OAuthSecret);
				return false;
			}
		}

		FString OAuthFormData = FString::Printf(
			TEXT("client_id=%s&scope=cache_access&grant_type=client_credentials&client_secret=%s"),
			*OAuthClientId,
			*OAuthSecret
		);

		TArray<uint8> FormData;
		auto OAuthFormDataUTF8 = FTCHARToUTF8(*OAuthFormData);
		FormData.Append((uint8*)OAuthFormDataUTF8.Get(), OAuthFormDataUTF8.Length());

		FRequest::Result Result = Request.PerformBlockingUpload<FRequest::Post>(*Uri, MakeArrayView(FormData));

		if (Result == FRequest::Success && Request.GetResponseCode() == 200)
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
					Access->SetHeader(*AccessTokenString);
					UE_LOG(LogDerivedDataCache, Display, TEXT("Logged in to HTTP DDC services. Expires in %d seconds."), ExpiryTimeSeconds);

					//Schedule a refresh of the token ahead of expiry time (this will not work in commandlets)
					if (!IsRunningCommandlet())
					{
						FTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
							[this](float DeltaTime)
							{
								this->AcquireAccessToken();
								return false;
							}
						), ExpiryTimeSeconds - 20.0f);
					}

					return true;
				}
			}
		}
		else
		{
			UE_LOG(LogDerivedDataCache, Warning, TEXT("Failed to log in to HTTP services. Server responed with code %d."), Request.GetResponseCode());
			FailedLoginAttempts++;
		}
	}
	return false;
}

bool FHttpDerivedDataBackend::ShouldRetryOnError(int64 ResponseCode)
{
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

bool FHttpDerivedDataBackend::CachedDataProbablyExists(const TCHAR* CacheKey)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(HttpDDC_Exist);
	TRACE_COUNTER_ADD(HttpDDC_Exist, int64(1));
	COOK_STAT(auto Timer = UsageStats.TimeProbablyExists());
	
	FString Uri = FString::Printf(TEXT("api/v1/c/ddc/%s/%s/%s"), *Namespace, *DefaultBucket, CacheKey);
	long ResponseCode = 0; uint32 Attempts = 0;

	// Retry request until we get an accepted response or exhaust allowed number of attempts.
	while (ResponseCode == 0 && ++Attempts < UE_HTTPDDC_MAX_ATTEMPTS)
	{
		FScopedRequestPtr Request(RequestPool.Get());
		FRequest::Result Result = Request->PerformBlockingQuery<FRequest::Head>(*Uri);
		ResponseCode = Request->GetResponseCode();

		if (ResponseCode == 200 || ResponseCode == 400)
		{
			const bool bIsHit = (Result == FRequest::Success && ResponseCode == 200);
			if (bIsHit)
			{
				TRACE_COUNTER_ADD(HttpDDC_ExistHit, int64(1));
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

bool FHttpDerivedDataBackend::GetCachedData(const TCHAR* CacheKey, TArray<uint8>& OutData)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(HttpDDC_Get);
	TRACE_COUNTER_ADD(HttpDDC_Get, int64(1));
	COOK_STAT(auto Timer = UsageStats.TimeGet());

	FString Uri = FString::Printf(TEXT("api/v1/c/ddc/%s/%s/%s.raw"), *Namespace, *DefaultBucket, CacheKey);
	int64 ResponseCode = 0; uint32 Attempts = 0;

	// Retry request until we get an accepted response or exhaust allowed number of attempts.
	while (ResponseCode == 0 && ++Attempts < UE_HTTPDDC_MAX_ATTEMPTS)
	{
		FScopedRequestPtr Request(RequestPool.Get());
		if (Request.IsValid())
		{
			FRequest::Result Result = Request->PerformBlockingDownload(*Uri, &OutData);
			ResponseCode = Request->GetResponseCode();

			// Request was successful, make sure we got all the expected data.
			if (ResponseCode == 200 && VerifyRequest(Request.Get(), OutData))
			{
				TRACE_COUNTER_ADD(HttpDDC_GetHit, int64(1));
				TRACE_COUNTER_ADD(HttpDDC_BytesReceived, int64(Request->GetBytesReceived()));
				COOK_STAT(Timer.AddHit(Request->GetBytesReceived()));
				return true;
			}

			if (!ShouldRetryOnError(ResponseCode))
			{
				return false;
			}

			ResponseCode = 0;
		}
	}

	return false;
}

void FHttpDerivedDataBackend::PutCachedData(const TCHAR* CacheKey, TArrayView<const uint8> InData, bool bPutEvenIfExists)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(HttpDDC_Put);
	COOK_STAT(auto Timer = UsageStats.TimePut());

	FString Uri = FString::Printf(TEXT("api/v1/c/ddc/%s/%s/%s"), *Namespace, *DefaultBucket, CacheKey);
	int64 ResponseCode = 0; uint32 Attempts = 0;

	// Retry request until we get an accepted response or exhaust allowed number of attempts.
	while (ResponseCode == 0 && ++Attempts < UE_HTTPDDC_MAX_ATTEMPTS)
	{
		FScopedRequestPtr Request(RequestPool.Get());
		if (Request.IsValid())
		{
			// Append the content hash to the header
			HashPayload(Request.Get(), InData);

			FRequest::Result Result = Request->PerformBlockingUpload<FRequest::Put>(*Uri, InData);
			ResponseCode = Request->GetResponseCode();

			if (ResponseCode == 200)
			{
				TRACE_COUNTER_ADD(HttpDDC_BytesSent, int64(Request->GetBytesSent()));
				COOK_STAT(Timer.AddHit(Request->GetBytesSent()));
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

void FHttpDerivedDataBackend::RemoveCachedData(const TCHAR* CacheKey, bool bTransient)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(HttpDDC_Remove);
	FString Uri = FString::Printf(TEXT("api/v1/c/ddc/%s/%s/%s"), *Namespace, *DefaultBucket, CacheKey);
	int64 ResponseCode = 0; uint32 Attempts = 0;

	// Retry request until we get an accepted response or exhaust allowed number of attempts.
	while (ResponseCode == 0 && ++Attempts < UE_HTTPDDC_MAX_ATTEMPTS)
	{
		FScopedRequestPtr Request(RequestPool.Get());
		if (Request.IsValid())
		{
			FRequest::Result Result = Request->PerformBlockingQuery<FRequest::Delete>(*Uri);
			ResponseCode = Request->GetResponseCode();

			if (ResponseCode == 200)
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

TSharedRef<FDerivedDataCacheStatsNode> FHttpDerivedDataBackend::GatherUsageStats() const
{
	TSharedRef<FDerivedDataCacheStatsNode> Usage = MakeShared<FDerivedDataCacheStatsNode>(this, FString::Printf(TEXT("%s @ %s (%s)"), TEXT("HTTP"), *Domain, *Namespace));
	Usage->Stats.Add(TEXT(""), UsageStats);

	return Usage;
}

#endif //WITH_HTTP_DDC_BACKEND

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
#include "Algo/Transform.h"
#include "Algo/Find.h"
#include "Containers/StaticArray.h"
#include "Containers/Ticker.h"
#include "DerivedDataCacheRecord.h"
#include "Dom/JsonObject.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/PlatformFileManager.h"
#include "IO/IoHash.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopeLock.h"
#include "Misc/SecureHash.h"
#include "Misc/StringBuilder.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Policies/CondensedJsonPrintPolicy.h"

#if WITH_SSL
#include "Ssl.h"
#include <openssl/ssl.h>
#endif

// Enables data request helpers that internally
// batch requests to reduce the number of concurrent
// connections.
#ifndef WITH_DATAREQUEST_HELPER
	#define WITH_DATAREQUEST_HELPER 1
#endif

#define UE_HTTPDDC_BACKEND_WAIT_INTERVAL 0.01f
#define UE_HTTPDDC_HTTP_REQUEST_TIMEOUT_SECONDS 30L
#define UE_HTTPDDC_HTTP_REQUEST_TIMOUT_ENABLED 1
#define UE_HTTPDDC_HTTP_DEBUG 0
#if WITH_DATAREQUEST_HELPER
	#define UE_HTTPDDC_GET_REQUEST_POOL_SIZE 16 
	#define UE_HTTPDDC_PUT_REQUEST_POOL_SIZE 2
#else
	#define UE_HTTPDDC_GET_REQUEST_POOL_SIZE 48
	#define UE_HTTPDDC_PUT_REQUEST_POOL_SIZE 16
#endif
#define UE_HTTPDDC_MAX_FAILED_LOGIN_ATTEMPTS 16
#define UE_HTTPDDC_MAX_ATTEMPTS 4
#define UE_HTTPDDC_MAX_BUFFER_RESERVE 104857600u
#define UE_HTTPDDC_BATCH_SIZE 12
#define UE_HTTPDDC_BATCH_NUM 64
#define UE_HTTPDDC_BATCH_GET_WEIGHT 4
#define UE_HTTPDDC_BATCH_HEAD_WEIGHT 1
#define UE_HTTPDDC_BATCH_WEIGHT_HINT 12

namespace UE::DerivedData::Backends
{

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

	FHttpRequest(const TCHAR* InDomain, FHttpAccessToken* InAuthorizationToken, bool bInLogErrors)
		: bLogErrors(bInLogErrors)
		, Domain(InDomain)
		, AuthorizationToken(InAuthorizationToken)
	{
		Curl = curl_easy_init();
		Reset();
	}

	~FHttpRequest()
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
		curl_easy_setopt(Curl, CURLOPT_HEADERFUNCTION, &FHttpRequest::StaticWriteHeaderFn);
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
		// Set minimum speed behavior to allow operations to abort if the transfer speed is poor for the given duration (1kbps over a 30 second span)
		curl_easy_setopt(Curl, CURLOPT_LOW_SPEED_TIME, 30L);
		curl_easy_setopt(Curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
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

	/**
	 * Tries to parse the response buffer as a JsonArray. Return empty array if
	 * parse error occurs.
	 */
	TArray<TSharedPtr<FJsonValue>> GetResponseAsJsonArray() const
	{
		FString Response = GetAnsiBufferAsString(ResponseBuffer);

		TArray<TSharedPtr<FJsonValue>> JsonArray;
		TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(Response);
		FJsonSerializer::Deserialize(JsonReader, JsonArray);
		return JsonArray;
	}

	/** Will return true if the response code is considered a success */
	static bool IsSuccessResponse(long ResponseCode)
	{
		// We consider anything in the 1XX or 2XX range a success
		return ResponseCode >= 100 && ResponseCode < 300;
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
			"User-Agent: Unreal Engine",
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
			case PostJson:
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
				// Dont log access denied as error, since tokens can expire mid session
				if (ResponseCode == 401)
				{
					UE_LOG(
						LogDerivedDataCache,
						Verbose,
						TEXT("Failed %s HTTP cache entry (response %d) from %s. Response: %s"),
						VerbStr,
						ResponseCode,
						Uri,
						*Response
					);
				}
				else
				{ 
					UE_LOG(
						LogDerivedDataCache,
						Display,
						TEXT("Failed %s HTTP cache entry (response %d) from %s. Response: %s"),
						VerbStr,
						ResponseCode,
						Uri,
						*Response
					);
				}


			}
		}
		else if(bLogErrors)
		{
			UE_LOG(
				LogDerivedDataCache, 
				Display, 
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
		FHttpRequest* Request = static_cast<FHttpRequest*>(UserData);

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
		FHttpRequest* Request = static_cast<FHttpRequest*>(UserData);
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
		FHttpRequest* Request = static_cast<FHttpRequest*>(UserData);
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
		FHttpRequest* Request = static_cast<FHttpRequest*>(UserData);
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
		FHttpRequest* Request = static_cast<FHttpRequest*>(UserData);
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
// Forward declarations
//----------------------------------------------------------------------------------------------------------
bool VerifyPayload(const FSHAHash& Hash, const TArray<uint8>& Payload);
bool VerifyPayload(const FIoHash& Hash, const TArray<uint8>& Payload);
bool VerifyRequest(const class FHttpRequest* Request, const TArray<uint8>& Payload);
bool HashPayload(class FHttpRequest* Request, const TArrayView<const uint8> Payload);
bool ShouldAbortForShutdown();

//----------------------------------------------------------------------------------------------------------
// Request pool
//----------------------------------------------------------------------------------------------------------


/**
 * Pool that manages a fixed set of requests. Users are required to release requests that have been 
 * acquired. Usable with \ref FScopedRequestPtr which handles this automatically.
 */
struct FRequestPool
{
	FRequestPool(const TCHAR* InServiceUrl, FHttpAccessToken* InAuthorizationToken, uint32 PoolSize)
	{
		Pool.AddUninitialized(PoolSize);
		for (uint8 i = 0; i < Pool.Num(); ++i)
		{
			Pool[i].Usage = 0u;
			Pool[i].Request = new FHttpRequest(InServiceUrl, InAuthorizationToken, true);
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
	FHttpRequest* GetFreeRequest()
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
	FHttpRequest* WaitForFreeRequest()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(HttpDDC_WaitForConnPool);
		FHttpRequest* Request = nullptr;
		while (true)
		{
			Request = GetFreeRequest();
			if (Request != nullptr)
				break;
			FPlatformProcess::Sleep(UE_HTTPDDC_BACKEND_WAIT_INTERVAL);
		}
		return Request;
	}

	/**
	 * Release request to the pool.
	 * @param Request Request that should be freed. Note that any buffer owened by the request can now be reset.
	 */
	void ReleaseRequestToPool(FHttpRequest* Request)
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
	void MakeRequestShared(FHttpRequest* Request, uint8 Users)
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
		FHttpRequest* Request;
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
	FRequestPool* Pool;
};


#if WITH_DATAREQUEST_HELPER

//----------------------------------------------------------------------------------------------------------
// FDataRequestHelper
//----------------------------------------------------------------------------------------------------------
/**
 * Helper class for requesting data. Will batch requests once the number of concurrent requests reach a threshold.
 */
struct FDataRequestHelper
{
	FDataRequestHelper(FRequestPool* InPool, const TCHAR* InNamespace, const TCHAR* InBucket, const TCHAR* InCacheKey, TArray<uint8>* OutData)
		: Request(nullptr)
		, Pool(InPool)
		, bVerified(false, 1)
	{
		Request = Pool->GetFreeRequest();
		if (Request && OutData != nullptr)
		{
			// We are below the threshold, make the connection immediately. OutData is set so this is a get.
			FString Uri = FString::Printf(TEXT("api/v1/c/ddc/%s/%s/%s.raw"), InNamespace, InBucket, InCacheKey);
			const FHttpRequest::Result Result = Request->PerformBlockingDownload(*Uri, OutData);
			if (FHttpRequest::IsSuccessResponse(Request->GetResponseCode()))
			{
				if (VerifyRequest(Request, *OutData))
				{
					TRACE_COUNTER_ADD(HttpDDC_GetHit, int64(1));
					TRACE_COUNTER_ADD(HttpDDC_BytesReceived, int64(Request->GetBytesReceived()));
					bVerified[0] = true;
				}
			}
		}
		else if (Request)
		{
			// We are below the threshold, make the connection immediately. OutData is missing so this is a head.
			FString Uri = FString::Printf(TEXT("api/v1/c/ddc/%s/%s/%s"), InNamespace, InBucket, InCacheKey);
			const FHttpRequest::Result Result = Request->PerformBlockingQuery<FHttpRequest::Head>(*Uri);
			if (FHttpRequest::IsSuccessResponse(Request->GetResponseCode()))
			{
				TRACE_COUNTER_ADD(HttpDDC_ExistHit, int64(1));
				bVerified[0] = true;
			}
		}
		else
		{
			// We have exceeded the threshold for concurrent connections, start or add this request
			// to a batched request.
			Request = QueueBatchRequest(
				InPool, 
				InNamespace,
				InBucket, 
				TConstArrayView<const TCHAR*>({ InCacheKey }),
				OutData ? TConstArrayView<TArray<uint8>*>({ OutData }) : TConstArrayView<TArray<uint8>*>(),
				bVerified
			);
		}
	}

	// Constructor specifically for batched head queries
	FDataRequestHelper(FRequestPool* InPool, const TCHAR* InNamespace, const TCHAR* InBucket, TConstArrayView<FString> InCacheKeys)
		: Request(nullptr)
		, Pool(InPool)
		, bVerified(false, InCacheKeys.Num())
	{
		// Transform the FString array to char pointers
		TArray<const TCHAR*> CacheKeys;
		Algo::Transform(InCacheKeys, CacheKeys, [](const FString& Key) { return *Key; });
		
		Request = Pool->GetFreeRequest();
		if (Request || InCacheKeys.Num() > UE_HTTPDDC_BATCH_SIZE)
		{
			// If the request is too big for existing batches, wait for a free connection and create our own.
			if (!Request)
			{
				Request = Pool->WaitForFreeRequest();
			}

			FQueuedBatchEntry Entry{
				InNamespace, 
				InBucket,
				CacheKeys,
				TConstArrayView<TArray<uint8>*>(),
				FHttpRequest::RequestVerb::Head,
				&bVerified
			};

			PerformBatchQuery(Request, TArrayView<FQueuedBatchEntry>(&Entry, 1));
		}
		else
		{
			Request = QueueBatchRequest(
				InPool, 
				InNamespace, 
				InBucket, 
				CacheKeys, 
				TConstArrayView<TArray<uint8>*>(), 
				bVerified
			);
		}
	}

	~FDataRequestHelper()
	{
		if (Request)
		{
			Pool->ReleaseRequestToPool(Request);
		}
	}

	static void StaticInitialize()
	{
		static bool Initialized = false;
		check(!Initialized);
		for (int32 i = 0; i < UE_HTTPDDC_BATCH_NUM; i++)
		{
			Batches[i].Reserved = 0;
			Batches[i].Ready = 0;
		}
		Initialized = true;
	}

	bool IsSuccess() const
	{
		return bVerified[0];
	}

	const TBitArray<>& IsBatchSuccess() const
	{
		return bVerified;
	}

	int64 GetResponseCode() const
	{
		return Request ? Request->GetResponseCode() : 0;
	}

private:

	struct FQueuedBatchEntry
	{
		const TCHAR* Namespace;
		const TCHAR* Bucket;
		TConstArrayView<const TCHAR*> CacheKeys;
		TConstArrayView<TArray<uint8>*> OutDatas;
		FHttpRequest::RequestVerb Verb;
		TBitArray<>* bSuccess;
	};

	struct FBatch
	{
		FQueuedBatchEntry Entries[UE_HTTPDDC_BATCH_SIZE];
		std::atomic<uint32> Reserved;
		std::atomic<uint32> Ready;
		std::atomic<uint32> WeightHint;
		FHttpRequest* Request;
		FEventRef Complete{ EEventMode::ManualReset };
	};

	FHttpRequest* Request;
	FRequestPool* Pool;
	TBitArray<> bVerified;
	static std::atomic<uint32> FirstAvailableBatch;
	static TStaticArray<FBatch, UE_HTTPDDC_BATCH_NUM> Batches;

	/**
	 * Queues up a request to be batched. Blocks until the query is made.
	 */
	static FHttpRequest* QueueBatchRequest(FRequestPool* InPool, 
		const TCHAR* InNamespace, 
		const TCHAR* InBucket, 
		TConstArrayView<const TCHAR*> InCacheKeys,
		TConstArrayView<TArray<uint8>*> OutDatas, 
		TBitArray<>& bOutVerified)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(HttpDDC_BatchQuery);
		check(InCacheKeys.Num() == OutDatas.Num() || OutDatas.Num() == 0);
		const uint32 RequestNum = InCacheKeys.Num();
		const uint32 RequestWeight = InCacheKeys.Num() * (OutDatas.Num() ? UE_HTTPDDC_BATCH_HEAD_WEIGHT : UE_HTTPDDC_BATCH_GET_WEIGHT);

		for (int32 i = 0; i < Batches.Num(); i++)
		{
			uint32 Index = (FirstAvailableBatch.load(std::memory_order_relaxed) + i) % Batches.Num();
			FBatch& Batch = Batches[Index];

			//Assign different weights to head vs. get queries
			if (Batch.WeightHint.load(std::memory_order_acquire) + RequestWeight > UE_HTTPDDC_BATCH_WEIGHT_HINT)
			{
				continue;
			}

			// Attempt to reserve a spot in the batch
			const uint32 Reserve = Batch.Reserved.fetch_add(1, std::memory_order_acquire);
			if (Reserve >= UE_HTTPDDC_BATCH_SIZE)
			{
				// We didn't manage to snag a valid reserve index try next batch
				continue;
			}

			// Add our weight to the batch. Note we are treating it as a hint, so don't syncronize.
			const uint32 ActualWeight = Batch.WeightHint.fetch_add(RequestWeight, std::memory_order_release);

			TAnsiStringBuilder<64> BatchString;
			BatchString << "HttpDDC_Batch" << Index;
			TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*BatchString);

			if (Reserve == (UE_HTTPDDC_BATCH_SIZE - 1))
			{
				FirstAvailableBatch++;
			}

			Batch.Entries[Reserve] = FQueuedBatchEntry{
				InNamespace,
				InBucket,
				InCacheKeys,
				OutDatas,
				OutDatas.Num() ? FHttpRequest::RequestVerb::Get : FHttpRequest::RequestVerb::Head,
				&bOutVerified
			};

			// Signal we are ready for batch to be submitted
			Batch.Ready.fetch_add(1u, std::memory_order_release);

			FHttpRequest* Request = nullptr;

			// The first to reserve a slot is the "driver" of the batch
			if (Reserve == 0)
			{
				Batch.Request = InPool->WaitForFreeRequest();

				// Make sure no new requests are added
				const uint32 Reserved = FMath::Min((uint32)UE_HTTPDDC_BATCH_SIZE, Batch.Reserved.fetch_add(UE_HTTPDDC_BATCH_SIZE, std::memory_order_acquire));

				// Give other threads time to copy their data to batch
				while (Batch.Ready.load(std::memory_order_acquire) < Reserved) {}

				// Increment request ref count to reflect all waiting threads
				InPool->MakeRequestShared(Batch.Request, Reserved);

				// Do the actual query and write response to respective target arrays
				PerformBatchQuery(Batch.Request, TArrayView<FQueuedBatchEntry>(Batch.Entries, Batch.Ready));

				// Signal to waiting threads the batch is complete
				Batch.Complete->Trigger();

				// Store away the request and wait until other threads have too
				Request = Batch.Request;
				while (Batch.Ready.load(std::memory_order_acquire) > 1) {}

				//Reset batch for next use
				Batch.Complete->Reset();
				Batch.WeightHint.store(0, std::memory_order_release);
				Batch.Ready.store(0, std::memory_order_release);
				Batch.Reserved.store(0, std::memory_order_release);
			}
			else
			{
				// Wait until "driver" has done query
				Batch.Complete->Wait(~0);

				// Store away request and signal we are done
				Request = Batch.Request;
				Batch.Ready.fetch_sub(1u, std::memory_order_release);
			}

			return Request;
		}

		return nullptr;
	}


	/**
	 * Creates request uri and headers and submits the request
	 */
	static void PerformBatchQuery(FHttpRequest* Request, TArrayView<FQueuedBatchEntry> Entries)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(HttpDDC_BatchGet);
		const TCHAR* Uri(TEXT("api/v1/c/ddc-rpc/batchget"));
		int64 ResponseCode = 0; uint32 Attempts = 0;

		//Prepare request object
		TArray<TSharedPtr<FJsonValue>> Operations;
		for (const FQueuedBatchEntry& Entry : Entries)
		{
			for (int32 KeyIdx = 0; KeyIdx < Entry.CacheKeys.Num(); KeyIdx++)
			{
				TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
				Object->SetField(TEXT("bucket"), MakeShared<FJsonValueString>(Entry.Bucket));
				Object->SetField(TEXT("key"), MakeShared<FJsonValueString>(Entry.CacheKeys[KeyIdx]));
				if (Entry.Verb == FHttpRequest::RequestVerb::Head)
				{
					Object->SetField(TEXT("verb"), MakeShared<FJsonValueString>(TEXT("HEAD")));
				}
				Operations.Add(MakeShared<FJsonValueObject>(Object));
			}
		}
		TSharedPtr<FJsonObject> RequestObject = MakeShared<FJsonObject>();
		RequestObject->SetField(TEXT("namespace"), MakeShared<FJsonValueString>(Entries[0].Namespace));
		RequestObject->SetField(TEXT("operations"), MakeShared<FJsonValueArray>(Operations));

		//Serialize to a buffer
		FBufferArchive RequestData;
		if (FJsonSerializer::Serialize(RequestObject.ToSharedRef(), TJsonWriterFactory<ANSICHAR, TCondensedJsonPrintPolicy<ANSICHAR>>::Create(&RequestData)))
		{
			Request->PerformBlockingUpload<FHttpRequest::PostJson>(Uri, MakeArrayView(RequestData));
			ResponseCode = Request->GetResponseCode();

			if (ResponseCode == 200)
			{
				const TArray<uint8>& ResponseBuffer = Request->GetResponseBuffer();
				const uint8* Response = ResponseBuffer.GetData();
				const int32 ResponseSize = ResponseBuffer.Num();

				// Parse the response and move the data to the target requests.
				if (ParseBatchedResponse(Response, ResponseSize, Entries))
				{
					UE_LOG(LogDerivedDataCache, VeryVerbose, TEXT("Batch query with %d operations completed."), Entries.Num());
					return;
				}
			}
		}
		
		// If we get here the request failed.
		UE_LOG(LogDerivedDataCache, Display, TEXT("Batch query failed. Query: %s"), ANSI_TO_TCHAR((ANSICHAR*)RequestData.GetData()));

		// Set all batch operations to failures
		for (FQueuedBatchEntry Entry : Entries)
		{
			Entry.bSuccess->SetRange(0, Entry.CacheKeys.Num(), false);
		}
	}

	// Searches for potentially multiple key requests that are satisfied the given cache key result
	// Search strategy is exhaustive forward search from the last found entry.  If the results come in ordered the same as the requests,
	//  and there are no duplicates, the search will be somewhat efficient (still has to do exhaustive searching looking for duplicates).
	//  If the results are unordered or there are duplicates, search will become more inefficient.
	struct FRequestSearchHelper
	{
		FRequestSearchHelper(TArrayView<FQueuedBatchEntry> InRequests, const FUTF8ToTCHAR& InCacheKey, int32 InEntryIdx, int32 InKeyIdx)
			: Requests(InRequests)
			, CacheKey(InCacheKey)
			, StartEntryIdx(InEntryIdx)
			, StartKeyIdx(InKeyIdx)
		{}

		bool FindNext(int32& EntryIdx, int32& KeyIdx)
		{
			int32 CurrentEntryIdx = EntryIdx;
			int32 CurrentKeyIdx = KeyIdx;
			do
			{
				if (FCString::Stricmp(Requests[CurrentEntryIdx].CacheKeys[CurrentKeyIdx], CacheKey.Get()) == 0)
				{
					EntryIdx = CurrentEntryIdx;
					KeyIdx = CurrentKeyIdx;
					return true;
				}
			} while (AdvanceIndices(CurrentEntryIdx, CurrentKeyIdx));

			return false;
		}

		bool AdvanceIndices(int32& EntryIdx, int32& KeyIdx)
		{
			if (++KeyIdx >= Requests[EntryIdx].CacheKeys.Num())
			{
				EntryIdx = (EntryIdx + 1) % Requests.Num();
				KeyIdx = 0;
			}

			return !((EntryIdx == StartEntryIdx) && (KeyIdx == StartKeyIdx));
		}

		TArrayView<FQueuedBatchEntry> Requests;
		const FUTF8ToTCHAR& CacheKey;
		int32 StartEntryIdx;
		int32 StartKeyIdx;
	};

	/**
	 * Parses a batched response stream, moves the data to target requests and marks them with result.
	 * @param Response Pointer to Response buffer
	 * @param ResponseSize Size of response buffer
	 * @param Requests Requests that will be filled with data.
	 * @return True if response was successfully parsed, false otherwise.
	 */
	static bool ParseBatchedResponse(const uint8* ResponseStart, const int32 ResponseSize, TArrayView<FQueuedBatchEntry> Requests)
	{
		// The expected data stream is structured accordingly
		// {"JPTR"} {PayloadCount:uint32} {{"JPEE"} {Name:cstr} {Result:uint8} {Hash:IoHash} {Size:uint64} {Payload...}} ...

		// Above result value
		enum OpResult : uint8
		{
			Ok = 0,			// Op finished succesfully
			Error = 1,		// Error during op
			NotFound = 2,	// Key was not found
			Exists = 3		// Used to indicate head op success
		};

		const TCHAR ResponseErrorMessage[] = TEXT("Malformed response from server.");
		const ANSICHAR* ProtocolMagic = "JPTR";
		const ANSICHAR* PayloadMagic = "JPEE";
		const uint32 MagicSize = 4;
		const uint8* Response = ResponseStart;
		const uint8* ResponseEnd = Response + ResponseSize;

		// Check that the stream starts with the protocol magic
		if (FMemory::Memcmp(ProtocolMagic, Response, MagicSize) != 0)
		{
			UE_LOG(LogDerivedDataCache, Display, ResponseErrorMessage);
			return false;
		}
		Response += MagicSize;

		// Number of payloads recieved
		uint32 PayloadCount = *(uint32*)Response;
		Response += sizeof(uint32);

		uint32 PayloadIdx = 0; 	// Current processed result
		int32 EntryIdx = 0; 	// Current Entry index
		int32 KeyIdx = 0; 		// Current Key index for current Entry

		while (Response < ResponseEnd && FMemory::Memcmp(PayloadMagic, Response, MagicSize) == 0)
		{
			PayloadIdx++;
			Response += MagicSize;

			const ANSICHAR* PayloadNameA = (const ANSICHAR*)Response;
			Response += FCStringAnsi::Strlen(PayloadNameA) + 1; //String and zero termination
			const ANSICHAR* CacheKeyA = FCStringAnsi::Strrchr(PayloadNameA, '.') + 1; // "namespace.bucket.cachekey"

			// Find the payload among the requests.  Payloads may be returned in any order and if the same cache key was part of two requests,
			// a single payload may satisfy multiple cache keys in multiple requests.
			FUTF8ToTCHAR CacheKey(CacheKeyA);
			FRequestSearchHelper RequestSearch(Requests, CacheKey, EntryIdx, KeyIdx);
			bool bFoundAny = false;

			const uint8* ResponseRewindMark = Response;
			while (RequestSearch.FindNext(EntryIdx, KeyIdx))
			{
				Response = ResponseRewindMark;
				bFoundAny = true;

				FQueuedBatchEntry& RequestOp = Requests[EntryIdx];
				TBitArray<>& bSuccess = *RequestOp.bSuccess;

				// Result of the operation
				uint8 PayloadResult = *Response;
				Response += sizeof(uint8);

				switch (PayloadResult)
				{
				
				case OpResult::Ok:
					{
						TArray<uint8>* OutData = RequestOp.OutDatas[KeyIdx];

						// Payload hash of the following payload data
						FIoHash PayloadHash = *(FIoHash*)Response;
						Response += sizeof(FIoHash);

						// Size of the following payload data
						const uint64 PayloadSize = *(uint64*)Response;
						Response += sizeof(uint64);

						if (PayloadSize > 0)
						{
							if (bSuccess[KeyIdx])
							{
								Response += PayloadSize;
							}
							else
							{
								OutData->Append(Response, PayloadSize);
								Response += PayloadSize;
								// Verify the recieved and parsed payload
								if (VerifyPayload(PayloadHash, *OutData))
								{
									TRACE_COUNTER_ADD(HttpDDC_GetHit, int64(1));
									TRACE_COUNTER_ADD(HttpDDC_BytesReceived, int64(PayloadSize));
									
									bSuccess[KeyIdx] = true;
								}
								else
								{
									OutData->Empty();
									bSuccess[KeyIdx] = false;
								}
							}
						}
						else
						{
							bSuccess[KeyIdx] = false;
						}
					}
					break;

				case OpResult::Exists:
					{
						TRACE_COUNTER_ADD(HttpDDC_ExistHit, int64(1));
						bSuccess[KeyIdx] = true;
					}
					break;

				default:
				case OpResult::Error:
					UE_LOG(LogDerivedDataCache, Display, TEXT("Server error while getting %s"), CacheKey.Get());
					// intentional falltrough

				case OpResult::NotFound:
					bSuccess[KeyIdx] = false;
					break;

				}

				if (!RequestSearch.AdvanceIndices(EntryIdx, KeyIdx))
				{
					break;
				}
			}

			if (!bFoundAny)
			{
				UE_LOG(LogDerivedDataCache, Error, ResponseErrorMessage);
				return false;
			}
		}

		// Have we parsed all the payloads from the message?
		if (PayloadIdx != PayloadCount)
		{
			UE_LOG(LogDerivedDataCache, Display, TEXT("%s: Found %d payloads but %d was reported."), ResponseErrorMessage, PayloadIdx, PayloadCount);
		}

		return true;
	}
};

TStaticArray<FDataRequestHelper::FBatch, UE_HTTPDDC_BATCH_NUM> FDataRequestHelper::Batches;
std::atomic<uint32> FDataRequestHelper::FirstAvailableBatch;

//----------------------------------------------------------------------------------------------------------
// FDataUploadHelper
//----------------------------------------------------------------------------------------------------------
struct FDataUploadHelper
{
	FDataUploadHelper(FRequestPool* InPool, 
		const TCHAR* InNamespace, 
		const TCHAR* InBucket, 
		const TCHAR* InCacheKey, 
		const TArrayView<const uint8>& InData,
		FDerivedDataCacheUsageStats& InUsageStats)
		: ResponseCode(0)
		, bSuccess(false)
		, bQueued(false)
	{
		FHttpRequest* Request = InPool->GetFreeRequest();
		if (Request)
		{
			ResponseCode = PerformPut(Request, InNamespace, InBucket, InCacheKey, InData, InUsageStats);
			bSuccess = FHttpRequest::IsSuccessResponse(Request->GetResponseCode());

			ProcessQueuedPutsAndReleaseRequest(InPool, Request, InUsageStats);
		}
		else
		{
			FQueuedEntry* Entry = new FQueuedEntry(InNamespace, InBucket, InCacheKey, InData);
			QueuedPuts.Push(Entry);
			bSuccess = true;
			bQueued = true;
			
			// A request may have been released while the entry was being queued.
			Request = InPool->GetFreeRequest();
			if (Request)
			{
				ProcessQueuedPutsAndReleaseRequest(InPool, Request, InUsageStats);
			}
		}
	}

	bool IsSuccess() const
	{
		return bSuccess;
	}

	int64 GetResponseCode() const
	{
		return ResponseCode;
	}

	bool IsQueued() const
	{
		return bQueued;
	}

private:

	struct FQueuedEntry
	{
		FString Namespace;
		FString Bucket;
		FString CacheKey;
		TArray<uint8> Data;

		FQueuedEntry(const TCHAR* InNamespace, const TCHAR* InBucket, const TCHAR* InCacheKey, const TArrayView<const uint8> InData)
			: Namespace(InNamespace)
			, Bucket(InBucket)
			, CacheKey(InCacheKey)
			, Data(InData) // Copies the data!
		{}
	};

	static TLockFreePointerListUnordered<FQueuedEntry, PLATFORM_CACHE_LINE_SIZE> QueuedPuts;

	int64 ResponseCode;
	bool bSuccess;
	bool bQueued;

	static void ProcessQueuedPutsAndReleaseRequest(FRequestPool* Pool, FHttpRequest* Request, FDerivedDataCacheUsageStats& UsageStats)
	{
		while (Request)
		{
			// Make sure that whether we early exit or execute past the end of this scope that
			// the request is released back to the pool.
			{
				ON_SCOPE_EXIT
				{
					Pool->ReleaseRequestToPool(Request);
				};

				if (ShouldAbortForShutdown())
				{
					return;
				}

				while (FQueuedEntry* Entry = QueuedPuts.Pop())
				{
					Request->Reset();
					PerformPut(Request, *Entry->Namespace, *Entry->Bucket, *Entry->CacheKey, Entry->Data, UsageStats);
					delete Entry;

					if (ShouldAbortForShutdown())
					{
						return;
					}
				}
			}

			// An entry may have been queued while the request was being released.
			if (QueuedPuts.IsEmpty())
			{
				break;
			}

			// Process the queue again if a request is free, otherwise the thread that got the request will process it.
			Request = Pool->GetFreeRequest();
		}
	}

	static int64 PerformPut(FHttpRequest* Request, const TCHAR* Namespace, const TCHAR* Bucket, const TCHAR* CacheKey, const TArrayView<const uint8> Data, FDerivedDataCacheUsageStats& UsageStats)
	{
		COOK_STAT(auto Timer = UsageStats.TimePut());

		HashPayload(Request, Data);

		TStringBuilder<256> Uri;
		Uri.Appendf(TEXT("api/v1/c/ddc/%s/%s/%s"), Namespace, Bucket, CacheKey);

		Request->PerformBlockingUpload<FHttpRequest::Put>(*Uri, Data);

		const int64 ResponseCode = Request->GetResponseCode();
		if (FHttpRequest::IsSuccessResponse(ResponseCode))
		{
			TRACE_COUNTER_ADD(HttpDDC_BytesSent, int64(Request->GetBytesSent()));
			COOK_STAT(Timer.AddHit(Request->GetBytesSent()));
		}

		return Request->GetResponseCode();
	}
};

TLockFreePointerListUnordered<FDataUploadHelper::FQueuedEntry, PLATFORM_CACHE_LINE_SIZE> FDataUploadHelper::QueuedPuts;

#endif // WITH_DATAREQUEST_HELPER

//----------------------------------------------------------------------------------------------------------
// Certificate checking
//----------------------------------------------------------------------------------------------------------

#if WITH_SSL

static int SslCertVerify(int PreverifyOk, X509_STORE_CTX* Context)
{
	if (PreverifyOk == 1)
	{
		SSL* Handle = static_cast<SSL*>(X509_STORE_CTX_get_ex_data(Context, SSL_get_ex_data_X509_STORE_CTX_idx()));
		check(Handle);

		SSL_CTX* SslContext = SSL_get_SSL_CTX(Handle);
		check(SslContext);

		FHttpRequest* Request = static_cast<FHttpRequest*>(SSL_CTX_get_app_data(SslContext));
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
 * Verifies the integrity of the received data using supplied checksum.
 * @param Hash received hash value.
 * @param Payload Payload received.
 * @return True if the data is correct, false if checksums doesn't match.
 */
bool VerifyPayload(const FSHAHash& Hash, const TArray<uint8>& Payload)
{
	FSHAHash PayloadHash;
	FSHA1::HashBuffer(Payload.GetData(), Payload.Num(), PayloadHash.Hash);

	if (Hash != PayloadHash)
	{
		UE_LOG(LogDerivedDataCache,
			Warning,
			TEXT("Checksum from server did not match received data (%s vs %s). Discarding cached result."),
			*WriteToString<48>(Hash),
			*WriteToString<48>(PayloadHash)
		);
		return false;
	}

	return true;
}

/**
 * Verifies the integrity of the received data using supplied checksum.
 * @param Hash received hash value.
 * @param Payload Payload received.
 * @return True if the data is correct, false if checksums doesn't match.
 */
bool VerifyPayload(const FIoHash& Hash, const TArray<uint8>& Payload)
{
	FIoHash PayloadHash = FIoHash::HashBuffer(Payload.GetData(), Payload.Num());

	if (Hash != PayloadHash)
	{
		UE_LOG(LogDerivedDataCache,
			Warning,
			TEXT("Checksum from server did not match received data (%s vs %s). Discarding cached result."),
			*WriteToString<48>(Hash),
			*WriteToString<48>(PayloadHash)
		);
		return false;
	}

	return true;
}


/**
 * Verifies the integrity of the received data using supplied checksum.
 * @param Request Request that the data was be received with.
 * @param Payload Payload received.
 * @return True if the data is correct, false if checksums doesn't match.
 */
bool VerifyRequest(const FHttpRequest* Request, const TArray<uint8>& Payload)
{
	FString ReceivedHashStr;
	if (Request->GetHeader("X-Jupiter-Sha1", ReceivedHashStr))
	{
		FSHAHash ReceivedHash;
		ReceivedHash.FromString(ReceivedHashStr);
		return VerifyPayload(ReceivedHash, Payload);
	}
	if (Request->GetHeader("X-Jupiter-IoHash", ReceivedHashStr))
	{
		FIoHash ReceivedHash(ReceivedHashStr);
		return VerifyPayload(ReceivedHash, Payload);
	}
	UE_LOG(LogDerivedDataCache, Warning, TEXT("HTTP server did not send a content hash. Wrong server version?"));
	return true;
}

/**
 * Adds a checksum (as request header) for a given payload. Jupiter will use this to verify the integrity
 * of the received data.
 * @param Request Request that the data will be sent with.
 * @param Payload Payload that will be sent.
 * @return True on success, false on failure.
 */
bool HashPayload(FHttpRequest* Request, const TArrayView<const uint8> Payload)
{
	FIoHash PayloadHash = FIoHash::HashBuffer(Payload.GetData(), Payload.Num());
	Request->SetHeader(TEXT("X-Jupiter-IoHash"), *WriteToString<48>(PayloadHash));
	return true;
}

bool ShouldAbortForShutdown()
{
	return !GIsBuildMachine && FDerivedDataBackend::Get().IsShuttingDown();
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
	ICacheFactory& InFactory,
	const TCHAR* InServiceUrl, 
	const TCHAR* InNamespace, 
	const TCHAR* InOAuthProvider,
	const TCHAR* InOAuthClientId,
	const TCHAR* InOAuthSecret,
	const bool bInReadOnly)
	: Factory(InFactory)
	, Domain(InServiceUrl)
	, Namespace(InNamespace)
	, DefaultBucket(TEXT("default"))
	, OAuthProvider(InOAuthProvider)
	, OAuthClientId(InOAuthClientId)
	, OAuthSecret(InOAuthSecret)
	, Access(nullptr)
	, bIsUsable(false)
	, bReadOnly(bInReadOnly)
	, FailedLoginAttempts(0)
	, SpeedClass(ESpeedClass::Slow)
{
#if WITH_DATAREQUEST_HELPER
	FDataRequestHelper::StaticInitialize();
#endif
	if (IsServiceReady() && AcquireAccessToken())
	{
		GetRequestPools[0] = MakeUnique<FRequestPool>(InServiceUrl, Access.Get(), UE_HTTPDDC_GET_REQUEST_POOL_SIZE);
		GetRequestPools[1] = MakeUnique<FRequestPool>(InServiceUrl, Access.Get(), 1);
		PutRequestPools[0] = MakeUnique<FRequestPool>(InServiceUrl, Access.Get(), UE_HTTPDDC_PUT_REQUEST_POOL_SIZE);
		PutRequestPools[1] = MakeUnique<FRequestPool>(InServiceUrl, Access.Get(), 1);
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

bool FHttpDerivedDataBackend::TryToPrefetch(TConstArrayView<FString> CacheKeys)
{
	return false;
}

bool FHttpDerivedDataBackend::WouldCache(const TCHAR* CacheKey, TArrayView<const uint8> InData)
{
	return IsWritable();
}

FHttpDerivedDataBackend::ESpeedClass FHttpDerivedDataBackend::GetSpeedClass() const
{
	return SpeedClass;
}

bool FHttpDerivedDataBackend::ApplyDebugOptions(FBackendDebugOptions& InOptions)
{
	return false;
}


bool FHttpDerivedDataBackend::IsServiceReady()
{
	FHttpRequest Request(*Domain, nullptr, false);
	FHttpRequest::Result Result = Request.PerformBlockingDownload(TEXT("health/ready"), nullptr);
	
	if (Result == FHttpRequest::Success && Request.GetResponseCode() == 200)
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

		FHttpRequest Request(*AuthDomain, nullptr, false);

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

		FHttpRequest::Result Result = Request.PerformBlockingUpload<FHttpRequest::Post>(*Uri, MakeArrayView(FormData));

		if (Result == FHttpRequest::Success && Request.GetResponseCode() == 200)
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
					// Reset failed login attempts, the service is indeed alive.
					FailedLoginAttempts = 0;
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

#if WITH_DATAREQUEST_HELPER
	// Retry request until we get an accepted response or exhaust allowed number of attempts.
	for (int32 Attempts = 0; Attempts < UE_HTTPDDC_MAX_ATTEMPTS; ++Attempts)
	{
		FDataRequestHelper RequestHelper(GetRequestPools[IsInGameThread()].Get(), *Namespace, *DefaultBucket, CacheKey, nullptr);
		const int64 ResponseCode = RequestHelper.GetResponseCode();

		if (FHttpRequest::IsSuccessResponse(ResponseCode) && RequestHelper.IsSuccess())
		{
			COOK_STAT(Timer.AddHit(0));
			return true;
		}

		if (!ShouldRetryOnError(ResponseCode))
		{
			return false;
		}
	}
#else
	FString Uri = FString::Printf(TEXT("api/v1/c/ddc/%s/%s/%s"), *Namespace, *DefaultBucket, CacheKey);

	// Retry request until we get an accepted response or exhaust allowed number of attempts.
	for (int32 Attempts = 0; Attempts < UE_HTTPDDC_MAX_ATTEMPTS; ++Attempts)
	{
		FScopedRequestPtr Request(GetRequestPools[IsInGameThread()].Get());
		const FHttpRequest::Result Result = Request->PerformBlockingQuery<FHttpRequest::Head>(*Uri);
		const int64 ResponseCode = Request->GetResponseCode();

		if (FHttpRequest::IsSuccessResponse(ResponseCode) || ResponseCode == 400)
		{
			const bool bIsHit = (Result == FHttpRequest::Success && FHttpRequest::IsSuccessResponse(ResponseCode));
			if (bIsHit)
			{
				TRACE_COUNTER_ADD(HttpDDC_ExistHit, int64(1));
				COOK_STAT(Timer.AddHit(0));
			}
			return bIsHit;
		}

		if (!ShouldRetryOnError(ResponseCode))
		{
			break;
		}
	}
#endif

	return false;
}

TBitArray<> FHttpDerivedDataBackend::CachedDataProbablyExistsBatch(TConstArrayView<FString> CacheKeys)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(HttpDDC_Exist);
	TRACE_COUNTER_ADD(HttpDDC_Exist, int64(1));
	COOK_STAT(auto Timer = UsageStats.TimeProbablyExists());
#if WITH_DATAREQUEST_HELPER
	for (int32 Attempts = 0; Attempts < UE_HTTPDDC_MAX_ATTEMPTS; ++Attempts)
	{
		FDataRequestHelper RequestHelper(GetRequestPools[IsInGameThread()].Get(), *Namespace, *DefaultBucket, CacheKeys);
		const int64 ResponseCode = RequestHelper.GetResponseCode();

		if (FHttpRequest::IsSuccessResponse(ResponseCode) && RequestHelper.IsSuccess())
		{
			COOK_STAT(Timer.AddHit(0));
			return RequestHelper.IsBatchSuccess();
		}

		if (!ShouldRetryOnError(ResponseCode))
		{
			return RequestHelper.IsBatchSuccess();
		}
	}
#else
	const TCHAR* const Uri = TEXT("api/v1/c/ddc-rpc");

	TAnsiStringBuilder<512> Body;
	const FTCHARToUTF8 AnsiNamespace(*Namespace);
	const FTCHARToUTF8 AnsiBucket(*DefaultBucket);
	Body << "{\"Operations\":[";
	for (const FString& CacheKey : CacheKeys)
	{
		Body << "{\"Namespace\":\"" << AnsiNamespace.Get() << "\",\"Bucket\":\"" << AnsiBucket.Get() << "\",";
		Body << "\"Id\":\"" << FTCHARToUTF8(*CacheKey).Get() << "\",\"Op\":\"HEAD\"},";
	}
	Body.RemoveSuffix(1);
	Body << "]}";

	TConstArrayView<uint8> BodyView(reinterpret_cast<const uint8*>(Body.ToString()), Body.Len());

	// Retry request until we get an accepted response or exhaust allowed number of attempts.
	for (int32 Attempts = 0; Attempts < UE_HTTPDDC_MAX_ATTEMPTS; ++Attempts)
	{
		FScopedRequestPtr Request(GetRequestPools[IsInGameThread()].Get());
		const FHttpRequest::Result Result = Request->PerformBlockingUpload<FHttpRequest::PostJson>(Uri, BodyView);
		const int64 ResponseCode = Request->GetResponseCode();

		if (Result == FHttpRequest::Success && ResponseCode == 200)
		{
			TArray<TSharedPtr<FJsonValue>> ResponseArray = Request->GetResponseAsJsonArray();

			TBitArray<> Exists;
			Exists.Reserve(CacheKeys.Num());
			for (const FString& CacheKey : CacheKeys)
			{
				const TSharedPtr<FJsonValue>* FoundResponse = Algo::FindByPredicate(ResponseArray, [&CacheKey](const TSharedPtr<FJsonValue>& Response) {
					FString Key;
					Response->TryGetString(Key);
					return Key == CacheKey;
				});

				Exists.Add(FoundResponse != nullptr);
			}

			if (Exists.CountSetBits() == CacheKeys.Num())
			{
				TRACE_COUNTER_ADD(HttpDDC_ExistHit, int64(1));
				COOK_STAT(Timer.AddHit(0));
			}
			return Exists;
		}

		if (!ShouldRetryOnError(ResponseCode))
		{
			break;
		}
	}
#endif

	return TBitArray<>(false, CacheKeys.Num());
}

bool FHttpDerivedDataBackend::GetCachedData(const TCHAR* CacheKey, TArray<uint8>& OutData)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(HttpDDC_Get);
	TRACE_COUNTER_ADD(HttpDDC_Get, int64(1));
	COOK_STAT(auto Timer = UsageStats.TimeGet());

#if WITH_DATAREQUEST_HELPER
	// Retry request until we get an accepted response or exhaust allowed number of attempts.
	for (int32 Attempts = 0; Attempts < UE_HTTPDDC_MAX_ATTEMPTS; ++Attempts)
	{
		FDataRequestHelper RequestHelper(GetRequestPools[IsInGameThread()].Get(), *Namespace, *DefaultBucket, CacheKey, &OutData);
		const int64 ResponseCode = RequestHelper.GetResponseCode();

		if (FHttpRequest::IsSuccessResponse(ResponseCode) && RequestHelper.IsSuccess())
		{
			COOK_STAT(Timer.AddHit(OutData.Num()));
			check(OutData.Num() > 0);
			return true;
		}

		if (!ShouldRetryOnError(ResponseCode))
		{
			return false;
		}
	}
#else 
	FString Uri = FString::Printf(TEXT("api/v1/c/ddc/%s/%s/%s.raw"), *Namespace, *DefaultBucket, CacheKey);

	// Retry request until we get an accepted response or exhaust allowed number of attempts.
	for (int32 Attempts = 0; Attempts < UE_HTTPDDC_MAX_ATTEMPTS; ++Attempts)
	{
		FScopedRequestPtr Request(GetRequestPools[IsInGameThread()].Get());
		if (Request.IsValid())
		{
			FHttpRequest::Result Result = Request->PerformBlockingDownload(*Uri, &OutData);
			const uint64 ResponseCode = Request->GetResponseCode();

			// Request was successful, make sure we got all the expected data.
			if (FHttpRequest::IsSuccessResponse(ResponseCode) && VerifyRequest(Request.Get(), OutData))
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
		}
	}
#endif

	return false;
}

FDerivedDataBackendInterface::EPutStatus FHttpDerivedDataBackend::PutCachedData(const TCHAR* CacheKey, TArrayView<const uint8> InData, bool bPutEvenIfExists)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(HttpDDC_Put);

	if (!IsWritable())
	{
		UE_LOG(LogDerivedDataCache, Verbose, TEXT("%s is read only. Skipping put of %s"), *GetName(), CacheKey);
		return EPutStatus::NotCached;
	}

#if WITH_DATAREQUEST_HELPER
	for (int32 Attempts = 0; Attempts < UE_HTTPDDC_MAX_ATTEMPTS; ++Attempts)
	{
		FDataUploadHelper Request(PutRequestPools[IsInGameThread()].Get(), *Namespace, *DefaultBucket,	CacheKey, InData, UsageStats);

		if (ShouldAbortForShutdown())
		{
			return EPutStatus::NotCached;
		}

		const int64 ResponseCode = Request.GetResponseCode();

		if (Request.IsSuccess() && (Request.IsQueued() || FHttpRequest::IsSuccessResponse(ResponseCode)))
		{
			return Request.IsQueued() ? EPutStatus::Executing : EPutStatus::Cached;
		}

		if (!ShouldRetryOnError(ResponseCode))
		{
			return EPutStatus::NotCached;
		}
	}
#else
	COOK_STAT(auto Timer = UsageStats.TimePut());

	FString Uri = FString::Printf(TEXT("api/v1/c/ddc/%s/%s/%s"), *Namespace, *DefaultBucket, CacheKey);
	int64 ResponseCode = 0; uint32 Attempts = 0;

	// Retry request until we get an accepted response or exhaust allowed number of attempts.
	while (ResponseCode == 0 && ++Attempts < UE_HTTPDDC_MAX_ATTEMPTS)
	{
		if (ShouldAbortForShutdown())
		{
			return EPutStatus::NotCached;
		}

		FScopedRequestPtr Request(PutRequestPools[IsInGameThread()].Get());
		if (Request.IsValid())
		{
			// Append the content hash to the header
			HashPayload(Request.Get(), InData);

			FHttpRequest::Result Result = Request->PerformBlockingUpload<FHttpRequest::Put>(*Uri, InData);
			ResponseCode = Request->GetResponseCode();

			if (ResponseCode == 200)
			{
				TRACE_COUNTER_ADD(HttpDDC_BytesSent, int64(Request->GetBytesSent()));
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
#endif // WITH_DATAREQUEST_HELPER

	return EPutStatus::NotCached;
}

void FHttpDerivedDataBackend::RemoveCachedData(const TCHAR* CacheKey, bool bTransient)
{
	// do not remove transient data as Jupiter does its own verification of the content and cleans itself up
	if (!IsWritable() || bTransient)
		return;
	
	TRACE_CPUPROFILER_EVENT_SCOPE(HttpDDC_Remove);
	FString Uri = FString::Printf(TEXT("api/v1/c/ddc/%s/%s/%s"), *Namespace, *DefaultBucket, CacheKey);
	int64 ResponseCode = 0; uint32 Attempts = 0;

	// Retry request until we get an accepted response or exhaust allowed number of attempts.
	while (ResponseCode == 0 && ++Attempts < UE_HTTPDDC_MAX_ATTEMPTS)
	{
		FScopedRequestPtr Request(PutRequestPools[IsInGameThread()].Get());
		if (Request.IsValid())
		{
			FHttpRequest::Result Result = Request->PerformBlockingQuery<FHttpRequest::Delete>(*Uri);
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

FRequest FHttpDerivedDataBackend::Put(
	TArrayView<FCacheRecord> Records,
	FStringView Context,
	ECachePolicy Policy,
	EPriority Priority,
	FOnCachePutComplete&& OnComplete)
{
	if (OnComplete)
	{
		for (const FCacheRecord& Record : Records)
		{
			OnComplete({Record.GetKey(), EStatus::Error});
		}
	}
	return FRequest();
}

FRequest FHttpDerivedDataBackend::Get(
	TConstArrayView<FCacheKey> Keys,
	FStringView Context,
	ECachePolicy Policy,
	EPriority Priority,
	FOnCacheGetComplete&& OnComplete)
{
	if (OnComplete)
	{
		for (const FCacheKey& Key : Keys)
		{
			OnComplete({Factory.CreateRecord(Key).Build(), EStatus::Error});
		}
	}
	return FRequest();
}

FRequest FHttpDerivedDataBackend::GetPayload(
	TConstArrayView<FCachePayloadKey> Keys,
	FStringView Context,
	ECachePolicy Policy,
	EPriority Priority,
	FOnCacheGetPayloadComplete&& OnComplete)
{
	if (OnComplete)
	{
		for (const FCachePayloadKey& Key : Keys)
		{
			OnComplete({Key.CacheKey, FPayload(Key.Id), EStatus::Error});
		}
	}
	return FRequest();
}

} // UE::DerivedData::Backends

#endif //WITH_HTTP_DDC_BACKEND

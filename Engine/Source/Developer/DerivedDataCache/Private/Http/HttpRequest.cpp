// Copyright Epic Games, Inc. All Rights Reserved.

#include "HttpClient.h"

#if WITH_HTTP_CLIENT
#include "DerivedDataLegacyCacheStore.h"
#include "Logging/LogMacros.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#define UE_HTTPDDC_HTTP_REQUEST_TIMEOUT_SECONDS 30L
#define UE_HTTPDDC_HTTP_REQUEST_TIMEOUT_ENABLED 1
#define UE_HTTPDDC_HTTP_DEBUG 0
#define UE_HTTPDDC_MAX_BUFFER_RESERVE 104857600u

namespace UE
{

static bool bHttpEnableAsync = true;
static FAutoConsoleVariableRef CVarHttpEnableAsync(
	TEXT("DDC.Http.EnableAsync"),
	bHttpEnableAsync,
	TEXT("If true, async operations are permitted, otherwise all operations are forced to be synchronous."),
	ECVF_Default);

FHttpRequest::FHttpRequest(const TCHAR* InDomain, const TCHAR* InEffectiveDomain, FHttpAccessToken* InAuthorizationToken, FHttpSharedData* InSharedData, bool bInLogErrors)
	: SharedData(InSharedData)
	, AsyncData(nullptr)
	, bLogErrors(bInLogErrors)
	, Domain(InDomain)
	, EffectiveDomain(InEffectiveDomain)
	, AuthorizationToken(InAuthorizationToken)
{
	Curl = curl_easy_init();
	Reset();
}

FHttpRequest::~FHttpRequest()
{
	curl_easy_cleanup(Curl);
	check(!AsyncData);
}

void FHttpRequest::Reset()
{
	Headers.Reset();
	ResponseHeader.Reset();
	ResponseBuffer.Reset();
	ResponseCode = 0;
	ReadDataView = FMemoryView();
	WriteDataBufferPtr = nullptr;
	WriteHeaderBufferPtr = nullptr;
	BytesSent = 0;
	BytesReceived = 0;
	Attempts = 0;
	CurlResult = CURL_LAST;

	curl_easy_reset(Curl);
	check(!AsyncData);

	// Options that are always set for all connections.
#if UE_HTTPDDC_HTTP_REQUEST_TIMEOUT_ENABLED
	curl_easy_setopt(Curl, CURLOPT_CONNECTTIMEOUT, UE_HTTPDDC_HTTP_REQUEST_TIMEOUT_SECONDS);
#endif
	curl_easy_setopt(Curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(Curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(Curl, CURLOPT_DNS_CACHE_TIMEOUT, 300L); // Don't re-resolve every minute
	curl_easy_setopt(Curl, CURLOPT_SHARE, SharedData ? SharedData->GetCurlShare() : nullptr);
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
	curl_easy_setopt(Curl, CURLOPT_SSL_CTX_FUNCTION, &FHttpRequest::StaticSSLCTXFn);
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
	curl_easy_setopt(Curl, CURLOPT_PRIVATE, this);
	curl_easy_setopt(Curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);
}

void FHttpRequest::PrepareToRetry()
{
	ResponseHeader.Reset();
	ResponseBuffer.Reset();
	ResponseCode = 0;
	BytesSent = 0;
	BytesReceived = 0;
	CurlResult = CURL_LAST;
	++Attempts;
}

FHttpRequest::Result FHttpRequest::PerformBlockingDownload(const TCHAR* Uri, TArray<uint8>* Buffer, TConstArrayView<long> ExpectedErrorCodes)
{
	curl_easy_setopt(Curl, CURLOPT_HTTPGET, 1L);
	WriteDataBufferPtr = Buffer;

	return PerformBlocking(Uri, Get, 0u, ExpectedErrorCodes);
}

void FHttpRequest::EnqueueAsyncDownload(UE::DerivedData::IRequestOwner& Owner, FHttpRequestPool* Pool, const TCHAR* Uri, FOnHttpRequestComplete&& OnComplete, TConstArrayView<long> ExpectedErrorCodes)
{
	curl_easy_setopt(Curl, CURLOPT_HTTPGET, 1L);

	return EnqueueAsync(Owner, Pool, Uri, Get, 0u, MoveTemp(OnComplete), ExpectedErrorCodes);
}

void FHttpRequest::SetHeader(const TCHAR* Header, const TCHAR* Value)
{
	check(CurlResult == CURL_LAST); // Cannot set header after request is sent
	Headers.Add(FString::Printf(TEXT("%s: %s"), Header, Value));
}

bool FHttpRequest::GetHeader(const ANSICHAR* Header, FString& OutValue) const
{
	check(CurlResult != CURL_LAST);  // Cannot query headers before request is sent

	const ANSICHAR* HeadersBuffer = (const ANSICHAR*) ResponseHeader.GetData();
	size_t HeaderLen = strlen(Header);

	// Find the header key in the (ANSI) response buffer. If not found we can exist immediately
	if (const ANSICHAR* Found = FCStringAnsi::Stristr(HeadersBuffer, Header))
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

TSharedPtr<FJsonObject> FHttpRequest::GetResponseAsJsonObject() const
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

TArray<TSharedPtr<FJsonValue>> FHttpRequest::GetResponseAsJsonArray() const
{
	FString Response = GetAnsiBufferAsString(ResponseBuffer);

	TArray<TSharedPtr<FJsonValue>> JsonArray;
	TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(Response);
	FJsonSerializer::Deserialize(JsonReader, JsonArray);
	return JsonArray;
}

bool FHttpRequest::AllowAsync()
{
	if (!FGenericPlatformProcess::SupportsMultithreading() || !bHttpEnableAsync)
	{
		return false;
	}

	return true;
}

void FHttpRequest::CompleteAsync(CURLcode Result)
{
	CurlResult = Result;

	// Get response code
	curl_easy_getinfo(Curl, CURLINFO_RESPONSE_CODE, &ResponseCode);

	LogResult(Result, *AsyncData->Uri, AsyncData->Verb, AsyncData->ExpectedErrorCodes);

	ECompletionBehavior Behavior;
	{
		UE::DerivedData::FRequestBarrier Barrier(*AsyncData->Owner, UE::DerivedData::ERequestBarrierFlags::Priority);
		FHttpRequest::Result HttpResult;
		switch (CurlResult)
		{
		case CURLE_OK:
			HttpResult = Success;
			break;
		case CURLE_OPERATION_TIMEDOUT:
			HttpResult = FailedTimeout;
			break;
		default:
			HttpResult = Failed;
			break;
		}
		Behavior = AsyncData->OnComplete(HttpResult, this);
	}

	if (Behavior == ECompletionBehavior::Retry)
	{
		PrepareToRetry();
		SharedData->AddRequest(Curl);
	}
	else
	{
		// Clean up
		curl_slist_free_all(AsyncData->CurlHeaders);

		AsyncData->Owner->End(AsyncData, [this, Behavior]
			{
				AsyncData->Event.Trigger();
				FHttpRequestPool* Pool = AsyncData->Pool;
				AsyncData = nullptr;
				if (Pool)
				{
					Pool->ReleaseRequestToPool(this);
				}
			});
	}
}

curl_slist* FHttpRequest::PrepareToIssueRequest(const TCHAR* Uri, uint64 ContentLength)
{
	static const char* CommonHeaders[] = {
		"User-Agent: Unreal Engine",
		nullptr
	};

	// Setup request options
	FString Url = FString::Printf(TEXT("%s/%s"), *EffectiveDomain, Uri);
	curl_easy_setopt(Curl, CURLOPT_URL, TCHAR_TO_ANSI(*Url));

	// Setup response header buffer. If caller has not setup a response data buffer, use internal.
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
	return CurlHeaders;
}

FHttpRequest::Result FHttpRequest::PerformBlocking(const TCHAR* Uri, RequestVerb Verb, uint64 ContentLength, TConstArrayView<long> ExpectedErrorCodes)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(HttpDDC_CurlPerform);

	// Build headers list
	curl_slist* CurlHeaders = PrepareToIssueRequest(Uri, ContentLength);

	// Shots fired!
	CurlResult = curl_easy_perform(Curl);

	// Get response code
	curl_easy_getinfo(Curl, CURLINFO_RESPONSE_CODE, &ResponseCode);

	LogResult(CurlResult, Uri, Verb, ExpectedErrorCodes);

	// Clean up
	curl_slist_free_all(CurlHeaders);

	return CurlResult == CURLE_OK ? Success : Failed;
}

void FHttpRequest::EnqueueAsync(UE::DerivedData::IRequestOwner& Owner, FHttpRequestPool* Pool, const TCHAR* Uri, RequestVerb Verb, uint64 ContentLength, FOnHttpRequestComplete&& OnComplete, TConstArrayView<long> ExpectedErrorCodes)
{
	if (!AllowAsync())
	{
		while (OnComplete(PerformBlocking(Uri, Verb, ContentLength, ExpectedErrorCodes), this) == ECompletionBehavior::Retry)
		{
			PrepareToRetry();
		}
		if (Pool)
		{
			Pool->ReleaseRequestToPool(this);
		}
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(HttpDDC_CurlEnqueueAsync);
	AsyncData = new FAsyncRequestData;
	AsyncData->Owner = &Owner;
	AsyncData->Pool = Pool;
	AsyncData->CurlHeaders = PrepareToIssueRequest(Uri, ContentLength);
	AsyncData->Uri = Uri;
	AsyncData->Verb = Verb;
	AsyncData->ExpectedErrorCodes = ExpectedErrorCodes;
	AsyncData->OnComplete = MoveTemp(OnComplete);
	AsyncData->Owner->Begin(AsyncData);

	SharedData->AddRequest(Curl);
}

void FHttpRequest::LogResult(CURLcode Result, const TCHAR* Uri, RequestVerb Verb, TConstArrayView<long> ExpectedErrorCodes) const
{
	if (Result == CURLE_OK)
	{
		bool bSuccess = false;
		const TCHAR* VerbStr = nullptr;
		FString AdditionalInfo;

		switch (Verb)
		{
		case Head:
			bSuccess = (ExpectedErrorCodes.Contains(ResponseCode) || IsSuccessResponse(ResponseCode));
			VerbStr = TEXT("querying");
			break;
		case Get:
			bSuccess = (ExpectedErrorCodes.Contains(ResponseCode) || IsSuccessResponse(ResponseCode));
			VerbStr = TEXT("fetching");
			AdditionalInfo = FString::Printf(TEXT("Received: %d bytes."), BytesReceived);
			break;
		case Put:
		case PutCompactBinary:
		case PutCompressedBlob:
			bSuccess = (ExpectedErrorCodes.Contains(ResponseCode) || IsSuccessResponse(ResponseCode));
			VerbStr = TEXT("updating");
			AdditionalInfo = FString::Printf(TEXT("Sent: %d bytes."), BytesSent);
			break;
		case Post:
		case PostCompactBinary:
		case PostJson:
			bSuccess = (ExpectedErrorCodes.Contains(ResponseCode) || IsSuccessResponse(ResponseCode));
			VerbStr = TEXT("posting");
			break;
		case Delete:
			bSuccess = (ExpectedErrorCodes.Contains(ResponseCode) || IsSuccessResponse(ResponseCode));
			VerbStr = TEXT("deleting");
			break;
		}

		if (bSuccess)
		{
			UE_LOG(
				LogDerivedDataCache, 
				Verbose, 
				TEXT("%s: Finished %s HTTP cache entry (response %d) from %s. %s"), 
				*GetName(),
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
			Response.ReplaceCharInline(TEXT('\n'), TEXT(' '));
			Response.ReplaceCharInline(TEXT('\r'), TEXT(' '));
			// Dont log access denied as error, since tokens can expire mid session
			if (ResponseCode == 401)
			{
				UE_LOG(
					LogDerivedDataCache,
					Verbose,
					TEXT("%s: Failed %s HTTP cache entry (response %d) from %s. Response: %s"),
					*GetName(),
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
					TEXT("%s: Failed %s HTTP cache entry (response %d) from %s. Response: %s"),
					*GetName(),
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
			TEXT("%s: Error while connecting to %s: %s"), 
			*GetName(),
			*EffectiveDomain,
			ANSI_TO_TCHAR(curl_easy_strerror(Result))
		);
	}
}

#if WITH_SSL
int FHttpRequest::SslCertVerify(int PreverifyOk, X509_STORE_CTX* Context)
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

CURLcode FHttpRequest::StaticSSLCTXFn(CURL * curl, void * sslctx, void * parm)
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

size_t FHttpRequest::StaticDebugCallback(CURL * Handle, curl_infotype DebugInfoType, char * DebugInfo, size_t DebugInfoSize, void* UserData)
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
		UE_LOG(LogDerivedDataCache, VeryVerbose, TEXT("%s: %p: '%s'"), *Request->GetName(), Request, *DebugText);
	}
	break;

	case CURLINFO_HEADER_IN:
		UE_LOG(LogDerivedDataCache, VeryVerbose, TEXT("%s: %p: Received header (%d bytes)"), *Request->GetName(), Request, DebugInfoSize);
		break;

	case CURLINFO_DATA_IN:
		UE_LOG(LogDerivedDataCache, VeryVerbose, TEXT("%s: %p: Received data (%d bytes)"), *Request->GetName(), Request, DebugInfoSize);
		break;

	case CURLINFO_DATA_OUT:
		UE_LOG(LogDerivedDataCache, VeryVerbose, TEXT("%s: %p: Sent data (%d bytes)"), *Request->GetName(), Request, DebugInfoSize);
		break;

	case CURLINFO_SSL_DATA_IN:
		UE_LOG(LogDerivedDataCache, VeryVerbose, TEXT("%s: %p: Received SSL data (%d bytes)"), *Request->GetName(), Request, DebugInfoSize);
		break;

	case CURLINFO_SSL_DATA_OUT:
		UE_LOG(LogDerivedDataCache, VeryVerbose, TEXT("%s: %p: Sent SSL data (%d bytes)"), *Request->GetName(), Request, DebugInfoSize);
		break;
	}

	return 0;
}

size_t FHttpRequest::StaticReadFn(void* Ptr, size_t SizeInBlocks, size_t BlockSizeInBytes, void* UserData)
{
	FHttpRequest* Request = static_cast<FHttpRequest*>(UserData);
	const size_t MaxReadSize = SizeInBlocks * BlockSizeInBytes;
	const FMemoryView SourceView = Request->ReadDataView.Mid(Request->BytesSent, MaxReadSize);
	MakeMemoryView(Ptr, MaxReadSize).CopyFrom(SourceView);
	Request->BytesSent += SourceView.GetSize();
	return SourceView.GetSize();
}

size_t FHttpRequest::StaticWriteHeaderFn(void* Ptr, size_t SizeInBlocks, size_t BlockSizeInBytes, void* UserData)
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

size_t FHttpRequest::StaticWriteBodyFn(void* Ptr, size_t SizeInBlocks, size_t BlockSizeInBytes, void* UserData)
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

size_t FHttpRequest::StaticSeekFn(void* UserData, curl_off_t Offset, int Origin)
{
	FHttpRequest* Request = static_cast<FHttpRequest*>(UserData);
	size_t NewPosition = 0;

	switch (Origin)
	{
	case SEEK_SET: NewPosition = Offset; break;
	case SEEK_CUR: NewPosition = Request->BytesSent + Offset; break;
	case SEEK_END: NewPosition = Request->ReadDataView.GetSize() + Offset; break;
	}

	// Make sure we don't seek outside of the buffer
	if (NewPosition < 0 || NewPosition >= Request->ReadDataView.GetSize())
	{
		return CURL_SEEKFUNC_FAIL;
	}

	// Update the used offset
	Request->BytesSent = NewPosition;
	return CURL_SEEKFUNC_OK;
}

} // UE

#endif // WITH_HTTP_CLIENT

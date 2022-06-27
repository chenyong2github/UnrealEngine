// Copyright Epic Games, Inc. All Rights Reserved.

#include "HttpClient.h"

#include "DerivedDataLegacyCacheStore.h"
#include "DerivedDataRequest.h"
#include "DerivedDataRequestOwner.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProperties.h"
#include "Logging/LogMacros.h"
#include "Misc/App.h"
#include "Misc/AsciiSet.h"
#include "Misc/EngineVersion.h"
#include "Serialization/CompactBinary.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "String/Find.h"

#if WITH_SSL
#include "Ssl.h"
#include <openssl/ssl.h>
#endif

#if !defined(CURL_NO_OLDIES)
	#define CURL_NO_OLDIES
#endif

#if PLATFORM_MICROSOFT
#include "Microsoft/AllowMicrosoftPlatformTypes.h"
#endif

#if defined(PLATFORM_CURL_INCLUDE)
#include PLATFORM_CURL_INCLUDE
#else
#include "curl/curl.h"
#endif // defined(PLATFORM_CURL_INCLUDE)

#if PLATFORM_MICROSOFT
#include "Microsoft/HideMicrosoftPlatformTypes.h"
#endif

namespace UE
{

static bool bHttpEnableAsync = true;
static FAutoConsoleVariableRef CVarHttpEnableAsync(
	TEXT("DDC.Http.EnableAsync"),
	bHttpEnableAsync,
	TEXT("If true, asynchronous operations are permitted, otherwise all operations are forced to be synchronous."),
	ECVF_Default);

namespace Http::Private
{

static constexpr bool RequestDebugEnabled = false;
static constexpr bool RequestTimeoutEnabled = true;
static constexpr long RequestTimeoutSeconds = 30;
static constexpr size_t RequestMaxBufferReserve = 100 * 1024 * 1024; // 100 MiB

struct FCurlStringListDeleter
{
	void operator()(curl_slist* List) const
	{
		if (List)
		{
			curl_slist_free_all(List);
		}
	}
};

struct FAsyncRequestData final : public DerivedData::FRequestBase
{
	DerivedData::IRequestOwner* Owner = nullptr;
	FHttpRequestPool* Pool = nullptr;
	FCurlStringList CurlHeaders;
	FString Uri;
	FHttpRequest::ERequestVerb Verb;
	TArray<long, TInlineAllocator<4>> ExpectedErrorCodes;
	FHttpRequest::FOnHttpRequestComplete OnComplete;
	FLazyEvent Event {EEventMode::ManualReset};

	void Reset()
	{
		CurlHeaders.Reset();
		Uri.Empty();
		ExpectedErrorCodes.Empty();
	}

	void SetPriority(DerivedData::EPriority Priority) final {}

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

struct FHttpRequestStatics
{
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

	static FHttpRequest::FResultCode StaticSSLCTXFn(CURL* Handle, void* sslctx, void* parm)
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

	static size_t StaticDebugCallback(CURL* Handle, curl_infotype DebugInfoType, char* DebugInfo, size_t DebugInfoSize, void* UserData)
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

			FString DebugText(CalculatedSize, static_cast<const ANSICHAR*>(DebugInfo));
			DebugText.ReplaceInline(TEXT("\n"), TEXT(""), ESearchCase::CaseSensitive);
			DebugText.ReplaceInline(TEXT("\r"), TEXT(""), ESearchCase::CaseSensitive);
			UE_LOG(LogDerivedDataCache, VeryVerbose, TEXT("%s: %p: '%s'"), *Request->GetName(), Request, *DebugText);
			break;
		}

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

	static size_t StaticReadFn(void* Ptr, size_t SizeInBlocks, size_t BlockSizeInBytes, void* UserData)
	{
		FHttpRequest* Request = static_cast<FHttpRequest*>(UserData);
		const size_t Offset = Request->BytesSent;
		const size_t ReadSize = FMath::Min((size_t)Request->ReadCompositeBuffer.GetSize() - Offset, SizeInBlocks * BlockSizeInBytes);
		Request->ReadCompositeBuffer.CopyTo(MakeMemoryView(Ptr, ReadSize), Request->BytesSent);
		Request->BytesSent += ReadSize;
		return ReadSize;
	}

	static size_t StaticWriteHeaderFn(void* Ptr, size_t SizeInBlocks, size_t BlockSizeInBytes, void* UserData)
	{
		FHttpRequest* Request = static_cast<FHttpRequest*>(UserData);
		const size_t WriteSize = SizeInBlocks * BlockSizeInBytes;
		TArray64<uint8>* WriteHeaderBufferPtr = Request->WriteHeaderBufferPtr;
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
					if (ContentLength > 0u && ContentLength < Http::Private::RequestMaxBufferReserve)
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
		case SEEK_END: NewPosition = Request->ReadCompositeBuffer.GetSize() + Offset; break;
		}

		// Make sure we don't seek outside of the buffer
		if (NewPosition < 0 || NewPosition >= Request->ReadCompositeBuffer.GetSize())
		{
			return CURL_SEEKFUNC_FAIL;
		}

		// Update the used offset
		Request->BytesSent = NewPosition;
		return CURL_SEEKFUNC_OK;
	}
};

} // Http::Private

FHttpRequest::FHttpRequest(
	const FStringView InDomain,
	const FStringView InEffectiveDomain,
	const FHttpAccessToken* const InAuthorizationToken,
	FHttpSharedData* const InSharedData,
	const bool bInLogErrors)
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
	curl_easy_cleanup(static_cast<CURL*>(Curl));
	check(!AsyncData);
}

void FHttpRequest::Reset()
{
	Headers.Reset();
	ResponseHeader.Reset();
	ResponseBuffer.Reset();
	ResponseCode = 0;
	ReadCompositeBuffer.Reset();
	WriteDataBufferPtr = nullptr;
	WriteHeaderBufferPtr = nullptr;
	BytesSent = 0;
	BytesReceived = 0;
	Attempts = 0;
	ResultCode = CURL_LAST;

	CURL* const LocalCurl = static_cast<CURL*>(Curl);
	curl_easy_reset(LocalCurl);
	check(!AsyncData);

	// Options that are always set for all connections.
	if constexpr (Http::Private::RequestTimeoutEnabled)
	{
		curl_easy_setopt(LocalCurl, CURLOPT_CONNECTTIMEOUT, Http::Private::RequestTimeoutSeconds);
	}
	curl_easy_setopt(LocalCurl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(LocalCurl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(LocalCurl, CURLOPT_DNS_CACHE_TIMEOUT, 300L); // Don't re-resolve every minute
	curl_easy_setopt(LocalCurl, CURLOPT_SHARE, SharedData ? SharedData->GetCurlShare() : nullptr);
	// Response functions
	curl_easy_setopt(LocalCurl, CURLOPT_HEADERDATA, this);
	curl_easy_setopt(LocalCurl, CURLOPT_HEADERFUNCTION, Http::Private::FHttpRequestStatics::StaticWriteHeaderFn);
	curl_easy_setopt(LocalCurl, CURLOPT_WRITEDATA, this);
	curl_easy_setopt(LocalCurl, CURLOPT_WRITEFUNCTION, Http::Private::FHttpRequestStatics::StaticWriteBodyFn);
	#if WITH_SSL
	// SSL options
	curl_easy_setopt(LocalCurl, CURLOPT_USE_SSL, CURLUSESSL_ALL);
	curl_easy_setopt(LocalCurl, CURLOPT_SSL_VERIFYPEER, 1);
	curl_easy_setopt(LocalCurl, CURLOPT_SSL_VERIFYHOST, 1);
	curl_easy_setopt(LocalCurl, CURLOPT_SSLCERTTYPE, "PEM");
	// SSL certification verification
	curl_easy_setopt(LocalCurl, CURLOPT_CAINFO, nullptr);
	curl_easy_setopt(LocalCurl, CURLOPT_SSL_CTX_FUNCTION, Http::Private::FHttpRequestStatics::StaticSSLCTXFn);
	curl_easy_setopt(LocalCurl, CURLOPT_SSL_CTX_DATA, this);
	#endif //#if WITH_SSL
	// Rewind method, handle special error case where request need to rewind data stream
	curl_easy_setopt(LocalCurl, CURLOPT_SEEKFUNCTION, Http::Private::FHttpRequestStatics::StaticSeekFn);
	curl_easy_setopt(LocalCurl, CURLOPT_SEEKDATA, this);
	// Set minimum speed behavior to allow operations to abort if the transfer speed is poor for the given duration (1kbps over a 30 second span)
	curl_easy_setopt(LocalCurl, CURLOPT_LOW_SPEED_TIME, 30L);
	curl_easy_setopt(LocalCurl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
	// Debug hooks
	if constexpr(Http::Private::RequestDebugEnabled)
	{
		curl_easy_setopt(LocalCurl, CURLOPT_DEBUGDATA, this);
		curl_easy_setopt(LocalCurl, CURLOPT_DEBUGFUNCTION, Http::Private::FHttpRequestStatics::StaticDebugCallback);
		curl_easy_setopt(LocalCurl, CURLOPT_VERBOSE, 1L);
	}
	curl_easy_setopt(LocalCurl, CURLOPT_PRIVATE, this);
	curl_easy_setopt(LocalCurl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);
}

void FHttpRequest::PrepareToRetry()
{
	ResponseHeader.Reset();
	ResponseBuffer.Reset();
	ResponseCode = 0;
	BytesSent = 0;
	BytesReceived = 0;
	ResultCode = CURL_LAST;
	++Attempts;
}

FHttpRequest::EResult FHttpRequest::PerformBlockingDownload(
	const FStringView Uri,
	TArray64<uint8>* const Buffer,
	const EHttpContentType AcceptType,
	const TConstArrayView<long> ExpectedErrorCodes)
{
	curl_easy_setopt(static_cast<CURL*>(Curl), CURLOPT_HTTPGET, 1L);
	WriteDataBufferPtr = Buffer;

	AddContentTypeHeader(TEXTVIEW("Accept"), AcceptType);

	return PerformBlocking(Uri, ERequestVerb::Get, 0u, ExpectedErrorCodes);
}

void FHttpRequest::EnqueueAsyncDownload(
	DerivedData::IRequestOwner& Owner,
	FHttpRequestPool* const Pool,
	const FStringView Uri,
	FOnHttpRequestComplete&& OnComplete,
	const EHttpContentType AcceptType,
	const TConstArrayView<long> ExpectedErrorCodes)
{
	curl_easy_setopt(static_cast<CURL*>(Curl), CURLOPT_HTTPGET, 1L);

	return EnqueueAsync(Owner, Pool, Uri, ERequestVerb::Get, 0u, MoveTemp(OnComplete), ExpectedErrorCodes);
}

void FHttpRequest::AddHeader(const FStringView Header, const FStringView Value)
{
	check(ResultCode == CURL_LAST); // Cannot set header after request is sent

	Headers.Emplace(WriteToString<128>(Header, TEXTVIEW(": "), Value));
}

bool FHttpRequest::GetHeader(const FAnsiStringView Header, FString& OutValue) const
{
	check(ResultCode != CURL_LAST); // Cannot query headers before request is sent

	// Find the header key in the (UTF-8) response buffer. If not found we can exit immediately.
	const FUtf8StringView LocalHeaders((const UTF8CHAR*)ResponseHeader.GetData(), ResponseHeader.Num());
	if (const int32 HeaderIndex = String::FindFirst(LocalHeaders, Header, ESearchCase::IgnoreCase); HeaderIndex != INDEX_NONE)
	{
		const FUtf8StringView HeaderToEnd = LocalHeaders.RightChop(HeaderIndex);
		const int32 LineBreakIndex = String::FindFirstChar(HeaderToEnd, '\r');
		const int32 ValueIndex = Header.Len() + 2; // colon and space
		const FUtf8StringView Value = HeaderToEnd.Mid(ValueIndex, LineBreakIndex - ValueIndex);
		OutValue = FString(Value);
		return true;
	}
	return false;
}

TSharedPtr<FJsonObject> FHttpRequest::GetResponseAsJsonObject() const
{
	FString Response = GetAnsiBufferAsString(ResponseBuffer);

	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<> > JsonReader = TJsonReaderFactory<>::Create(Response);
	FJsonSerializer::Deserialize(JsonReader, JsonObject);
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

void FHttpRequest::CompleteAsync(FResultCode Result)
{
	ResultCode = Result;

	curl_easy_getinfo(static_cast<CURL*>(Curl), CURLINFO_RESPONSE_CODE, &ResponseCode);

	LogResult(Result, *AsyncData->Uri, AsyncData->Verb, AsyncData->ExpectedErrorCodes);

	ECompletionBehavior Behavior;
	{
		FHttpRequest::EResult HttpResult;
		switch (ResultCode)
		{
		case CURLE_OK:
			HttpResult = EResult::Success;
			break;
		case CURLE_OPERATION_TIMEDOUT:
			HttpResult = EResult::FailedTimeout;
			break;
		default:
			HttpResult = EResult::Failed;
			break;
		}
		DerivedData::FRequestBarrier Barrier(*AsyncData->Owner, DerivedData::ERequestBarrierFlags::Priority);
		Behavior = AsyncData->OnComplete(HttpResult, this);
	}

	if (Behavior == ECompletionBehavior::Retry && !AsyncData->Owner->IsCanceled())
	{
		PrepareToRetry();
		SharedData->AddRequest(Curl);
	}
	else
	{
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

void FHttpRequest::AddContentTypeHeader(FStringView Header, EHttpContentType Type)
{
	if (Type != EHttpContentType::UnspecifiedContentType)
	{
		AddHeader(Header, GetHttpMimeType(Type));
	}
}

static const char* GetSessionIdHeader()
{
	static FCbObjectId SessionId = FCbObjectId::NewObjectId();

	static const char* HeaderString = []
	{
		static TAnsiStringBuilder<64> SessionIdHeader;
		SessionIdHeader << "UE-Session: " << SessionId;
		return SessionIdHeader.GetData();
	}();

	return HeaderString;
}

static std::atomic<int> GRequestId = 1;

Http::Private::FCurlStringList FHttpRequest::PrepareToIssueRequest(FStringView Uri, ERequestVerb Verb, uint64 ContentLength)
{
	// Strip leading slashes because the separating slash is added below.
	Uri = FAsciiSet::TrimPrefixWith(Uri, "/");
	curl_easy_setopt(static_cast<CURL*>(Curl), CURLOPT_URL, *WriteToUtf8String<512>(EffectiveDomain, '/', Uri));

	// Set up response header buffer.
	WriteHeaderBufferPtr = &ResponseHeader;

	// Use internal response data buffer if not provided by the caller.
	if (WriteDataBufferPtr == nullptr)
	{
		WriteDataBufferPtr = &ResponseBuffer;
	}

	// Set up headers.
	curl_slist* CurlHeaders = nullptr;
	const auto AddHeader = [&CurlHeaders](const ANSICHAR* Header)
	{
		CurlHeaders = curl_slist_append(CurlHeaders, Header);
	};

	AddHeader(GetSessionIdHeader());
	AddHeader(*WriteToAnsiString<32>(ANSITEXTVIEW("UE-Request: "), GRequestId.fetch_add(1, std::memory_order_relaxed)));

	// User-Agent: UnrealEngine/X.Y.Z-<CL> (<Platform>; <Config> <TargetType>; <BranchName>) <AppName> (<ProjectName>)
	const FEngineVersion& Version = FEngineVersion::Current();
	TUtf8StringBuilder<256> UserAgent;
	UserAgent << "User-Agent: UnrealEngine/"
		<< Version.GetMajor() << '.' << Version.GetMinor() << '.' << Version.GetPatch() << '-' << Version.GetChangelist()
		<< " (" << FPlatformProperties::PlatformName()
		<< "; " << LexToString(FApp::GetBuildConfiguration()) << ' ' << LexToString(FApp::GetBuildTargetType())
		<< "; " << FApp::GetBranchName()
		<< ") " << FApp::GetName();
	if (FApp::HasProjectName())
	{
		UserAgent << " (" << FApp::GetProjectName() << ")";
	}
	AddHeader((const ANSICHAR*)*UserAgent);

	// Strip any Expect: 100-Continue header since this just introduces latency.
	AddHeader("Expect:");

	for (const FString& Header : Headers)
	{
		AddHeader(TCHAR_TO_ANSI(*Header));
	}

	if ((Verb != ERequestVerb::Get) && (Verb != ERequestVerb::Delete))
	{
		AddHeader(*WriteToAnsiString<32>(ANSITEXTVIEW("Content-Length: "), ContentLength));
	}

	if (AuthorizationToken)
	{
		AddHeader(*WriteToAnsiString<1024>(*AuthorizationToken));
	}

	curl_easy_setopt(static_cast<CURL*>(Curl), CURLOPT_HTTPHEADER, CurlHeaders);
	return Http::Private::FCurlStringList(CurlHeaders);
}

FHttpRequest::EResult FHttpRequest::PerformBlocking(
	const FStringView Uri,
	const ERequestVerb Verb,
	const uint64 ContentLength,
	const TConstArrayView<long> ExpectedErrorCodes)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(HttpDDC_CurlPerform);

	Http::Private::FCurlStringList CurlHeaders = PrepareToIssueRequest(Uri, Verb, ContentLength);

	ResultCode = curl_easy_perform(static_cast<CURL*>(Curl));

	curl_easy_getinfo(static_cast<CURL*>(Curl), CURLINFO_RESPONSE_CODE, &ResponseCode);

	LogResult(ResultCode, Uri, Verb, ExpectedErrorCodes);

	return ResultCode == CURLE_OK ? EResult::Success : EResult::Failed;
}

void FHttpRequest::EnqueueAsync(
	DerivedData::IRequestOwner& Owner,
	FHttpRequestPool* const Pool,
	const FStringView Uri,
	const ERequestVerb Verb,
	const uint64 ContentLength,
	FOnHttpRequestComplete&& OnComplete,
	const TConstArrayView<long> ExpectedErrorCodes)
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
	AsyncData = new Http::Private::FAsyncRequestData;
	AsyncData->Owner = &Owner;
	AsyncData->Pool = Pool;
	AsyncData->CurlHeaders = PrepareToIssueRequest(Uri, Verb, ContentLength);
	AsyncData->Uri = Uri;
	AsyncData->Verb = Verb;
	AsyncData->ExpectedErrorCodes = ExpectedErrorCodes;
	AsyncData->OnComplete = MoveTemp(OnComplete);
	AsyncData->Owner->Begin(AsyncData);

	SharedData->AddRequest(Curl);
}

void FHttpRequest::LogResult(
	const FResultCode Result,
	const FStringView Uri,
	const ERequestVerb Verb,
	const TConstArrayView<long> ExpectedErrorCodes) const
{
	if (Result == CURLE_OK)
	{
		const bool bSuccess = (IsSuccessResponse(ResponseCode) || ExpectedErrorCodes.Contains(ResponseCode));
		const TCHAR* VerbStr = nullptr;
		TStringBuilder<32> AdditionalInfo;

		switch (Verb)
		{
		case ERequestVerb::Head:
			VerbStr = TEXT("querying");
			break;
		case ERequestVerb::Get:
			VerbStr = TEXT("fetching");
			AdditionalInfo << TEXTVIEW("Received: ") << uint64(BytesReceived) << TEXTVIEW(" bytes.");
			break;
		case ERequestVerb::Put:
			VerbStr = TEXT("updating");
			AdditionalInfo << TEXTVIEW("Sent: ") << uint64(BytesSent) << TEXTVIEW(" bytes.");
			break;
		case ERequestVerb::Post:
			VerbStr = TEXT("posting");
			break;
		case ERequestVerb::Delete:
			VerbStr = TEXT("deleting");
			break;
		default:
			checkNoEntry();
			break;
		}

		if (bSuccess)
		{
			UE_LOG(LogDerivedDataCache, Verbose,
				TEXT("%s: Finished %s HTTP cache entry (response %d) from %.*s. %s"),
				*GetName(), VerbStr, ResponseCode, Uri.Len(), Uri.GetData(), *AdditionalInfo);
		}
		else if (bLogErrors)
		{
			// Print the response body if we got one, otherwise print header.
			FString Response = GetAnsiBufferAsString(ResponseBuffer.Num() > 0 ? ResponseBuffer : ResponseHeader);
			Response.ReplaceCharInline(TEXT('\n'), TEXT(' '));
			Response.ReplaceCharInline(TEXT('\r'), TEXT(' '));
			// Don't log access denied as error, since tokens can expire mid-session.
			if (ResponseCode == 401)
			{
				UE_LOG(LogDerivedDataCache, Verbose,
					TEXT("%s: Failed %s HTTP cache entry (response %d) from %.*s. Response: %s"),
					*GetName(), VerbStr, ResponseCode, Uri.Len(), Uri.GetData(), *Response);
			}
			else
			{
				UE_LOG(LogDerivedDataCache, Display,
					TEXT("%s: Failed %s HTTP cache entry (response %d) from %.*s. Response: %s"),
					*GetName(), VerbStr, ResponseCode, Uri.Len(), Uri.GetData(), *Response);
			}
		}
	}
	else if (bLogErrors)
	{
		UE_LOG(LogDerivedDataCache, Display,
			TEXT("%s: Error while connecting to %s: %s"),
			*GetName(), *EffectiveDomain, ANSI_TO_TCHAR(curl_easy_strerror(static_cast<CURLcode>(Result))));
	}
}

FHttpRequest::EResult FHttpRequest::PerformBlockingPut(
	const FStringView Uri,
	const FCompositeBuffer& Buffer,
	const EHttpContentType ContentType,
	const TConstArrayView<long> ExpectedErrorCodes)
{
	uint64 ContentLength = 0u;

	CURL* const LocalCurl = static_cast<CURL*>(Curl);
	curl_easy_setopt(LocalCurl, CURLOPT_UPLOAD, 1L);
	curl_easy_setopt(LocalCurl, CURLOPT_INFILESIZE, Buffer.GetSize());
	curl_easy_setopt(LocalCurl, CURLOPT_READDATA, this);
	curl_easy_setopt(LocalCurl, CURLOPT_READFUNCTION, Http::Private::FHttpRequestStatics::StaticReadFn);
	AddContentTypeHeader(TEXTVIEW("Content-Type"), ContentType);
	ContentLength = Buffer.GetSize();
	ReadCompositeBuffer = Buffer;

	return PerformBlocking(Uri, ERequestVerb::Put, ContentLength, ExpectedErrorCodes);
}

FHttpRequest::EResult FHttpRequest::PerformBlockingPost(
	const FStringView Uri,
	const FCompositeBuffer& Buffer,
	const EHttpContentType ContentType,
	const EHttpContentType AcceptType,
	const TConstArrayView<long> ExpectedErrorCodes)
{
	uint64 ContentLength = 0;

	CURL* const LocalCurl = static_cast<CURL*>(Curl);
	curl_easy_setopt(LocalCurl, CURLOPT_POST, 1L);
	curl_easy_setopt(LocalCurl, CURLOPT_POSTFIELDSIZE, Buffer.GetSize());
	curl_easy_setopt(LocalCurl, CURLOPT_READDATA, this);
	curl_easy_setopt(LocalCurl, CURLOPT_READFUNCTION, Http::Private::FHttpRequestStatics::StaticReadFn);
	AddContentTypeHeader(TEXTVIEW("Content-Type"), ContentType);
	AddContentTypeHeader(TEXTVIEW("Accept"), AcceptType);
	ContentLength = Buffer.GetSize();
	ReadCompositeBuffer = Buffer;

	return PerformBlocking(Uri, ERequestVerb::Post, ContentLength, ExpectedErrorCodes);
}

void FHttpRequest::EnqueueAsyncPut(
	DerivedData::IRequestOwner& Owner,
	FHttpRequestPool* const Pool,
	const FStringView Uri,
	const FCompositeBuffer& Buffer,
	FOnHttpRequestComplete&& OnComplete,
	const EHttpContentType ContentType,
	const TConstArrayView<long> ExpectedErrorCodes)
{
	uint64 ContentLength = 0;

	CURL* const LocalCurl = static_cast<CURL*>(Curl);
	curl_easy_setopt(LocalCurl, CURLOPT_UPLOAD, 1L);
	curl_easy_setopt(LocalCurl, CURLOPT_INFILESIZE, Buffer.GetSize());
	curl_easy_setopt(LocalCurl, CURLOPT_READDATA, this);
	curl_easy_setopt(LocalCurl, CURLOPT_READFUNCTION, Http::Private::FHttpRequestStatics::StaticReadFn);
	AddContentTypeHeader(TEXTVIEW("Content-Type"), ContentType);
	ReadCompositeBuffer = Buffer;
	ContentLength = Buffer.GetSize();

	return EnqueueAsync(Owner, Pool, Uri, ERequestVerb::Put, ContentLength, MoveTemp(OnComplete), ExpectedErrorCodes);
}

void FHttpRequest::EnqueueAsyncPost(
	DerivedData::IRequestOwner& Owner,
	FHttpRequestPool* const Pool,
	const FStringView Uri,
	const FCompositeBuffer& Buffer,
	FOnHttpRequestComplete&& OnComplete,
	const EHttpContentType ContentType,
	const EHttpContentType AcceptType,
	const TConstArrayView<long> ExpectedErrorCodes)
{
	uint64 ContentLength = 0;

	CURL* const LocalCurl = static_cast<CURL*>(Curl);
	curl_easy_setopt(LocalCurl, CURLOPT_POST, 1L);
	curl_easy_setopt(LocalCurl, CURLOPT_POSTFIELDSIZE, Buffer.GetSize());
	curl_easy_setopt(LocalCurl, CURLOPT_READDATA, this);
	curl_easy_setopt(LocalCurl, CURLOPT_READFUNCTION, Http::Private::FHttpRequestStatics::StaticReadFn);
	AddContentTypeHeader(TEXTVIEW("Content-Type"), ContentType);
	AddContentTypeHeader(TEXTVIEW("Accept"), AcceptType);
	ReadCompositeBuffer = Buffer;
	ContentLength = Buffer.GetSize();

	return EnqueueAsync(Owner, Pool, Uri, ERequestVerb::Post, ContentLength, MoveTemp(OnComplete), ExpectedErrorCodes);
}

FHttpRequest::EResult FHttpRequest::PerformBlockingHead(
	const FStringView Uri,
	const EHttpContentType AcceptType,
	const TConstArrayView<long> ExpectedErrorCodes)
{
	curl_easy_setopt(static_cast<CURL*>(Curl), CURLOPT_NOBODY, 1L);
	AddContentTypeHeader(TEXTVIEW("Accept"), AcceptType);
	return PerformBlocking(Uri, ERequestVerb::Head, 0, ExpectedErrorCodes);
}

FHttpRequest::EResult FHttpRequest::PerformBlockingDelete(
	const FStringView Uri,
	const TConstArrayView<long> ExpectedErrorCodes)
{
	curl_easy_setopt(static_cast<CURL*>(Curl), CURLOPT_CUSTOMREQUEST, "DELETE");
	return PerformBlocking(Uri, ERequestVerb::Delete, 0, ExpectedErrorCodes);
}

void FHttpRequest::EnqueueAsyncHead(
	DerivedData::IRequestOwner& Owner,
	FHttpRequestPool* const Pool,
	const FStringView Uri,
	FOnHttpRequestComplete&& OnComplete,
	const EHttpContentType AcceptType,
	const TConstArrayView<long> ExpectedErrorCodes)
{
	curl_easy_setopt(static_cast<CURL*>(Curl), CURLOPT_NOBODY, 1L);
	AddContentTypeHeader(TEXTVIEW("Accept"), AcceptType);
	return EnqueueAsync(Owner, Pool, Uri, ERequestVerb::Head, 0, MoveTemp(OnComplete), ExpectedErrorCodes);
}

void FHttpRequest::EnqueueAsyncDelete(
	DerivedData::IRequestOwner& Owner,
	FHttpRequestPool* const Pool,
	const FStringView Uri,
	FOnHttpRequestComplete&& OnComplete,
	const TConstArrayView<long> ExpectedErrorCodes)
{
	curl_easy_setopt(static_cast<CURL*>(Curl), CURLOPT_CUSTOMREQUEST, "DELETE");
	return EnqueueAsync(Owner, Pool, Uri, ERequestVerb::Delete, 0, MoveTemp(OnComplete), ExpectedErrorCodes);
}

} // UE

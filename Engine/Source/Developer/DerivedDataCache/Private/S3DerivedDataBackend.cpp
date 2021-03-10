// Copyright Epic Games, Inc. All Rights Reserved.

#include "S3DerivedDataBackend.h"

#if WITH_S3_DDC_BACKEND

#if PLATFORM_WINDOWS || PLATFORM_HOLOLENS
	#include "Windows/WindowsHWrapper.h"
	#include "Windows/AllowWindowsPlatformTypes.h"
#endif
#include "curl/curl.h"
#if PLATFORM_WINDOWS || PLATFORM_HOLOLENS
	#include "Windows/HideWindowsPlatformTypes.h"
#endif
#include "Ssl.h"
#include <openssl/ssl.h>
#include <openssl/sha.h>
#include <openssl/hmac.h>
#include "Misc/Base64.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Misc/FileHelper.h"
#include "Misc/FeedbackContext.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Async/ParallelFor.h"
#include "Serialization/MemoryReader.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "HAL/PlatformFile.h"
#include "HAL/PlatformFilemanager.h"
#include "HAL/Runnable.h"
#include "DesktopPlatformModule.h"
#include "Misc/ConfigCacheIni.h"

#define S3DDC_BACKEND_WAIT_INTERVAL 0.01f
#define S3DDC_HTTP_REQUEST_TIMEOUT_SECONDS 30L
#define S3DDC_HTTP_REQUEST_TIMOUT_ENABLED 1
#define S3DDC_REQUEST_POOL_SIZE 16
#define S3DDC_MAX_FAILED_LOGIN_ATTEMPTS 16
#define S3DDC_MAX_ATTEMPTS 4
#define S3DDC_MAX_BUFFER_RESERVE 104857600u

TRACE_DECLARE_INT_COUNTER(S3DDC_Exist, TEXT("S3DDC Exist"));
TRACE_DECLARE_INT_COUNTER(S3DDC_ExistHit, TEXT("S3DDC Exist Hit"));
TRACE_DECLARE_INT_COUNTER(S3DDC_Get, TEXT("S3DDC Get"));
TRACE_DECLARE_INT_COUNTER(S3DDC_GetHit, TEXT("S3DDC Get Hit"));
TRACE_DECLARE_INT_COUNTER(S3DDC_BytesRecieved, TEXT("S3DDC Bytes Recieved"));

FString BuildPathForCacheKey(const TCHAR* CacheKey);

class FStringAnsi
{
public:
	FStringAnsi()
	{
		Inner.Add(0);
	}

	FStringAnsi(const ANSICHAR* Text)
	{
		Inner.Append(Text, FCStringAnsi::Strlen(Text) + 1);
	}

	void Append(ANSICHAR Character)
	{
		Inner[Inner.Num() - 1] = Character;
		Inner.Add(0);
	}

	void Append(const FStringAnsi& Other)
	{
		Inner.RemoveAt(Inner.Num() - 1);
		Inner.Append(Other.Inner);
	}

	void Append(const ANSICHAR* Text)
	{
		Inner.RemoveAt(Inner.Num() - 1);
		Inner.Append(Text, FCStringAnsi::Strlen(Text) + 1);
	}

	void Append(const ANSICHAR* Start, const ANSICHAR* End)
	{
		Inner.RemoveAt(Inner.Num() - 1);
		Inner.Append(Start, End - Start);
		Inner.Add(0);
	}

	static FStringAnsi Printf(const ANSICHAR* Format, ...)
	{
		ANSICHAR Buffer[1024];
		GET_VARARGS_ANSI(Buffer, UE_ARRAY_COUNT(Buffer), UE_ARRAY_COUNT(Buffer) - 1, Format, Format);
		return Buffer;
	}

	FString ToWideString() const
	{
		return ANSI_TO_TCHAR(Inner.GetData());
	}

	const ANSICHAR* operator*() const
	{
		return Inner.GetData();
	}

	int32 Len() const
	{
		return Inner.Num() - 1;
	}

private:
	TArray<ANSICHAR> Inner;
};

struct FSHA256
{
	uint8 Digest[32];

	FStringAnsi ToString() const
	{
		ANSICHAR Buffer[65];
		for (int Idx = 0; Idx < 32; Idx++)
		{
			FCStringAnsi::Sprintf(Buffer + (Idx * 2), "%02x", Digest[Idx]);
		}
		return Buffer;
	}
};

FSHA256 Sha256(const uint8* Input, size_t InputLen)
{
	FSHA256 Output;
	SHA256(Input, InputLen, Output.Digest);
	return Output;
}

FSHA256 HmacSha256(const uint8* Input, size_t InputLen, const uint8* Key, size_t KeyLen)
{
	FSHA256 Output;
	unsigned int OutputLen = 0;
	HMAC(EVP_sha256(), Key, KeyLen, (const unsigned char*)Input, InputLen, Output.Digest, &OutputLen);
	return Output;
}

FSHA256 HmacSha256(const FStringAnsi& Input, const uint8* Key, size_t KeyLen)
{
	return HmacSha256((const uint8*)*Input, (size_t)Input.Len(), Key, KeyLen);
}

FSHA256 HmacSha256(const char* Input, const uint8* Key, size_t KeyLen)
{
	return HmacSha256((const uint8*)Input, (size_t)FCStringAnsi::Strlen(Input), Key, KeyLen);
}

bool IsSuccessfulHttpResponse(long ResponseCode)
{
	return (ResponseCode >= 200 && ResponseCode <= 299);
}

struct IRequestCallback
{
	virtual ~IRequestCallback() { }
	virtual bool Update(int NumBytes, int TotalBytes) = 0;
};

/**
 * Minimal HTTP request type wrapping CURL without the need for managers. This request
 * is written to allow reuse of request objects, in order to allow connections to be reused.
 *
 * CURL has a global library initialization (curl_global_init). We rely on this happening in 
 * the Online/HTTP library which is a dependency on this module.
 */
class FS3DerivedDataBackend::FRequest
{
public:
	FRequest(const ANSICHAR* InRegion, const ANSICHAR* InAccessKey, const ANSICHAR* InSecretKey)
		: Region(InRegion)
		, AccessKey(InAccessKey)
		, SecretKey(InSecretKey)
	{
		Curl = curl_easy_init();
	}

	~FRequest()
	{
		curl_easy_cleanup(Curl);
	}

	/**
	 * Performs the request, blocking until finished.
	 * @param Url HTTP URL to fetch
	 * @param Callback Object used to convey state to/from the operation
	 * @param Buffer Optional buffer to directly receive the result of the request. 
	 * If unset the response body will be stored in the request.
	 */
	long PerformBlocking(const ANSICHAR* Url, IRequestCallback* Callback, TArray<uint8>& OutResponseBody, FOutputDevice* Log)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(S3DDC_CurlPerform);

		// Find the host from the URL
		const ANSICHAR* ProtocolEnd = FCStringAnsi::Strchr(Url, ':');
		check(ProtocolEnd != nullptr && *(ProtocolEnd + 1) == '/' && *(ProtocolEnd + 2) == '/');

		const ANSICHAR* UrlHost = ProtocolEnd + 3;
		const ANSICHAR* UrlHostEnd = FCStringAnsi::Strchr(UrlHost, '/');
		check(UrlHostEnd != nullptr);

		FStringAnsi Host;
		Host.Append(UrlHost, UrlHostEnd);

		// Get the header strings
		FDateTime Timestamp = FDateTime::UtcNow();// FDateTime(2015, 9, 15, 12, 45, 0);
		FStringAnsi TimeString = FStringAnsi::Printf("%04d%02d%02dT%02d%02d%02dZ", Timestamp.GetYear(), Timestamp.GetMonth(), Timestamp.GetDay(), Timestamp.GetHour(), Timestamp.GetMinute(), Timestamp.GetSecond());

		// Payload string
		FStringAnsi EmptyPayloadSha256 = Sha256(nullptr, 0).ToString();

		// Create the headers
		curl_slist* CurlHeaders = nullptr;
		CurlHeaders = curl_slist_append(CurlHeaders, *FStringAnsi::Printf("Host: %s", *Host));
		CurlHeaders = curl_slist_append(CurlHeaders, *FStringAnsi::Printf("x-amz-content-sha256: %s", *EmptyPayloadSha256));
		CurlHeaders = curl_slist_append(CurlHeaders, *FStringAnsi::Printf("x-amz-date: %s", *TimeString));
		CurlHeaders = curl_slist_append(CurlHeaders, *GetAuthorizationHeader("GET", UrlHostEnd, "", CurlHeaders, *TimeString, *EmptyPayloadSha256));

		// Create the callback data
		FStringAnsi Domain;
		Domain.Append(Url, UrlHostEnd);

		FCallbackData CallbackData(Domain, OutResponseBody);

		// Setup the request
		curl_easy_reset(Curl);
		curl_easy_setopt(Curl, CURLOPT_FOLLOWLOCATION, 1L);
		curl_easy_setopt(Curl, CURLOPT_NOSIGNAL, 1L);
		curl_easy_setopt(Curl, CURLOPT_HTTPGET, 1L);
		curl_easy_setopt(Curl, CURLOPT_URL, Url);
		curl_easy_setopt(Curl, CURLOPT_ACCEPT_ENCODING, "gzip");
#if S3DDC_HTTP_REQUEST_TIMOUT_ENABLED
		curl_easy_setopt(Curl, CURLOPT_CONNECTTIMEOUT, S3DDC_HTTP_REQUEST_TIMEOUT_SECONDS);
#endif

		// Headers
		curl_easy_setopt(Curl, CURLOPT_HTTPHEADER, CurlHeaders);
		curl_easy_setopt(Curl, CURLOPT_HEADERDATA, CurlHeaders);

		// Progress
		curl_easy_setopt(Curl, CURLOPT_NOPROGRESS, 0);
		curl_easy_setopt(Curl, CURLOPT_XFERINFODATA, Callback);
		curl_easy_setopt(Curl, CURLOPT_XFERINFOFUNCTION, &FRequest::StaticStatusFn);

		// Response
		curl_easy_setopt(Curl, CURLOPT_HEADERDATA, &CallbackData);
		curl_easy_setopt(Curl, CURLOPT_HEADERFUNCTION, &FRequest::StaticWriteHeaderFn);
		curl_easy_setopt(Curl, CURLOPT_WRITEDATA, &CallbackData);
		curl_easy_setopt(Curl, CURLOPT_WRITEFUNCTION, StaticWriteBodyFn);

		// SSL options
		curl_easy_setopt(Curl, CURLOPT_USE_SSL, CURLUSESSL_ALL);
		curl_easy_setopt(Curl, CURLOPT_SSL_VERIFYPEER, 1);
		curl_easy_setopt(Curl, CURLOPT_SSL_VERIFYHOST, 1);
		curl_easy_setopt(Curl, CURLOPT_SSLCERTTYPE, "PEM");

		// SSL certification verification
		curl_easy_setopt(Curl, CURLOPT_CAINFO, nullptr);
		curl_easy_setopt(Curl, CURLOPT_SSL_CTX_FUNCTION, *sslctx_function);
		curl_easy_setopt(Curl, CURLOPT_SSL_CTX_DATA, &CallbackData);

		// Send the request
		CURLcode CurlResult = curl_easy_perform(Curl);

		// Free the headers object
		curl_slist_free_all(CurlHeaders);
		curl_easy_setopt(Curl, CURLOPT_HEADERDATA, nullptr);

		// Get the response code
		long ResponseCode = 0;
		if (CurlResult == CURLE_OK)
		{
			CurlResult = curl_easy_getinfo(Curl, CURLINFO_RESPONSE_CODE, &ResponseCode);
		}
		if (CurlResult != CURLE_OK)
		{
			if (CurlResult != CURLE_ABORTED_BY_CALLBACK)
			{
				Log->Logf(ELogVerbosity::Error, TEXT("Error while connecting to %s: %d (%s)"), ANSI_TO_TCHAR(Url), CurlResult, ANSI_TO_TCHAR(curl_easy_strerror(CurlResult)));
			}
			return 500;
		}

		// Print any diagnostic output
		if (!(ResponseCode >= 200 && ResponseCode <= 299))
		{
			Log->Logf(ELogVerbosity::Error, TEXT("Download failed for %s (response %d):\n%s\n%s"), ANSI_TO_TCHAR(Url), ResponseCode, *CallbackData.ResponseHeader.ToWideString(), ANSI_TO_TCHAR((const ANSICHAR*)OutResponseBody.GetData()));
		}
		return ResponseCode;
	}

private:
	struct FCallbackData
	{
		const FStringAnsi& Domain;
		FStringAnsi ResponseHeader;
		TArray<uint8>& ResponseBody;

		FCallbackData(const FStringAnsi& InDomain, TArray<uint8>& InResponseBody)
			: Domain(InDomain)
			, ResponseBody(InResponseBody)
		{
		}
	};

	CURL* Curl;
	FStringAnsi Region;
	FStringAnsi AccessKey;
	FStringAnsi SecretKey;

	FStringAnsi GetAuthorizationHeader(const ANSICHAR* Verb, const ANSICHAR* RelativeUrl, const ANSICHAR* QueryString, const curl_slist* Headers, const ANSICHAR* Timestamp, const ANSICHAR* Digest)
	{
		// Create the canonical list of headers
		FStringAnsi CanonicalHeaders;
		for (const curl_slist* Header = Headers; Header != nullptr; Header = Header->next)
		{
			const ANSICHAR* Colon = FCStringAnsi::Strchr(Header->data, ':');
			if (Colon != nullptr)
			{
				for (const ANSICHAR* Char = Header->data; Char != Colon; Char++)
				{
					CanonicalHeaders.Append(tolower(*Char));
				}
				CanonicalHeaders.Append(':');

				const ANSICHAR* Value = Colon + 1;
				while (*Value == ' ')
				{
					Value++;
				}
				for (; *Value != 0; Value++)
				{
					CanonicalHeaders.Append(*Value);
				}
				CanonicalHeaders.Append('\n');
			}
		}

		// Create the list of signed headers
		FStringAnsi SignedHeaders;
		for (const curl_slist* Header = Headers; Header != nullptr; Header = Header->next)
		{
			const ANSICHAR* Colon = FCStringAnsi::Strchr(Header->data, ':');
			if (Colon != nullptr)
			{
				if (SignedHeaders.Len() > 0)
				{
					SignedHeaders.Append(';');
				}
				for (const ANSICHAR* Char = Header->data; Char != Colon; Char++)
				{
					SignedHeaders.Append(tolower(*Char));
				}
			}
		}

		// Build the canonical request string
		FStringAnsi CanonicalRequest;
		CanonicalRequest.Append(Verb);
		CanonicalRequest.Append('\n');
		CanonicalRequest.Append(RelativeUrl);
		CanonicalRequest.Append('\n');
		CanonicalRequest.Append(QueryString);
		CanonicalRequest.Append('\n');
		CanonicalRequest.Append(CanonicalHeaders);
		CanonicalRequest.Append('\n');
		CanonicalRequest.Append(SignedHeaders);
		CanonicalRequest.Append('\n');
		CanonicalRequest.Append(Digest);

		// Get the date
		FStringAnsi DateString;
		for (int32 Idx = 0; Timestamp[Idx] != 0 && Timestamp[Idx] != 'T'; Idx++)
		{
			DateString.Append(Timestamp[Idx]);
		}

		// Generate the signature key
		FStringAnsi Key = FStringAnsi::Printf("AWS4%s", *SecretKey);

		FSHA256 DateHash = HmacSha256(DateString, (const uint8*)*Key, Key.Len());
		FSHA256 RegionHash = HmacSha256(Region, DateHash.Digest, sizeof(DateHash.Digest));
		FSHA256 ServiceHash = HmacSha256("s3", RegionHash.Digest, sizeof(RegionHash.Digest));
		FSHA256 SigningKeyHash = HmacSha256("aws4_request", ServiceHash.Digest, sizeof(ServiceHash.Digest));

		// Calculate the signature
		FStringAnsi DateRequest = FStringAnsi::Printf("%s/%s/s3/aws4_request", *DateString, *Region);
		FStringAnsi CanonicalRequestSha256 = Sha256((const uint8*)*CanonicalRequest, CanonicalRequest.Len()).ToString();
		FStringAnsi StringToSign = FStringAnsi::Printf("AWS4-HMAC-SHA256\n%s\n%s\n%s", Timestamp, *DateRequest, *CanonicalRequestSha256);
		FStringAnsi Signature = HmacSha256(*StringToSign, SigningKeyHash.Digest, sizeof(SigningKeyHash.Digest)).ToString();

		// Format the final header
		return FStringAnsi::Printf("Authorization: AWS4-HMAC-SHA256 Credential=%s/%s, SignedHeaders=%s, Signature=%s", *AccessKey, *DateRequest, *SignedHeaders, *Signature);
	}

	/**
	 * Returns the response buffer as a string. Note that is the request is performed
	 * with an external buffer as target buffer this string will be empty.
	 */
	static FString GetResponseAsString(const TArray<uint8>& Buffer)
	{
		FUTF8ToTCHAR TCHARData(reinterpret_cast<const ANSICHAR*>(Buffer.GetData()), Buffer.Num());
		return FString(TCHARData.Length(), TCHARData.Get());
	}

	static int StaticStatusFn(void* Ptr, curl_off_t TotalDownloadSize, curl_off_t CurrentDownloadSize, curl_off_t TotalUploadSize, curl_off_t CurrentUploadSize)
	{
		IRequestCallback* Callback = (IRequestCallback*)Ptr;
		if (Callback != nullptr)
		{
			return Callback->Update(CurrentDownloadSize, TotalDownloadSize)? 0 : 1;
		}
		return 0;
	}

	static size_t StaticWriteHeaderFn(void* Ptr, size_t SizeInBlocks, size_t BlockSizeInBytes, void* UserData)
	{
		const size_t WriteSize = SizeInBlocks * BlockSizeInBytes;
		if (WriteSize > 0)
		{
			FCallbackData* CallbackData = static_cast<FCallbackData*>(UserData);
			CallbackData->ResponseHeader.Append((const ANSICHAR*)Ptr, (const ANSICHAR*)Ptr + WriteSize);
			return WriteSize;
		}
		return 0;
	}

	static size_t StaticWriteBodyFn(void* Ptr, size_t SizeInBlocks, size_t BlockSizeInBytes, void* UserData)
	{
		const size_t WriteSize = SizeInBlocks * BlockSizeInBytes;
		if (WriteSize > 0)
		{
			FCallbackData* CallbackData = static_cast<FCallbackData*>(UserData);

			// If this is the first part of the body being received, try to reserve 
			// memory if content length is defined in the header.
			if (CallbackData->ResponseBody.Num() == 0)
			{
				static const ANSICHAR Prefix[] = "Content-Length: ";
				static const size_t PrefixLen = UE_ARRAY_COUNT(Prefix) - 1;

				for(const ANSICHAR* Header = *CallbackData->ResponseHeader;;Header++)
				{
					// Check this header
					if (FCStringAnsi::Strnicmp(Header, Prefix, PrefixLen) == 0)
					{
						size_t ContentLength = (size_t)atol(Header + PrefixLen);
						if (ContentLength > 0u && ContentLength < S3DDC_MAX_BUFFER_RESERVE)
						{
							CallbackData->ResponseBody.Reserve(ContentLength);
						}
						break;
					}

					// Move to the next string
					Header = FCStringAnsi::Strchr(Header, '\n');
					if (Header == nullptr)
					{
						break;
					}
				}
			}

			// Write to the target buffer
			CallbackData->ResponseBody.Append((const uint8*)Ptr, WriteSize);
			return WriteSize;
		}
		return 0;
	}

	static int SslCertVerify(int PreverifyOk, X509_STORE_CTX* Context)
	{
		if (PreverifyOk == 1)
		{
			SSL* Handle = static_cast<SSL*>(X509_STORE_CTX_get_ex_data(Context, SSL_get_ex_data_X509_STORE_CTX_idx()));
			check(Handle);

			SSL_CTX* SslContext = SSL_get_SSL_CTX(Handle);
			check(SslContext);

			FCallbackData* CallbackData = reinterpret_cast<FCallbackData*>(SSL_CTX_get_app_data(SslContext));
			check(CallbackData);

			if (!FSslModule::Get().GetCertificateManager().VerifySslCertificates(Context, *CallbackData->Domain))
			{
				PreverifyOk = 0;
			}
		}

		return PreverifyOk;
	}

	static CURLcode sslctx_function(CURL* curl, void* sslctx, void* parm)
	{
		SSL_CTX* Context = static_cast<SSL_CTX*>(sslctx);
		const ISslCertificateManager& CertificateManager = FSslModule::Get().GetCertificateManager();

		CertificateManager.AddCertificatesToSslContext(Context);
		SSL_CTX_set_verify(Context, SSL_CTX_get_verify_mode(Context), SslCertVerify);
		SSL_CTX_set_app_data(Context, parm);

		/* all set to go */
		return CURLE_OK;
	}
};

//----------------------------------------------------------------------------------------------------------
// FS3DerivedDataBackend::FRequestPool
//----------------------------------------------------------------------------------------------------------

class FS3DerivedDataBackend::FRequestPool
{
public:
	FRequestPool(const ANSICHAR* Region, const ANSICHAR* AccessKey, const ANSICHAR* SecretKey)
	{
		Pool.SetNum(S3DDC_REQUEST_POOL_SIZE);
		for (uint8 i = 0; i < Pool.Num(); ++i)
		{
			Pool[i].Usage = 0u;
			Pool[i].Request = new FRequest(Region, AccessKey, SecretKey);
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

	long Download(const TCHAR* Url, IRequestCallback* Callback, TArray<uint8>& OutData, FOutputDevice* Log)
	{
		FRequest* Request = WaitForFreeRequest();
		long ResponseCode = Request->PerformBlocking(TCHAR_TO_ANSI(Url), Callback, OutData, Log);
		ReleaseRequestToPool(Request);
		return ResponseCode;
	}

private:
	struct FEntry
	{
		TAtomic<uint8> Usage;
		FRequest* Request;
	};

	TArray<FEntry> Pool;

	FRequest* WaitForFreeRequest()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(S3DDC_WaitForConnPool);
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
			FPlatformProcess::Sleep(S3DDC_BACKEND_WAIT_INTERVAL);
		}
	}

	void ReleaseRequestToPool(FRequest* Request)
	{
		for (uint8 i = 0; i < Pool.Num(); ++i)
		{
			if (Pool[i].Request == Request)
			{
				uint8 Expected = 1u;
				Pool[i].Usage.CompareExchange(Expected, 0u);
				return;
			}
		}
		check(false);
	}
};

//----------------------------------------------------------------------------------------------------------
// FS3DerivedDataBackend::FBundleEntry
//----------------------------------------------------------------------------------------------------------

struct FS3DerivedDataBackend::FBundleEntry
{
	int64 Offset;
	int32 Length;
};

//----------------------------------------------------------------------------------------------------------
// FS3DerivedDataBackend::FBundle
//----------------------------------------------------------------------------------------------------------

struct FS3DerivedDataBackend::FBundle
{
	FString Name;
	FString ObjectKey;
	FString LocalFile;
	int32 CompressedLength;
	int32 UncompressedLength;
	TMap<FSHAHash, FBundleEntry> Entries;
};

//----------------------------------------------------------------------------------------------------------
// FS3DerivedDataBackend::FBundleDownloadInfo
//----------------------------------------------------------------------------------------------------------

struct FS3DerivedDataBackend::FBundleDownload : IRequestCallback
{
	FCriticalSection& CriticalSection;
	FBundle& Bundle;
	FString BundleUrl;
	FRequestPool& RequestPool;
	FFeedbackContext* Context;
	FGraphEventRef Event;
	int DownloadedBytes;

	FBundleDownload(FCriticalSection& InCriticalSection, FBundle& InBundle, FString InBundleUrl, FRequestPool& InRequestPool, FFeedbackContext* InContext)
		: CriticalSection(InCriticalSection)
		, Bundle(InBundle)
		, BundleUrl(InBundleUrl)
		, RequestPool(InRequestPool)
		, Context(InContext)
		, DownloadedBytes(0)
	{
	}

	void Execute()
	{
		if (Context->ReceivedUserCancel())
		{
			return;
		}

		Context->Logf(TEXT("Downloading %s (%d bytes)"), *BundleUrl, Bundle.CompressedLength);

		TArray<uint8> CompressedData;
		CompressedData.Reserve(Bundle.CompressedLength);

		long ResponseCode = RequestPool.Download(*BundleUrl, this, CompressedData, Context);
		if(!IsSuccessfulHttpResponse(ResponseCode))
		{
			if (!Context->ReceivedUserCancel())
			{
				Context->Logf(ELogVerbosity::Warning, TEXT("Unable to download bundle %s (%d)"), *BundleUrl, ResponseCode);
			}
			return;
		}

		Context->Logf(TEXT("Decompressing %s (%d bytes)"), *BundleUrl, Bundle.UncompressedLength);

		TArray<uint8> UncompressedData;
		UncompressedData.SetNum(Bundle.UncompressedLength);

		if (!FCompression::UncompressMemory(NAME_Gzip, UncompressedData.GetData(), Bundle.UncompressedLength, CompressedData.GetData(), CompressedData.Num()))
		{
			Context->Logf(ELogVerbosity::Warning, TEXT("Unable to decompress bundle %s"), *BundleUrl);
			return;
		}

		FString TempFile = Bundle.LocalFile + TEXT(".incoming");
		if (!FFileHelper::SaveArrayToFile(UncompressedData, *TempFile))
		{
			Context->Logf(ELogVerbosity::Warning, TEXT("Unable to save bundle to %s"), *TempFile);
			return;
		}

		IFileManager::Get().Move(*Bundle.LocalFile, *TempFile);
		Context->Logf(TEXT("Finished downloading %s to %s"), *BundleUrl, *Bundle.LocalFile);
	}

	virtual bool Update(int NumBytes, int MaxBytes) override final
	{
		FScopeLock Lock(&CriticalSection);
		DownloadedBytes = NumBytes;
		return !Context->ReceivedUserCancel();
	}
};

//----------------------------------------------------------------------------------------------------------
// FS3DerivedDataBackend::FRootManifest
//----------------------------------------------------------------------------------------------------------

struct FS3DerivedDataBackend::FRootManifest
{
	FString AccessKey;
	FString SecretKey;
	TArray<FString> Keys;

	bool Load(const FString& RootManifestPath)
	{
		// Read the root manifest from disk
		FString RootManifestText;
		if (!FFileHelper::LoadFileToString(RootManifestText, *RootManifestPath))
		{
			UE_LOG(LogDerivedDataCache, Warning, TEXT("Unable to read manifest from %s"), *RootManifestPath);
			return false;
		}

		// Deserialize a JSON object from the string
		TSharedPtr<FJsonObject> RootManifestObject;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(RootManifestText), RootManifestObject) || !RootManifestObject.IsValid())
		{
			UE_LOG(LogDerivedDataCache, Warning, TEXT("Unable to parse manifest from %s"), *RootManifestPath);
			return false;
		}

		// Read the access and secret keys
		if (!RootManifestObject->TryGetStringField(TEXT("AccessKey"), AccessKey))
		{
			UE_LOG(LogDerivedDataCache, Warning, TEXT("Root manifest %s does not specify AccessKey"), *RootManifestPath);
			return false;
		}
		if (!RootManifestObject->TryGetStringField(TEXT("SecretKey"), SecretKey))
		{
			UE_LOG(LogDerivedDataCache, Warning, TEXT("Root manifest %s does not specify SecretKey"), *RootManifestPath);
			return false;
		}

		// Parse out the list of manifests
		const TArray<TSharedPtr<FJsonValue>>* RootEntriesArray;
		if (!RootManifestObject->TryGetArrayField(TEXT("Entries"), RootEntriesArray))
		{
			UE_LOG(LogDerivedDataCache, Warning, TEXT("Root manifest from %s is missing entries array"), *RootManifestPath);
			return false;
		}
		for (const TSharedPtr<FJsonValue>& Value : *RootEntriesArray)
		{
			const TSharedPtr<FJsonObject>& LastRootManifestEntry = (*RootEntriesArray)[RootEntriesArray->Num() - 1]->AsObject();
			Keys.Add(LastRootManifestEntry->GetStringField("Key"));
		}

		return true;
	}
};

//----------------------------------------------------------------------------------------------------------
// FS3DerivedDataBackend
//----------------------------------------------------------------------------------------------------------

FS3DerivedDataBackend::FS3DerivedDataBackend(const TCHAR* InRootManifestPath, const TCHAR* InBaseUrl, const TCHAR* InRegion, const TCHAR* InCanaryObjectKey, const TCHAR* InCachePath)
	: RootManifestPath(InRootManifestPath)
	, BaseUrl(InBaseUrl)
	, Region(InRegion)
	, CanaryObjectKey(InCanaryObjectKey)
	, CacheDir(InCachePath)
	, bEnabled(false)
{
	FRootManifest RootManifest;
	if (RootManifest.Load(InRootManifestPath))
	{
		RequestPool.Reset(new FRequestPool(TCHAR_TO_ANSI(InRegion), TCHAR_TO_ANSI(*RootManifest.AccessKey), TCHAR_TO_ANSI(*RootManifest.SecretKey)));

		// Test whether we can reach the canary URL
		bool bCanaryValid = true;
		if (GIsBuildMachine)
		{
			UE_LOG(LogDerivedDataCache, Log, TEXT("S3DerivedDataBackend: Disabling on build machine"));
			bCanaryValid = false;
		}
		else if (CanaryObjectKey.Len() > 0)
		{
			TArray<uint8> Data;

			FStringOutputDevice DummyOutputDevice;
			if (!IsSuccessfulHttpResponse(RequestPool->Download(*(BaseUrl / CanaryObjectKey), nullptr, Data, &DummyOutputDevice)))
			{
				UE_LOG(LogDerivedDataCache, Log, TEXT("S3DerivedDataBackend: Unable to download canary file. Disabling."));
				bCanaryValid = false;
			}
		}

		// Allow the user to override it from the editor
		bool bSetting;
		if (GConfig->GetBool(TEXT("/Script/UnrealEd.EditorSettings"), TEXT("bEnableS3DDC"), bSetting, GEditorSettingsIni) && !bSetting)
		{
			UE_LOG(LogDerivedDataCache, Log, TEXT("S3DerivedDataBackend: Disabling due to config setting"));
			bCanaryValid = false;
		}

		// Try to read the bundles
		if (bCanaryValid)
		{
			UE_LOG(LogDerivedDataCache, Log, TEXT("Using %s S3 backend at %s"), *Region, *BaseUrl);

			FFeedbackContext* Context = FDesktopPlatformModule::Get()->GetNativeFeedbackContext();
			Context->BeginSlowTask(NSLOCTEXT("S3DerivedDataBackend", "DownloadingDDCBundles", "Downloading DDC bundles..."), true, true);

			if (DownloadManifest(RootManifest, Context))
			{
				// Get the path for each bundle that needs downloading
				for (FBundle& Bundle : Bundles)
				{
					Bundle.LocalFile = CacheDir / Bundle.Name;
				}

				// Remove any bundles that are no longer required
				RemoveUnusedBundles();

				// Create a critical section used for updating download state
				FCriticalSection CriticalSection;

				// Create all the download tasks
				TArray<TSharedPtr<FBundleDownload>> Downloads;
				for (FBundle& Bundle : Bundles)
				{
					if (!FPaths::FileExists(Bundle.LocalFile))
					{
						TSharedPtr<FBundleDownload> Download(new FBundleDownload(CriticalSection, Bundle, BaseUrl + Bundle.ObjectKey, *RequestPool.Get(), Context));
						Download->Event = FFunctionGraphTask::CreateAndDispatchWhenReady([Download]() { Download->Execute(); }, TStatId());
						Downloads.Add(MoveTemp(Download));
					}
				}

				// Loop until the downloads have all finished
				for (bool bComplete = false; !bComplete; )
				{
					FPlatformProcess::Sleep(0.1f);

					int64 NumBytes = 0;
					int64 MaxBytes = 0;
					bComplete = true;

					CriticalSection.Lock();
					for (TSharedPtr<FBundleDownload>& Download : Downloads)
					{
						NumBytes += Download->DownloadedBytes;
						MaxBytes += Download->Bundle.CompressedLength;
						bComplete &= Download->Event->IsComplete();
					}
					CriticalSection.Unlock();

					int NumMB = (int)((NumBytes + (1024 * 1024) - 1) / (1024 * 1024));
					int MaxMB = (int)((MaxBytes + (1024 * 1024) - 1) / (1024 * 1024));
					if (MaxBytes > 0)
					{
						FText StatusText = FText::Format(NSLOCTEXT("S3DerivedDataBackend", "DownloadingDDCBundlesPct", "Downloading DDC bundles... ({0}Mb/{1}Mb)"), NumMB, MaxMB);
						Context->StatusUpdate((int)((NumBytes * 1000) / MaxBytes), 1000, StatusText);
					}
				}

				// Mount all the bundles
				ParallelFor(Bundles.Num(), [this](int32 Index) { ReadBundle(Bundles[Index]); });
				bEnabled = true;
			}

			Context->EndSlowTask();
		}
	}
}

FS3DerivedDataBackend::~FS3DerivedDataBackend()
{
}

bool FS3DerivedDataBackend::IsUsable() const
{
	return bEnabled;
}

bool FS3DerivedDataBackend::CachedDataProbablyExists(const TCHAR* CacheKey)
{
	const FBundle* Bundle;
	const FBundleEntry* BundleEntry;

	if (ShouldSimulateMiss(CacheKey))
	{
		return false;
	}

	if (!FindBundleEntry(CacheKey, Bundle, BundleEntry))
	{
		UE_LOG(LogDerivedDataCache, Verbose, TEXT("S3DerivedDataBackend: Cache miss on %s (probably)"), CacheKey);
		return false;
	}
	return true;
}

bool FS3DerivedDataBackend::GetCachedData(const TCHAR* CacheKey, TArray<uint8>& OutData)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(S3DDC_Get);
	TRACE_COUNTER_ADD(S3DDC_Get, int64(1));
	COOK_STAT(auto Timer = UsageStats.TimeGet());

	if (ShouldSimulateMiss(CacheKey))
	{
		return false;
	}

	const FBundle* Bundle;
	const FBundleEntry* BundleEntry;
	if (FindBundleEntry(CacheKey, Bundle, BundleEntry))
	{
		TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*Bundle->LocalFile));
		if (!Reader->IsError())
		{
			UE_LOG(LogDerivedDataCache, Verbose, TEXT("S3DerivedDataBackend: Cache hit on %s"), CacheKey);
			OutData.SetNum(BundleEntry->Length);
			Reader->Seek(BundleEntry->Offset);
			Reader->Serialize(OutData.GetData(), BundleEntry->Length);
			return true;
		}
	}

	UE_LOG(LogDerivedDataCache, Verbose, TEXT("S3DerivedDataBackend: Cache miss on %s"), CacheKey);
	return false;
}

void FS3DerivedDataBackend::PutCachedData(const TCHAR* CacheKey, TArrayView<const uint8> InData, bool bPutEvenIfExists)
{
	// Not implemented
}

void FS3DerivedDataBackend::RemoveCachedData(const TCHAR* CacheKey, bool bTransient)
{
	// Not implemented
}

TSharedRef<FDerivedDataCacheStatsNode> FS3DerivedDataBackend::GatherUsageStats() const
{
	TSharedRef<FDerivedDataCacheStatsNode> Usage = MakeShared<FDerivedDataCacheStatsNode>(this, FString::Printf(TEXT("%s @ %s"), TEXT("S3"), *BaseUrl));
	Usage->Stats.Add(TEXT(""), UsageStats);

	return Usage;
}

FString FS3DerivedDataBackend::GetName() const
{
	return BaseUrl;
}

FDerivedDataBackendInterface::ESpeedClass FS3DerivedDataBackend::GetSpeedClass() const
{
	return ESpeedClass::Local;
}

bool FS3DerivedDataBackend::TryToPrefetch(const TCHAR* CacheKey)
{
	return false;
}

bool FS3DerivedDataBackend::WouldCache(const TCHAR* CacheKey, TArrayView<const uint8> InData)
{
	return false;
}

bool FS3DerivedDataBackend::DownloadManifest(const FRootManifest& RootManifest, FFeedbackContext* Context)
{
	// Read the root manifest from disk
	if (RootManifest.Keys.Num() == 0)
	{
		Context->Logf(ELogVerbosity::Warning, TEXT("Root manifest has empty entries array"));
		return false;
	}

	// Get the object key for the last entry
	FString BundleManifestKey = RootManifest.Keys.Last();

	// Download the bundle manifest
	TArray<uint8> BundleManifestData;
	long ResponseCode = RequestPool->Download(*(BaseUrl + BundleManifestKey), nullptr, BundleManifestData, Context);
	if (!IsSuccessfulHttpResponse(ResponseCode))
	{
		Context->Logf(ELogVerbosity::Warning, TEXT("Unable to download bundle manifest from %s (%d)"), *BundleManifestKey, (int)ResponseCode);
		return false;
	}

	// Convert it to text
	BundleManifestData.Add(0);
	FString BundleManifestText = ANSI_TO_TCHAR((const ANSICHAR*)BundleManifestData.GetData());

	// Deserialize a JSON object from the string
	TSharedPtr<FJsonObject> BundleManifestObject;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(BundleManifestText), BundleManifestObject) || !BundleManifestObject.IsValid())
	{
		Context->Logf(ELogVerbosity::Warning, TEXT("Unable to parse manifest from %s"), *BundleManifestKey);
		return false;
	}

	// Parse out the list of bundles
	const TArray<TSharedPtr<FJsonValue>>* BundlesArray;
	if (!BundleManifestObject->TryGetArrayField(TEXT("Entries"), BundlesArray))
	{
		Context->Logf(ELogVerbosity::Warning, TEXT("Manifest from %s is missing bundles array"), *BundleManifestKey);
		return false;
	}

	// Parse out each bundle
	for (const TSharedPtr<FJsonValue>& BundleValue : *BundlesArray)
	{
		const FJsonObject& BundleObject = *BundleValue->AsObject();

		FBundle Bundle;
		if (!BundleObject.TryGetStringField(TEXT("Name"), Bundle.Name))
		{
			Context->Logf(ELogVerbosity::Warning, TEXT("Manifest from %s is missing a bundle name"), *BundleManifestKey);
			return false;
		}
		if (!BundleObject.TryGetStringField(TEXT("ObjectKey"), Bundle.ObjectKey))
		{
			Context->Logf(ELogVerbosity::Warning, TEXT("Manifest from %s is missing an bundle object key"), *BundleManifestKey);
			return false;
		}
		if (!BundleObject.TryGetNumberField(TEXT("CompressedLength"), Bundle.CompressedLength))
		{
			Context->Logf(ELogVerbosity::Warning, TEXT("Manifest from %s is missing the compressed length"), *BundleManifestKey);
			return false;
		}
		if (!BundleObject.TryGetNumberField(TEXT("UncompressedLength"), Bundle.UncompressedLength))
		{
			Context->Logf(ELogVerbosity::Warning, TEXT("Manifest from %s is missing the uncompressed length"), *BundleManifestKey);
			return false;
		}

		Bundles.Add(MoveTemp(Bundle));
	}

	return true;
}

void FS3DerivedDataBackend::RemoveUnusedBundles()
{
	IFileManager& FileManager = IFileManager::Get();

	// Find all the files we want to keep
	TSet<FString> KeepFiles;
	for (const FBundle& Bundle : Bundles)
	{
		KeepFiles.Add(Bundle.Name);
	}

	// Find all the files on disk
	TArray<FString> Files;
	FileManager.FindFiles(Files, *CacheDir);

	// Remove anything left over
	for (const FString& File : Files)
	{
		if (!KeepFiles.Contains(File))
		{
			FileManager.Delete(*(CacheDir / File));
		}
	}
}

void FS3DerivedDataBackend::ReadBundle(FBundle& Bundle)
{
	IFileManager& FileManager = IFileManager::Get();

	// Open the file for reading. If this fails, assume it's because the download was aborted.
	TUniquePtr<FArchive> Reader(FileManager.CreateFileReader(*Bundle.LocalFile));
	if (!Reader.IsValid() || Reader->IsError())
	{
		UE_LOG(LogDerivedDataCache, Warning, TEXT("Unable to open bundle %s for reading. Ignoring."), *Bundle.LocalFile);
		return;
	}
	
	struct FFileHeader
	{
		uint32 Signature;
		int32 NumRecords;
	};

	FFileHeader Header;
	Reader->Serialize(&Header, sizeof(Header));

	const uint32 BundleSignature = (uint32)'D' | ((uint32)'D' << 8) | ((uint32)'B' << 16);
	const uint32 BundleSignatureV1 = BundleSignature | (1U << 24);
	if (Header.Signature != BundleSignatureV1)
	{
		UE_LOG(LogDerivedDataCache, Warning, TEXT("Unable to read bundle with signature %08x"), Header.Signature);
		return;
	}

	struct FFileRecord
	{
		FSHAHash Hash;
		uint32 Length;
	};

	TArray<FFileRecord> Records;
	Records.SetNum(Header.NumRecords);
	Reader->Serialize(Records.GetData(), Header.NumRecords * sizeof(FFileRecord));

	Bundle.Entries.Reserve(Records.Num());

	int64 Offset = Reader->Tell();
	for (const FFileRecord& Record : Records)
	{
		FBundleEntry& Entry = Bundle.Entries.Add(Record.Hash);
		Entry.Offset = Offset;
		Entry.Length = Record.Length;
		Offset += Record.Length;
		check(Offset <= Bundle.UncompressedLength);
	}
}

bool FS3DerivedDataBackend::FindBundleEntry(const TCHAR* CacheKey, const FBundle*& OutBundle, const FBundleEntry*& OutBundleEntry) const
{
	FSHAHash Hash;

	auto AnsiString = StringCast<ANSICHAR>(*BuildPathForCacheKey(CacheKey).ToUpper());
	FSHA1::HashBuffer(AnsiString.Get(), AnsiString.Length(), Hash.Hash);

	for (const FBundle& Bundle : Bundles)
	{
		const FBundleEntry* Entry = Bundle.Entries.Find(Hash);
		if (Entry != nullptr)
		{
			OutBundle = &Bundle;
			OutBundleEntry = Entry;
			return true;
		}
	}

	return false;
}

bool FS3DerivedDataBackend::ApplyDebugOptions(FBackendDebugOptions& InOptions)
{
	DebugOptions = InOptions;
	return true;
}

bool FS3DerivedDataBackend::DidSimulateMiss(const TCHAR* InKey)
{
	if (DebugOptions.RandomMissRate == 0 || DebugOptions.SimulateMissTypes.Num() == 0)
	{
		return false;
	}
	FScopeLock Lock(&MissedKeysCS);
	return DebugMissedKeys.Contains(FName(InKey));
}

bool FS3DerivedDataBackend::ShouldSimulateMiss(const TCHAR* InKey)
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

#endif

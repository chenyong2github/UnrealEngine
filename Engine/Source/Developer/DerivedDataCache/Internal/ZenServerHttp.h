// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ZenServerInterface.h"

#include "Containers/StaticArray.h"
#include "Containers/UnrealString.h"

class FCompositeBuffer;
class FCbPackage;

#if UE_WITH_ZEN

namespace UE::Zen {

static bool IsSuccessCode(int ResponseCode)
{
	return 200 <= ResponseCode && ResponseCode < 300;
}

enum class EContentType
{
	Binary,
	CompactBinary,
	CompactBinaryPackage
};

/** Minimal HTTP request type wrapping CURL without the need for managers. This request
  * is written to allow reuse of request objects, in order to allow connections to be reused.
	
  * CURL has a global library initialization (curl_global_init). We rely on this happening in
  * the Online/HTTP library which is a dependency of this module
  */

class FZenHttpRequest
{
public:
	DERIVEDDATACACHE_API FZenHttpRequest(const TCHAR* InDomain, bool bInLogErrors);
	DERIVEDDATACACHE_API ~FZenHttpRequest();

	/**
		* Resets all options on the request except those that should always be set.
		*/
	void Reset();

	/** Returns the HTTP response code.*/
	inline const int GetResponseCode() const
	{
		return int(ResponseCode);
	}

	inline const bool GetResponseFormatValid() const
	{
		return bResponseFormatValid;
	}

	/** Returns the number of bytes sent during this request (headers withstanding). */
	inline const size_t  GetBytesSent() const
	{
		return BytesSent;
	}

	/**
		* Convenience result type interpreted from HTTP response code.
		*/
	enum class Result
	{
		Success,
		Failed
	};

	/**
		* Upload buffer using the request, using PUT verb
		* @param Uri Url to use.
		* @param Buffer Data to upload
		* @return Result of the request
		*/
	DERIVEDDATACACHE_API Result PerformBlockingPut(const TCHAR* Uri, const FCompositeBuffer& Buffer, EContentType ContentType);

	/**
		* Download an url into a buffer using the request.
		* @param Uri Url to use.
		* @param Buffer Optional buffer where data should be downloaded to. If this is null then 
		* downloaded data will be stored in an internal buffer and accessed via GetResponseAsString
		* @return Result of the request
		*/
	DERIVEDDATACACHE_API Result PerformBlockingDownload(const TCHAR* Uri, TArray<uint8>* Buffer);

	/**
		* Download an url into a buffer using the request.
		* @param Uri Url to use.
		* @param OutPackage Package instance which will receive the data
		* @result Request success/failure status
		*/
	DERIVEDDATACACHE_API Result PerformBlockingDownload(const TCHAR* Uri, FCbPackage& OutPackage);

	/**
		* Query an url using the request. Queries can use either "Head" or "Delete" verbs.
		* @param Uri Url to use.
		* @return Result of the request
		*/
	DERIVEDDATACACHE_API Result PerformBlockingHead(const TCHAR* Uri);

	/**
		* Query an url using the request. Queries can use either "Head" or "Delete" verbs.
		* @param Uri Url to use.
		* @return Result of the request
		*/
	DERIVEDDATACACHE_API Result PerformBlockingDelete(const TCHAR* Uri);

	/**
		* Returns the response buffer as a string. Note that is the request is performed
		* with an external buffer as target buffer this string will be empty.
		*/
	inline FString GetResponseAsString() const
	{
		return GetAnsiBufferAsString(ResponseBuffer);
	}

private:
	void* /* CURL */		Curl = nullptr;
	long /* CURLCode */		CurlResult;
	long					ResponseCode = 0;
	size_t					BytesSent = 0;
	size_t					BytesReceived = 0;
	bool					bLogErrors = false;
	bool					bResponseFormatValid = false;

	const FCompositeBuffer*	ReadDataView = nullptr;
	TArray<uint8>*			WriteDataBufferPtr = nullptr;
	FCbPackage*				WriteDataPackage = nullptr;
	TArray<uint8>*			WriteHeaderBufferPtr = nullptr;

	TArray<uint8>			ResponseHeader;
	TArray<uint8>			ResponseBuffer;
	TArray<FString>			Headers;
	FString					Domain;

	/**
	  * Supported request verb
	  */
	enum class RequestVerb
	{
		Get,
		Put,
		Post,
		Delete,
		Head
	};

	void LogResult(long /*CURLcode*/ Result, const TCHAR* Uri, RequestVerb Verb) const;

	/**
		* Performs the request, blocking until finished.
		* @param Uri Address on the domain to query
		* @param Verb HTTP verb to use
		* @param Buffer Optional buffer to directly receive the result of the request.
		* If unset the response body will be stored in the request.
		*/
	Result PerformBlocking(const TCHAR* Uri, RequestVerb Verb, uint32 ContentLength);

	static FString GetAnsiBufferAsString(const TArray<uint8>& Buffer);

	struct FStatics;
};

/**
  * Pool which manages a fixed set of requests. Users are required to release requests that have been
  * acquired. 
  * 
  * Intended to be used with \ref FScopedRequestPtr which handles lifetime management transparently
  */
struct FZenHttpRequestPool
{
	DERIVEDDATACACHE_API explicit FZenHttpRequestPool(const TCHAR* InServiceUrl);
	DERIVEDDATACACHE_API ~FZenHttpRequestPool();

	/** Block until a request is free. Once a request has been returned it is
	  * "owned by the caller and need to release it to the pool when work has been completed.
	  * @return Usable request instance.
	  */
	DERIVEDDATACACHE_API FZenHttpRequest* WaitForFreeRequest();

	/** Release request to the pool.
	  * @param Request Request that should be freed. Note that any buffer owned by the request can now be reset.
	  */
	DERIVEDDATACACHE_API void ReleaseRequestToPool(FZenHttpRequest* Request);

private:
	struct FEntry
	{
		TAtomic<uint8> Usage;
		FZenHttpRequest* Request;
	};

	TStaticArray<FEntry, 16> Pool;
};

/**
  * Utility class to manage requesting and releasing requests from the \ref FRequestPool.
  */
struct FZenScopedRequestPtr
{
public:
	FZenScopedRequestPtr(FZenHttpRequestPool* InPool)
	:	Request(InPool->WaitForFreeRequest())
	,	Pool(InPool)
	{
	}

	~FZenScopedRequestPtr()
	{
		Pool->ReleaseRequestToPool(Request);
	}

	inline bool IsValid() const
	{
		return Request != nullptr;
	}

	inline operator bool() const { return IsValid(); }

	FZenHttpRequest* operator->()
	{
		check(IsValid());
		return Request;
	}

private:
	FZenHttpRequest*		Request;
	FZenHttpRequestPool*	Pool;
};

} // namespace UE::Zen

#endif // UE_WITH_ZEN

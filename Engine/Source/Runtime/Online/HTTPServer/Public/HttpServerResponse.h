// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"

struct FHttpServerResponse final
{
public:
	/**
	 * Constructor
	 */
	FHttpServerResponse() 
	{ }

	/**
	 * Constructor
	 * Facilitates in-place body construction
     *
	 * @param InBody The r-value body data
	 */
	FHttpServerResponse(TArray<uint8>&& InBody)
		: Body(MoveTemp(InBody))
	{ }

	/** Http Response Code */
	int32 Code;

	/** Http Headers */
	TMap<FString, TArray<FString>> Headers;

	/** Http Body Content */
	TArray<uint8> Body;

public:

	/**
	 * Creates an FHttpServerResponse from a string
	 * 
	 * @param  Text         The text to serialize
	 * @param  ContentType  The HTTP response content type
	 * @return              A unique pointer to an initialized response object
	 */
	HTTPSERVER_API static TUniquePtr<FHttpServerResponse> Create(const FString& Text, FString ContentType);

	/**
	 * Creates an FHttpServerResponse from a raw byte buffer
	 *
	 * @param  RawBytes     The byte buffer to serialize
	 * @param  ContentType  The HTTP response content type
	 * @return              A unique pointer to an initialized response object
	 */
	HTTPSERVER_API static TUniquePtr<FHttpServerResponse> Create(TArray<uint8>&& RawBytes, FString ContentType);

	/**
	 * Creates an FHttpServerResponse from a raw byte buffer
	 *
	 * @param  RawBytes     The byte buffer view to serialize
	 * @param  ContentType  The HTTP response content type
	 * @return              A unique pointer to an initialized response object
	 */
	HTTPSERVER_API static TUniquePtr<FHttpServerResponse> Create(const TArrayView<uint8>& RawBytes, FString ContentType);

	/**
	 * Creates an FHttpServerResponse from a response code
	 *
	 * @param  HttpResponseCode  The HTTP response/error code
	 * @param  ErrorCode         The string error code/message
	 * @return                   A unique pointer to an initialized response object
	 */
	HTTPSERVER_API static TUniquePtr<FHttpServerResponse> Create(int32 HttpResponseCode, const FString& ErrorCode);

	/**
	 * Creates an FHttpServerResponse 200
	 * 
	 * @param Message The optional and respective success message
	 * @param ContentType The optional and respective content type
	 * @return A unique pointer to an initialized response object
	 */
	HTTPSERVER_API static TUniquePtr<FHttpServerResponse> Ok(const FString& Message = TEXT(""), FString ContentType = TEXT("text/text"));

	/**
    * Creates an FHttpServerResponse 500
	*
	* @param Message The optional and respective error message
	* @param ContentType The optional and respective content type
    * @return A unique pointer to an initialized response object
    */
	HTTPSERVER_API static TUniquePtr<FHttpServerResponse> Error(const FString& Message = TEXT(""), FString ContentType = TEXT("text/text"));
};



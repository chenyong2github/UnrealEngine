// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IStructSerializerBackend.h"
#include "Serialization/RCJsonStructSerializerBackend.h"
#include "Serialization/RCJsonStructDeserializerBackend.h"
#include "HttpServerResponse.h"
#include "HttpServerRequest.h"
#include "Serialization/MemoryReader.h"
#include "StructDeserializer.h"
#include "StructSerializer.h"
#include "RemoteControlRequest.h"
#include "Templates/UnrealTypeTraits.h"
#include "WebRemoteControlUtils.h"


namespace RemotePayloadSerializer
{
	/**
	 * Replaces the first occurrence of a string in a TCHAR binary payload.
	 */
	void ReplaceFirstOccurence(TConstArrayView<uint8> InPayload, const FString& From, const FString& To, TArray<uint8>& OutModifiedPayload);

	/**
	 * Converts a string verb to the enum representation.
	 */
	EHttpServerRequestVerbs ParseHttpVerb(FName InVerb);

	/**
	 * Unwrap a request wrapper, while copying headers and http version from a template request if available.
	 */
	TSharedRef<FHttpServerRequest> UnwrapHttpRequest(const FRCRequestWrapper& Wrapper, const FHttpServerRequest* TemplateRequest = nullptr);

	/**
	 * @note This will serialize in ANSI directly.
	 */
	void SerializeWrappedCallResponse(int32 RequestId, TUniquePtr<FHttpServerResponse> Response, FMemoryWriter& Writer);

	bool DeserializeCall(const FHttpServerRequest& InRequest, FRCCall& OutCall, const FHttpResultCallback& InCompleteCallback);

	bool SerializeCall(const FRCCall& InCall, TArray<uint8>& OutPayload, bool bOnlyReturn = false);

	bool DeserializeObjectRef(const FHttpServerRequest& InRequest, FRCObjectReference& OutObjectRef, FRCObjectRequest& OutDeserializedRequest, const FHttpResultCallback& InCompleteCallback);
}


namespace WebRemoteControlInternalUtils
{
	static const TCHAR* WrappedRequestHeader = TEXT("UE-Wrapped-Request");
	static const FString PassphraseHeader = TEXT("Passphrase");

	/**
	 * Construct a default http response with CORS headers.
	 * @param InResponseCode The response's code. (Defaults to a bad request)
	 * @return The constructed server response.
	 */
	TUniquePtr<FHttpServerResponse> CreateHttpResponse(EHttpServerResponseCodes InResponseCode = EHttpServerResponseCodes::BadRequest);

	/**
	 * Create a json structure containing an error message.
	 * @param InMessage The desired error message.
	 * @param OutUTF8Message The error message wrapped in a json struct, in UTF-8 format.
	 * Example output:
	 *	{
	 *	  errorMessage: "Request content type must be application/json" 
	 *	}
	 */
	void CreateUTF8ErrorMessage(const FString& InMessage, TArray<uint8>& OutUTF8Message);

	/**
	 * Deserialize a json structure to find the start and end of every struct parameter in the request.
	 * @param InTCHARPayload The json payload to deserialize.
	 * @param InOutStructParameters A map of struct parameter names to the start and end of the struct in the payload.
	 * @param OutErrorText If set, the string pointer will be populated with an error message on error.
	 * @return Whether the deserialization was successful.
	 */
	bool GetStructParametersDelimiters(TConstArrayView<uint8> InTCHARPayload, TMap<FString, FBlockDelimiters>& InOutStructParameters, FString* OutErrorText = nullptr);

	/**
	 * Deserialize a request into a UStruct.
	 * @param InTCHARPayload The json payload to deserialize.
	 * @param InCompleteCallback The callback to call error.
	 * @param The structure to serialize using the request's content.
	 * @return Whether the deserialization was successful.
	 *
	 * @note InCompleteCallback will be called with an appropriate http response if the deserialization fails.
	 */
	template <typename RequestType>
	UE_NODISCARD bool DeserializeRequestPayload(TConstArrayView<uint8> InTCHARPayload, const FHttpResultCallback* InCompleteCallback, RequestType& OutDeserializedRequest)
	{
		FMemoryReaderView Reader(InTCHARPayload);
		FJsonStructDeserializerBackend DeserializerBackend(Reader);
		if (!FStructDeserializer::Deserialize(&OutDeserializedRequest, *RequestType::StaticStruct(), DeserializerBackend, FStructDeserializerPolicies()))
		{
			if (InCompleteCallback)
			{
				TUniquePtr<FHttpServerResponse> Response = CreateHttpResponse();
				CreateUTF8ErrorMessage(TEXT("Unable to deserialize request."), Response->Body);
				(*InCompleteCallback)(MoveTemp(Response));
			}
			return false;
		}

		if (!GetStructParametersDelimiters(InTCHARPayload, OutDeserializedRequest.GetStructParameters(), nullptr))
		{
			if (InCompleteCallback)
			{
				TUniquePtr<FHttpServerResponse> Response = CreateHttpResponse();
				CreateUTF8ErrorMessage(TEXT("Unable to deserialize request."), Response->Body);
				(*InCompleteCallback)(MoveTemp(Response));
			}
			return false;
		}

		return true;
	}

	/**
	 * Deserialize a wrapped request into a wrapper struct.
	 * @param InTCHARPayload The json payload to deserialize.
	 * @param InCompleteCallback The callback to call error.
	 * @param The wrapper structure to populate with the request's content.
	 * @return Whether the deserialization was successful.
	 */
	UE_NODISCARD inline bool DeserializeWrappedRequestPayload(TConstArrayView<uint8> InTCHARPayload, const FHttpResultCallback* InCompleteCallback, FRCRequestWrapper& Wrapper)
	{
		if (!DeserializeRequestPayload(InTCHARPayload, InCompleteCallback, Wrapper))
		{
			return false;
		}

		FBlockDelimiters& BodyDelimiters = Wrapper.GetParameterDelimiters(FRCRequestWrapper::BodyLabel());
		if (BodyDelimiters.BlockStart != BodyDelimiters.BlockEnd)
		{
			Wrapper.TCHARBody = InTCHARPayload.Slice(BodyDelimiters.BlockStart, BodyDelimiters.BlockEnd - BodyDelimiters.BlockStart);
		}

		return true;
	}

	/**
	 * Get the struct delimiters for all the batched requests.
	 * @param InTCHARPayload The json payload to deserialize.
	 * @param OutStructParameters A mapping of Request Id to their respective struct delimiters.
	 * @param OutErrorText If set, the string pointer will be populated with an error message on error.
	 * @return Whether the delimiters were able to be found.
	 */
	UE_NODISCARD bool GetBatchRequestStructDelimiters(TConstArrayView<uint8> InTCHARPayload, TMap<int32, FBlockDelimiters>& OutStructParameters, FString* OutErrorText = nullptr);
	
	/**
	 * Specialization of DeserializeRequestPayload that handles Batch requests.
	 * This will populate the TCHARBody of all the wrapped requests.
	 * @param InTCHARPayload The json payload to deserialize.
	 * @param InCompleteCallback The callback to call error.
	 * @param The structure to serialize using the request's content.
	 * @return Whether the deserialization was successful.
	 *
	 * @note InCompleteCallback will be called with an appropriate http response if the deserialization fails.
	 */
	template <>
	UE_NODISCARD inline bool DeserializeRequestPayload(TConstArrayView<uint8> InTCHARPayload, const FHttpResultCallback* InCompleteCallback, FRCBatchRequest& OutDeserializedRequest)
	{
		FMemoryReaderView Reader(InTCHARPayload);
		FJsonStructDeserializerBackend DeserializerBackend(Reader);
		
		if (!FStructDeserializer::Deserialize(&OutDeserializedRequest, *FRCBatchRequest::StaticStruct(), DeserializerBackend, FStructDeserializerPolicies()))
		{
			if (InCompleteCallback)
			{
				TUniquePtr<FHttpServerResponse> Response = CreateHttpResponse();
				CreateUTF8ErrorMessage(TEXT("Unable to deserialize request."), Response->Body);
				(*InCompleteCallback)(MoveTemp(Response));
			}
			return false;
		}

		TMap<int32, FBlockDelimiters> Delimiters;
		if (!GetBatchRequestStructDelimiters(InTCHARPayload, Delimiters, nullptr))
		{
			if (InCompleteCallback)
			{
				TUniquePtr<FHttpServerResponse> Response = CreateHttpResponse();
				CreateUTF8ErrorMessage(TEXT("Unable to deserialize request."), Response->Body);
				(*InCompleteCallback)(MoveTemp(Response));
			}
			return false;
		}

		for (FRCRequestWrapper& Wrapper : OutDeserializedRequest.Requests)
		{
			if (FBlockDelimiters* BodyDelimiters = Delimiters.Find(Wrapper.RequestId))
			{
				Wrapper.GetParameterDelimiters(FRCRequestWrapper::BodyLabel()) = MoveTemp(*BodyDelimiters);
				Wrapper.TCHARBody = InTCHARPayload.Slice(BodyDelimiters->BlockStart, BodyDelimiters->BlockEnd - BodyDelimiters->BlockStart);
			}
		}

		return true;
	}

	/**
	 * Adds a header indicating that the request is a wrapped request that originated from the engine itself.
	 */
	void AddWrappedRequestHeader(FHttpServerRequest& Request);

	/**
	 * Get whether the request is a wrapped request.
	 */
	bool IsWrappedRequest(const FHttpServerRequest& Request);

	/**
	 * Deserialize a request into a UStruct.
	 * @param InRequest The incoming http request.
	 * @param InCompleteCallback The callback to call error.
	 * @param The structure to serialize using the request's content.
	 * @return Whether the deserialization was successful. 
	 * 
	 * @note InCompleteCallback will be called with an appropriate http response if the deserialization fails.
	 */
	template <typename RequestType>
	UE_NODISCARD bool DeserializeRequest(const FHttpServerRequest& InRequest, const FHttpResultCallback* InCompleteCallback, RequestType& OutDeserializedRequest)
	{
		static_assert(TIsDerivedFrom<RequestType, FRCRequest>::IsDerived, "Argument OutDeserializedRequest must derive from FRCRequest");
		
		if (IsWrappedRequest(InRequest))
		{
			// If the request is wrapped, the body should already be encoded in UCS2.
			OutDeserializedRequest.TCHARBody = InRequest.Body;
		}
		
		if (!OutDeserializedRequest.TCHARBody.Num())
		{
			WebRemoteControlUtils::ConvertToTCHAR(InRequest.Body, OutDeserializedRequest.TCHARBody);
		}

		return DeserializeRequestPayload(OutDeserializedRequest.TCHARBody, InCompleteCallback, OutDeserializedRequest);
	}

	/**
	 * Validate a content-type.
	 * @param InRequest The incoming http request.
	 * @param InContentType The callback to call error.
	 * @param InCompleteCallback The callback to call error.
	 * @return Whether the content type was valid or not.
	 * 
	 * @note InCompleteCallback will be called with an appropriate http response if the content type is not valid.
	 */
	UE_NODISCARD bool ValidateContentType(const FHttpServerRequest& InRequest, FString InContentType, const FHttpResultCallback& InCompleteCallback);

	/**
	 * Add the desired content type to the http response headers.
	 * @param InResponse The response to add the content type to.
	 * @param InContentType The content type header to add.
	 */
	void AddContentTypeHeaders(FHttpServerResponse* InOutResponse, FString InContentType);
	
	/**
	* Add CORS headers to a http response.
	* @param InOutResponse The http response to add the CORS headers to.
	*/
	void AddCORSHeaders(FHttpServerResponse* InOutResponse);

	/**
	 * Validate a request's content type.
	 * @param InRequest The request to validate the content type on.
	 * @param InContentType The target content type.
	 * @param OutErrorText If set, the string pointer will be populated with an error message on error.
	 * @return Whether or not the content type matches the target content type.
	 */
	bool IsRequestContentType(const FHttpServerRequest& InRequest, const FString& InContentType, FString* OutErrorText);

	/**
	 * Serialize a struct on scope.
	 * @param Struct the struct on scope to serialize.
	 * @param Writer the memory archive to write to.
	 */
	template <typename SerializerBackendType = FRCJsonStructSerializerBackend>
	void SerializeStructOnScope(const FStructOnScope& Struct, FMemoryWriter& Writer)
	{
		static_assert(TIsDerivedFrom<SerializerBackendType, IStructSerializerBackend>::IsDerived, "SerializerBackendType must inherit from IStructSerializerBackend.");
		SerializerBackendType SerializerBackend(Writer);
		FStructSerializer::Serialize(Struct.GetStructMemory(), *(UScriptStruct*)Struct.GetStruct(), SerializerBackend, FStructSerializerPolicies());
	}
}

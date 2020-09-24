// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Backends/JsonStructSerializerBackend.h"
#include "Backends/JsonStructDeserializerBackend.h"
#include "HttpServerResponse.h"
#include "HttpServerRequest.h"
#include "Serialization/MemoryReader.h"
#include "StructDeserializer.h"
#include "StructSerializer.h"
#include "RemoteControlRequest.h"
#include "Templates/UnrealTypeTraits.h"


namespace RemotePayloadSerializer
{
	void ReplaceFirstOccurence(TConstArrayView<uint8> InPayload, const FString& From, const FString& To, TArray<uint8>& OutModifiedPayload);

	/**
	 * Convers a string verb to the enum representation.
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

	/**
	 * Calls a the provided serializer function then strips the identifier from the resulting JSON.
	 * ie. A returned payload like:
	 * {
	 *   "RelativeLocation": {
	 *		"prop": 2
	 *    }
	 * }
	 *
	 * Would result in:
	 * "prop": 2
	 *
	 * @todo Modify struct serializer to allow serializing without the identifier since this method is pretty inefficient.
	 */
	bool SerializePartial(TFunctionRef<bool(FJsonStructSerializerBackend&)> SerializeFunction, FMemoryWriter& SerializedPayloadWriter);

	bool DeserializeCall(const FHttpServerRequest& InRequest, FRCCall& OutCall, const FHttpResultCallback& InCompleteCallback);

	bool SerializeCall(const FRCCall& InCall, TArray<uint8>& OutPayload, bool bOnlyReturn = false);

	bool DeserializeObjectRef(const FHttpServerRequest& InRequest, FRCObjectReference& OutObjectRef, FRCObjectRequest& OutDeserializedRequest, const FHttpResultCallback& InCompleteCallback);
}


namespace WebRemoteControlUtils
{
	/**
	 * Convert a UTF-8 payload to a TCHAR payload.
	 * @param InUTF8Payload The UTF-8 payload in binary format.
	 * @param OutTCHARPayload The converted TCHAR output in binary format.
	 */
	void ConvertToTCHAR(TConstArrayView<uint8> InUTF8Payload, TArray<uint8>& OutTCHARPayload);

	/**
	 * Convert a TCHAR payload to UTF-8.
	 * @param InTCHARPayload The TCHAR payload in binary format.
	 * @param OutUTF8Payload The converted UTF-8 output in binary format.
	 */
	void ConvertToUTF8(TConstArrayView<uint8> InTCHARPayload, TArray<uint8>& OutUTF8Payload);

	/**
	 * Convert a FString to UTF-8.
	 * @param InString The string to be converted.
	 * @param OutUTF8Payload the converted UTF-8 output in binary format.
	 */
	void ConvertToUTF8(const FString& InString, TArray<uint8>& OutUTF8Payload);

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

		if (OutDeserializedRequest.GetStructParameters().Num() > 0)
		{
			FString ErrorText;
			if (!GetStructParametersDelimiters(InTCHARPayload, OutDeserializedRequest.GetStructParameters(), &ErrorText))
			{
				if (InCompleteCallback)
				{
					TUniquePtr<FHttpServerResponse> Response = CreateHttpResponse();
					CreateUTF8ErrorMessage(TEXT("Unable to deserialize request."), Response->Body);
					(*InCompleteCallback)(MoveTemp(Response));
				}
				return false;
			}
		}

		return true;
	}

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
		TArray<uint8> WorkingBuffer;
		ConvertToTCHAR(InRequest.Body, WorkingBuffer);

		return DeserializeRequestPayload(WorkingBuffer, InCompleteCallback, OutDeserializedRequest);
	}

	/**
	 * Serialize a response object into a UTF-8 Payload.
	 * @param InResponseObject the object to serialize.
	 * @param OutResponsePayload the resulting UTF-8 payload.
	 */
	template <typename RequestType>
	void SerializeResponse(const RequestType& InResponseObject, TArray<uint8>& OutResponsePayload)
	{
		TArray<uint8> WorkingBuffer;
		FMemoryWriter Writer(WorkingBuffer);
		FJsonStructSerializerBackend SerializeBackend{Writer, EStructSerializerBackendFlags::Default};
		FStructSerializer::Serialize(InResponseObject, SerializeBackend, FStructSerializerPolicies());
		ConvertToUTF8(WorkingBuffer, OutResponsePayload);
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
}

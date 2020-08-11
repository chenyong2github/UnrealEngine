// Copyright Epic Games, Inc. All Rights Reserved.

#include "WebRemoteControlUtils.h"
#include "HttpServerRequest.h"
#include "UObject/StructOnScope.h"
#include "Serialization/JsonReader.h"

namespace HttpUtils
{
	/**
	 * Add the desired content type to the http response headers.
	 * @param InResponse The response to add the content type to.
	 * @param InContentType The content type header to add.
	 */
	void AddContentTypeHeaders(FHttpServerResponse* InOutResponse, FString InContentType)
	{
		InOutResponse->Headers.Add(TEXT("content-type"), { MoveTemp(InContentType) });
	}

	/**
	* Add CORS headers to a http response.
	* @param InOutResponse The http response to add the CORS headers to.
	*/
	void AddCORSHeaders(FHttpServerResponse* InOutResponse)
	{
		check(InOutResponse != nullptr);
		InOutResponse->Headers.Add(TEXT("Access-Control-Allow-Origin"), { TEXT("*") });
		InOutResponse->Headers.Add(TEXT("Access-Control-Allow-Methods"), { TEXT("PUT, POST, GET, OPTIONS") });
		InOutResponse->Headers.Add(TEXT("Access-Control-Allow-Headers"), { TEXT("Origin, X-Requested-With, Content-Type, Accept") });
		InOutResponse->Headers.Add(TEXT("Access-Control-Max-Age"), { TEXT("600") });
	}

	/**
	 * Validate a request's content type.
	 * @param InRequest The request to validate the content type on.
	 * @param InContentType The target content type.
	 * @param OutErrorText If set, the string pointer will be populated with an error message on error.
	 * @return Whether or not the content type matches the target content type.
	 */
	bool IsRequestContentType(const FHttpServerRequest& InRequest, const FString& InContentType, FString* OutErrorText)
	{
		if (const TArray<FString>* ContentTypeHeaders = InRequest.Headers.Find(TEXT("Content-Type")))
		{
			if (ContentTypeHeaders->Num() > 0 && (*ContentTypeHeaders)[0] == InContentType)
			{
				return true;
			}
		}

		if (OutErrorText)
		{
			*OutErrorText = FString::Printf(TEXT("Request content type must be %s"), *InContentType);
		}
		return false;
	}
}

void WebRemoteControlUtils::ConvertToTCHAR(TConstArrayView<uint8> InUTF8Payload, TArray<uint8>& OutTCHARPayload)
{
	int32 StartIndex = OutTCHARPayload.Num();
	OutTCHARPayload.AddUninitialized(FUTF8ToTCHAR_Convert::ConvertedLength((ANSICHAR*)InUTF8Payload.GetData(), InUTF8Payload.Num() / sizeof(ANSICHAR)) * sizeof(TCHAR));
	FUTF8ToTCHAR_Convert::Convert((TCHAR*)(OutTCHARPayload.GetData() + StartIndex), (OutTCHARPayload.Num() - StartIndex) / sizeof(TCHAR), (ANSICHAR*)InUTF8Payload.GetData(), InUTF8Payload.Num() / sizeof(ANSICHAR));
}

void WebRemoteControlUtils::ConvertToUTF8(TConstArrayView<uint8> InTCHARPayload, TArray<uint8>& OutUTF8Payload)
{
	int32 StartIndex = OutUTF8Payload.Num();
	OutUTF8Payload.AddUninitialized(FTCHARToUTF8_Convert::ConvertedLength((TCHAR*)InTCHARPayload.GetData(), InTCHARPayload.Num() / sizeof(TCHAR)) * sizeof(ANSICHAR));
	FTCHARToUTF8_Convert::Convert((ANSICHAR*)(OutUTF8Payload.GetData() + StartIndex), (OutUTF8Payload.Num() - StartIndex) / sizeof(ANSICHAR), (TCHAR*)InTCHARPayload.GetData(), InTCHARPayload.Num() / sizeof(TCHAR));
}

void WebRemoteControlUtils::ConvertToUTF8(const FString& InString, TArray<uint8>& OutUTF8Payload)
{
	int32 StartIndex = OutUTF8Payload.Num();
	OutUTF8Payload.AddUninitialized(FTCHARToUTF8_Convert::ConvertedLength(*InString, InString.Len()) * sizeof(ANSICHAR));
	FTCHARToUTF8_Convert::Convert((ANSICHAR*)(OutUTF8Payload.GetData() + StartIndex), (OutUTF8Payload.Num() - StartIndex) / sizeof(ANSICHAR), *InString, InString.Len());
}

TUniquePtr<FHttpServerResponse> WebRemoteControlUtils::CreateHttpResponse(EHttpServerResponseCodes InResponseCode)
{
	TUniquePtr<FHttpServerResponse> Response = MakeUnique<FHttpServerResponse>();
	HttpUtils::AddCORSHeaders(Response.Get());
	HttpUtils::AddContentTypeHeaders(Response.Get(), TEXT("application/json"));
	Response->Code = InResponseCode;
	return Response;
}

void WebRemoteControlUtils::CreateUTF8ErrorMessage(const FString& InMessage, TArray<uint8>& OutUTF8Message)
{
	WebRemoteControlUtils::ConvertToUTF8(FString::Printf(TEXT("{ \"errorMessage\": \"%s\" }"), *InMessage), OutUTF8Message);
}

bool WebRemoteControlUtils::GetStructParametersDelimiters(TConstArrayView<uint8> InTCHARPayload, TMap<FString, FBlockDelimiters>& InOutStructParameters, FString* OutErrorText)
{
	typedef UCS2CHAR PayloadCharType;
	FMemoryReaderView Reader(InTCHARPayload);
	TSharedRef<TJsonReader<PayloadCharType>> JsonReader = TJsonReader<PayloadCharType>::Create(&Reader);

	EJsonNotation Notation;

	FString ErrorText;
	// The payload should be an object
	JsonReader->ReadNext(Notation);
	if (Notation != EJsonNotation::ObjectStart)
	{
		ErrorText = TEXT("Expected json object.");
	}

	// Mark the start/end of the param object in the payload
	while (JsonReader->ReadNext(Notation) && ErrorText.IsEmpty())
	{
		switch (Notation)
		{
			// this should mean we reached the parameters field, record the start and ending offset
		case EJsonNotation::ObjectStart:
			if (FBlockDelimiters* Delimiters = InOutStructParameters.Find(JsonReader->GetIdentifier()))
			{
				Delimiters->BlockStart = Reader.Tell() - sizeof(PayloadCharType);
				if (JsonReader->SkipObject())
				{
					Delimiters->BlockEnd = Reader.Tell();
				}
				else
				{
					ErrorText = FString::Printf(TEXT("%s object improperly formatted."), *JsonReader->GetIdentifier());
				}
			}
			else
			{
				ErrorText = TEXT("Unexpected object field.");
			}
			break;
			// This means we should be done with the request object
		case EJsonNotation::ObjectEnd:
			break;
			// Read the reset to default property
		case EJsonNotation::Error:
			ErrorText = JsonReader->GetErrorMessage();
			break;
		default:
			// Ignore any other fields
			break;
		}
	}

	if (!ErrorText.IsEmpty())
	{
		UE_LOG(LogRemoteControl, Error, TEXT("Web Remote Control deserialization error: %s"), *ErrorText);
		if (OutErrorText)
		{
			*OutErrorText = MoveTemp(ErrorText);
		}
		return false;
	}

	return true;
}

bool WebRemoteControlUtils::ValidateContentType(const FHttpServerRequest& InRequest, FString InContentType, const FHttpResultCallback& InCompleteCallback)
{
	FString ErrorText;
	if (!HttpUtils::IsRequestContentType(InRequest, MoveTemp(InContentType), &ErrorText))
	{
		TUniquePtr<FHttpServerResponse> Response = CreateHttpResponse();
		CreateUTF8ErrorMessage(ErrorText, Response->Body);
		InCompleteCallback(MoveTemp(Response));
		return false;
	}
	return true;
}

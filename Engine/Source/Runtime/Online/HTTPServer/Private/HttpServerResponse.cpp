
#include "HttpServerResponse.h"
#include "HttpServerConstants.h"

TUniquePtr<FHttpServerResponse> FHttpServerResponse::Create(const FString& Text, FString ContentType)
{
	TUniquePtr<FHttpServerResponse> Response = MakeUnique<FHttpServerResponse>();
	Response->Code = EHttpServerResponseCodes::Ok;

	FTCHARToUTF8 ConvertToUtf8(*Text);
	const uint8* ConvertToUtf8Bytes = (reinterpret_cast<const uint8*>(ConvertToUtf8.Get()));
	Response->Body.Append(ConvertToUtf8Bytes, ConvertToUtf8.Length());

	FString Utf8CharsetContentType = FString::Printf(TEXT("%s ;charset=utf-8"), *ContentType);
	TArray<FString> ContentTypeValue = { Utf8CharsetContentType };
	Response->Headers.Add(FHttpServerHeaderKeys::CONTENT_TYPE, ContentTypeValue);

	return Response;
}

TUniquePtr<FHttpServerResponse> FHttpServerResponse::Create(TArray<uint8>&& RawBytes, FString ContentType)
{
	TUniquePtr<FHttpServerResponse> Response = MakeUnique<FHttpServerResponse>(MoveTemp(RawBytes));
	Response->Code = EHttpServerResponseCodes::Ok;

	TArray<FString> ContentTypeValue = { MoveTemp(ContentType) };
	Response->Headers.Add(FHttpServerHeaderKeys::CONTENT_TYPE, MoveTemp(ContentTypeValue));
	return Response;
}

TUniquePtr<FHttpServerResponse> FHttpServerResponse::Create(const TArrayView<uint8>& RawBytes, FString ContentType)
{
	TUniquePtr<FHttpServerResponse> Response = MakeUnique<FHttpServerResponse>();
	Response->Code = EHttpServerResponseCodes::Ok;
	Response->Body.Append(RawBytes.GetData(), RawBytes.Num());

	TArray<FString> ContentTypeValue = { MoveTemp(ContentType) };
	Response->Headers.Add(FHttpServerHeaderKeys::CONTENT_TYPE, MoveTemp(ContentTypeValue));
	return Response;
}

TUniquePtr<FHttpServerResponse> FHttpServerResponse::Create(int32 HttpResponseCode, const FString& ErrorCode)
{
	FString ResponseBody = FString::Printf(TEXT("{\"errorCode\": \"%s\"}"), *ErrorCode);
	auto Response = Create(ResponseBody, TEXT("application/json"));
	Response->Code = HttpResponseCode;
	return Response;
}

TUniquePtr<FHttpServerResponse> FHttpServerResponse::Ok()
{
	auto Response = MakeUnique<FHttpServerResponse>();
	Response->Code = EHttpServerResponseCodes::Ok;
	return Response;
}
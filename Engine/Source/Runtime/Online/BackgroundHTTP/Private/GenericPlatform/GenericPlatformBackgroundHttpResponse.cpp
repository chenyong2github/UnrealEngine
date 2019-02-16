// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "GenericPlatform/GenericPlatformBackgroundHttpResponse.h"
#include "GenericPlatform/GenericPlatformBackgroundHttp.h"

#include "PlatformBackgroundHttp.h"

#include "Interfaces/IHttpResponse.h"
#include "Misc/FileHelper.h"

FGenericPlatformBackgroundHttpResponse::FGenericPlatformBackgroundHttpResponse(int32 InResponseCode, const FString& InTempContentFilePath)
{
	ResponseCode = InResponseCode;
	TempContentFilePath = InTempContentFilePath;
}

FGenericPlatformBackgroundHttpResponse::FGenericPlatformBackgroundHttpResponse(FHttpRequestPtr HttpRequestIn, FHttpResponsePtr HttpResponse, bool bSuccess)
{
	//Don't bother making a response out of a failed HttpRequest
	if (bSuccess && HttpResponse.IsValid())
	{
		//Copy HTTP Response Code if we have one, otherwise it will be Unknown
		ResponseCode = HttpResponse->GetResponseCode();

		//Only try to copy results to a temp file if it as an ok result
		if (EHttpResponseCodes::IsOk(ResponseCode))
		{
			const TArray<uint8>& ContentBuffer = HttpResponse->GetContent();
			if (ContentBuffer.Num() > 0)
			{
				const FString RequestURL = HttpRequestIn->GetURL();
				const FString& FileDestination = FPlatformBackgroundHttp::GetTemporaryFilePathFromURL(RequestURL);

				FFileHelper::SaveArrayToFile(ContentBuffer, *FileDestination);
				TempContentFilePath = FileDestination;
			}
		}
	}
}
// Copyright Epic Games, Inc. All Rights Reserved.

#include "Security/WebAPIAuthentication.h"

#include "WebAPIDeveloperSettings.h"
#include "WebAPILog.h"
#include "WebAPISubsystem.h"
#include "Engine/Engine.h"
#include "Interfaces/IHttpRequest.h"
#include "Serialization/JsonSerializer.h"

UWebAPIOAuthSettings::UWebAPIOAuthSettings()
{
	SchemeName = TEXT("OAuth");
}

bool UWebAPIOAuthSettings::IsValid() const
{
	return !ClientId.IsEmpty() && !ClientSecret.IsEmpty();
}

bool FWebAPIOAuthSchemeHandler::HandleHttpRequest(TSharedPtr<IHttpRequest> InRequest, UWebAPIDeveloperSettings* InSettings)
{
	check(InSettings);

	const UWebAPIOAuthSettings* AuthSettings = GetAuthSettings(InSettings);	
	check(AuthSettings);

	if(!ensureMsgf(AuthSettings->IsValid(), TEXT("Authentication settings are missing one or more required properties.")))
	{
		return false;
	}

	if(AuthSettings->AccessToken.IsEmpty())
	{
		return false;
	}

	InRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("%s %s"), *AuthSettings->TokenType, *AuthSettings->AccessToken));

	return true;
}

bool FWebAPIOAuthSchemeHandler::HandleHttpResponse(EHttpResponseCodes::Type InResponseCode, TSharedPtr<IHttpResponse, ESPMode::ThreadSafe> InResponse, bool bInWasSuccessful, UWebAPIDeveloperSettings* InSettings)
{
	check(InSettings);

	UWebAPIOAuthSettings* AuthSettings = GetAuthSettings(InSettings);	
	check(AuthSettings);
	
	if(!ensureMsgf(AuthSettings->IsValid(), TEXT("Authentication settings are missing one or more required properties.")))
	{
		return false;
	}

	GEngine->GetEngineSubsystem<UWebAPISubsystem>()->MakeHttpRequest(TEXT("POST"), [&, AuthSettings](const TSharedPtr<IHttpRequest>& InRequest)
	{
		//Microsoft login rest URL
		FStringFormatNamedArguments UrlArgs;
		UrlArgs.Add(TEXT("TenantId"), AuthSettings->TenantId);
		const FString Url = FString::Format(*AuthSettings->AuthenticationServer, UrlArgs);

		// @todo: move!
		FString StringPayload = "grant_type=client_credentials";
		StringPayload.Append("&client_id=" + AuthSettings->ClientId);
		StringPayload.Append("&client_secret=" + AuthSettings->ClientSecret);
		StringPayload.Append("&resource=https://digitaltwins.azure.net");

		InRequest->SetURL(Url);
		InRequest->SetContentAsString(StringPayload);

		TMap<FString, FString> Headers;
		// FInternetAddr
		FString SchemeName;
		FParse::SchemeNameFromURI(*Url, SchemeName);
		FString Host = Url.Replace(*(SchemeName + TEXT("://")), TEXT(""));
		
		int32 DelimiterIdx = -1;
		if(Host.FindChar(TEXT('/'), DelimiterIdx))
		{
			Host = Host.Left(DelimiterIdx);
		}

		Headers.Add("Host", Host);
		Headers.Add("Content-Type", "application/x-www-form-urlencoded");

		for (const TPair<FString, FString>& Header : Headers)
		{
			InRequest->SetHeader(Header.Key, Header.Value);
		}
	})
	.Next([this, AuthSettings](const TTuple<FHttpResponsePtr, bool>& InResponse)
	{
		// Request failed
		if(!InResponse.Get<bool>())
		{
			return false;
		}

		const FHttpResponsePtr Response = InResponse.Get<FHttpResponsePtr>();

		FString Message;
		FString AccessTkn;
		bool bSuccess = false;

		if (Response.IsValid())
		{
			const int32 ResponseCode = Response->GetResponseCode();
			Message = Response->GetContentAsString();

			if (EHttpResponseCodes::IsOk(ResponseCode))
			{
				const TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
				TSharedPtr<FJsonObject> JsonObject;
				if(FJsonSerializer::Deserialize(JsonReader, JsonObject))
				{
					const int32 UnixTimeExpire = JsonObject->GetNumberField("expires_on");

					UE_LOG(LogWebAPI, Display, TEXT("Generate token Response success"));
					AuthSettings->TokenType = JsonObject->GetStringField("token_type");
					AuthSettings->AccessToken = JsonObject->GetStringField("access_token");
					AuthSettings->ExpiresOn =  FDateTime::FromUnixTimestamp(UnixTimeExpire);
					AuthSettings->SaveConfig();
					bSuccess = true;
				}
				else
				{
					UE_LOG(LogWebAPI, Warning, TEXT("Deserialize JSON failed"));
					UE_LOG(LogWebAPI, Error, TEXT("Authentication failed: Deserialize JSON Response token failed"));
					Message = "Deserialize JSON Response token failed:" + Message;
				}
			}
			else
				{
				UE_LOG(LogWebAPI, Error, TEXT("Authentication failed: Response not valid"));
				Message = "Response code not valid:" + Message;
			}
		}
		else
		{
			UE_LOG(LogWebAPI, Error, TEXT("Authentication failed: Generate token Response not valid"));
			Message = "Response is null";
		}

		// @todo: request retry
		//Redo all the requests that arrived while fetching new token
		/*
		if (bUpdatingToken && bSuccess)
		{
			UpdatingToken = false;
			RedoRequests();
		}
		*/

		return bSuccess;
	});

	return true;
}

UWebAPIOAuthSettings* FWebAPIOAuthSchemeHandler::GetAuthSettings(const UWebAPIDeveloperSettings* InSettings)
{
	if(!CachedAuthSettings.IsValid() && InSettings)
	{
		UWebAPIOAuthSettings* OAuthSettings = nullptr;
		InSettings->AuthenticationSettings.FindItemByClass<UWebAPIOAuthSettings>(&OAuthSettings);
		check(OAuthSettings);

		CachedAuthSettings = OAuthSettings;	
	}
	
	return CachedAuthSettings.IsValid() ? CachedAuthSettings.Get() : nullptr;
}

// Copyright Epic Games, Inc. All Rights Reserved.

#include "OnlineErrorEOS.h"

namespace UE::Online {

const TSharedRef<const IOnlineErrorDetails, ESPMode::ThreadSafe>& FOnlineErrorDetailsEOS::Get()
{
	static TSharedRef<const IOnlineErrorDetails, ESPMode::ThreadSafe> Instance = MakeShared<FOnlineErrorDetailsEOS>();
	return Instance;
}

FText FOnlineErrorDetailsEOS::GetText(const FOnlineError& OnlineError) const
{
	const EOS_EResult EosResult = EOS_EResult(OnlineError.GetValue());
	return FText::FromStringView(FStringView(UTF8_TO_TCHAR(EOS_EResult_ToString(EosResult))));
}

FString FOnlineErrorDetailsEOS::GetLogString(const FOnlineError& OnlineError) const 
{
	const EOS_EResult EosResult = EOS_EResult(OnlineError.GetValue());
	return UTF8_TO_TCHAR(EOS_EResult_ToString(EosResult));
}

bool operator==(const UE::Online::FOnlineError& OnlineError, EOS_EResult EosResult)
{
	return OnlineError.GetErrorCode() == CreateErrorCode(EosResult);
}

ErrorCodeType CreateErrorCode(EOS_EResult EosResult)
{
	return Errors::ErrorCode::Create(Errors::ErrorCode::Category::EOS_System, Errors::ErrorCode::Category::EOS, uint32(EosResult));
}

FOnlineError CreateOnlineError(EOS_EResult EosResult)
{
	return FOnlineError(CreateErrorCode(EosResult), FOnlineErrorDetailsEOS::Get());
}

} /* namespace UE::Online */

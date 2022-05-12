// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Online/OnlineError.h"
#include "Online/OnlineErrorCode.h"
#include "Online/PresenceEOS.h"

/*
*	Proper usage:
		Errors::FromEOSResult(EOSResult::EOS_PlayerDataStorage_FileSizeTooLarge);
	Certain EOS errors are predefined to have a common error parent type (see Internal_EOSWrapInner). i.e.
		(Errors::FromEOSResult(EOSResult::EOS_NoConnection) == Errors::NoConnection()) == true
*/

namespace UE::Online::Errors {

	UE_ONLINE_ERROR_CATEGORY(EOS, Engine, 0x4, "EOS")

	/** This doesn't wrap every single error in EOS, only the common ones that are strongly related to common errors */
	inline FOnlineError Internal_EOSWrapInner(FOnlineError Error, EOS_EResult Result)
	{
		switch(Result)
		{
			case EOS_EResult::EOS_Success:				return Errors::Success(Error);
			case EOS_EResult::EOS_NoConnection:			return Errors::NoConnection(Error);
			case EOS_EResult::EOS_InvalidCredentials:	return Errors::InvalidCreds(Error);
			case EOS_EResult::EOS_InvalidUser:			return Errors::InvalidUser(Error);
			case EOS_EResult::EOS_InvalidAuth:			return Errors::InvalidAuth(Error);
			case EOS_EResult::EOS_AccessDenied:			return Errors::AccessDenied(Error);
			case EOS_EResult::EOS_MissingPermissions:	return Errors::AccessDenied(Error);
			case EOS_EResult::EOS_TooManyRequests:		return Errors::TooManyRequests(Error);
			case EOS_EResult::EOS_AlreadyPending:		return Errors::AlreadyPending(Error);
			case EOS_EResult::EOS_InvalidParameters:	return Errors::InvalidParams(Error);
			case EOS_EResult::EOS_InvalidRequest:		return Errors::InvalidParams(Error);
			case EOS_EResult::EOS_UnrecognizedResponse: return Errors::InvalidResults(Error);
			case EOS_EResult::EOS_IncompatibleVersion:	return Errors::IncompatibleVersion(Error);
			case EOS_EResult::EOS_NotConfigured:		return Errors::NotConfigured(Error);
			case EOS_EResult::EOS_NotImplemented:		return Errors::NotImplemented(Error);
			case EOS_EResult::EOS_Canceled:				return Errors::Cancelled(Error);
			case EOS_EResult::EOS_NotFound:				return Errors::NotFound(Error);
			case EOS_EResult::EOS_OperationWillRetry:	return Errors::WillRetry(Error);
			case EOS_EResult::EOS_VersionMismatch:		return Errors::IncompatibleVersion(Error);
			case EOS_EResult::EOS_LimitExceeded:		return Errors::TooManyRequests(Error);
			case EOS_EResult::EOS_TimedOut:				return Errors::Timeout(Error);
		}
		return Error;
	}

	inline ErrorCodeType ErrorValueFromEOSResult(EOS_EResult Result)
	{
		return (ErrorCodeType)((uint32)Result);
	}

	inline ErrorCodeType ErrorCodeFromEOSResult(EOS_EResult Result)
	{
		return ErrorCode::Create(ErrorCode::System::ThirdPartyPlugin, ErrorCode::Category::EOS, ErrorValueFromEOSResult(Result));
	}


	inline FOnlineError FromEOSResult(EOS_EResult Result)
	{
		// these get moved around so make sure they have their own memories
		FString ErrorStr = UTF8_TO_TCHAR(EOS_EResult_ToString(Result));
		FText ErrorTxt = FText::FromString(UTF8_TO_TCHAR(EOS_EResult_ToString(Result)));
		return Internal_EOSWrapInner(FOnlineError(ErrorCodeFromEOSResult(Result), MakeShared<FOnlineErrorDetails, ESPMode::ThreadSafe>(MoveTemp(ErrorStr), MoveTemp(ErrorTxt))), Result);
	}

} // UE::Online::Errors

// These are extern'd out of the namespace for some of the Catch API that can't see these operators if they are inside
ONLINESERVICESEOS_API inline bool operator==(const UE::Online::FOnlineError& Left, EOS_EResult Right)
{
	return Left == UE::Online::Errors::ErrorCodeFromEOSResult(Right);
}

ONLINESERVICESEOS_API inline bool operator==(EOS_EResult Left, const UE::Online::FOnlineError& Right)
{
	return Right == UE::Online::Errors::ErrorCodeFromEOSResult(Left);
}

ONLINESERVICESEOS_API inline bool operator!=(const UE::Online::FOnlineError& Left, EOS_EResult Right)
{
	return !(Left == UE::Online::Errors::ErrorCodeFromEOSResult(Right));
}

ONLINESERVICESEOS_API inline bool operator!=(EOS_EResult Left, const UE::Online::FOnlineError& Right)
{
	return !(Right == UE::Online::Errors::ErrorCodeFromEOSResult(Left));
}
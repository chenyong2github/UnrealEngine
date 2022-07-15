// Copyright Epic Games, Inc. All Rights Reserved.

#include "EOSShared.h"
#include "EOSSharedTypes.h"

#include "eos_auth_types.h"
#include "eos_friends_types.h"

DEFINE_LOG_CATEGORY(LogEOSSDK);

FString LexToString(const EOS_EResult EosResult)
{
	return UTF8_TO_TCHAR(EOS_EResult_ToString(EosResult));
}

FString LexToString(const EOS_ProductUserId UserId)
{
	FString Result;

	char ProductIdString[EOS_PRODUCTUSERID_MAX_LENGTH + 1];
	ProductIdString[0] = '\0';
	int32_t BufferSize = sizeof(ProductIdString);
	if (EOS_ProductUserId_IsValid(UserId) == EOS_TRUE &&
		EOS_ProductUserId_ToString(UserId, ProductIdString, &BufferSize) == EOS_EResult::EOS_Success)
	{
		Result = UTF8_TO_TCHAR(ProductIdString);
	}

	return Result;
}

void LexFromString(EOS_ProductUserId& UserId, const TCHAR* String)
{
	UserId = EOS_ProductUserId_FromString(TCHAR_TO_UTF8(String));
}

FString LexToString(const EOS_EpicAccountId AccountId)
{
	FString Result;

	char AccountIdString[EOS_EPICACCOUNTID_MAX_LENGTH + 1];
	AccountIdString[0] = '\0';
	int32_t BufferSize = sizeof(AccountIdString);
	if (EOS_EpicAccountId_IsValid(AccountId) == EOS_TRUE &&
		EOS_EpicAccountId_ToString(AccountId, AccountIdString, &BufferSize) == EOS_EResult::EOS_Success)
	{
		Result = UTF8_TO_TCHAR(AccountIdString);
	}

	return Result;
}

const TCHAR* LexToString(const EOS_EFriendsStatus FriendStatus)
{
	switch (FriendStatus)
	{
		default: checkNoEntry(); // Intentional fall through
		case EOS_EFriendsStatus::EOS_FS_NotFriends:		return TEXT("NotFriends");
		case EOS_EFriendsStatus::EOS_FS_InviteSent:		return TEXT("InviteSent");
		case EOS_EFriendsStatus::EOS_FS_InviteReceived: return TEXT("InviteReceived");
		case EOS_EFriendsStatus::EOS_FS_Friends:		return TEXT("Friends");
	}
}

const TCHAR* LexToString(EOS_ELoginStatus LoginStatus)
{
	switch (LoginStatus)
	{
	default: checkNoEntry(); // Intentional fallthrough
	case EOS_ELoginStatus::EOS_LS_NotLoggedIn:			return TEXT("EOS_LS_NotLoggedIn");
	case EOS_ELoginStatus::EOS_LS_UsingLocalProfile:	return TEXT("EOS_LS_UsingLocalProfile");
	case EOS_ELoginStatus::EOS_LS_LoggedIn:				return TEXT("EOS_LS_LoggedIn");
	}
}

bool LexFromString(EOS_EExternalCredentialType& OutEnum, const TCHAR* InString)
{
	if (FCString::Stricmp(InString, TEXT("Steam")) == 0)
	{
		OutEnum = EOS_EExternalCredentialType::EOS_ECT_STEAM_APP_TICKET;
	}
	else if (FCString::Stricmp(InString, TEXT("PSN")) == 0)
	{
		OutEnum = EOS_EExternalCredentialType::EOS_ECT_PSN_ID_TOKEN;
	}
	else if (FCString::Stricmp(InString, TEXT("Xbox")) == 0)
	{
		OutEnum = EOS_EExternalCredentialType::EOS_ECT_XBL_XSTS_TOKEN;
	}
	else if (FCString::Stricmp(InString, TEXT("Nintendo")) == 0)
	{
		OutEnum = EOS_EExternalCredentialType::EOS_ECT_NINTENDO_ID_TOKEN;
	}
	else if (FCString::Stricmp(InString, TEXT("NSA")) == 0)
	{
		OutEnum = EOS_EExternalCredentialType::EOS_ECT_NINTENDO_NSA_ID_TOKEN;
	}
	else if (FCString::Stricmp(InString, TEXT("Apple")) == 0)
	{
		OutEnum = EOS_EExternalCredentialType::EOS_ECT_APPLE_ID_TOKEN;
	}
	else if (FCString::Stricmp(InString, TEXT("Google")) == 0)
	{
		OutEnum = EOS_EExternalCredentialType::EOS_ECT_GOOGLE_ID_TOKEN;
	}
	else
	{
		// Unknown means OpenID
		OutEnum = EOS_EExternalCredentialType::EOS_ECT_OPENID_ACCESS_TOKEN;
	}

	return true;
}

bool LexFromString(EOS_EAuthScopeFlags& OutEnum, const TCHAR* InString)
{
	if (FCString::Stricmp(InString, TEXT("BasicProfile")) == 0)
	{
		OutEnum = EOS_EAuthScopeFlags::EOS_AS_BasicProfile;
	}
	else if (FCString::Stricmp(InString, TEXT("FriendsList")) == 0)
	{
		OutEnum = EOS_EAuthScopeFlags::EOS_AS_FriendsList;
	}
	else if (FCString::Stricmp(InString, TEXT("Presence")) == 0)
	{
		OutEnum = EOS_EAuthScopeFlags::EOS_AS_Presence;
	}
	else if (FCString::Stricmp(InString, TEXT("FriendsManagement")) == 0)
	{
		OutEnum = EOS_EAuthScopeFlags::EOS_AS_FriendsManagement;
	}
	else if (FCString::Stricmp(InString, TEXT("Email")) == 0)
	{
		OutEnum = EOS_EAuthScopeFlags::EOS_AS_Email;
	}
	else if (FCString::Stricmp(InString, TEXT("NoFlags")) == 0 || FCString::Stricmp(InString, TEXT("None")) == 0)
	{
		OutEnum = EOS_EAuthScopeFlags::EOS_AS_NoFlags;
	}
	else
	{
		return false;
	}
	return true;
}

bool LexFromString(EOS_ELoginCredentialType& OutEnum, const TCHAR* InString)
{
	if (FCString::Stricmp(InString, TEXT("ExchangeCode")) == 0)
	{
		OutEnum = EOS_ELoginCredentialType::EOS_LCT_ExchangeCode;
	}
	else if (FCString::Stricmp(InString, TEXT("PersistentAuth")) == 0)
	{
		OutEnum = EOS_ELoginCredentialType::EOS_LCT_PersistentAuth;
	}
	else if (FCString::Stricmp(InString, TEXT("Password")) == 0)
	{
		OutEnum = EOS_ELoginCredentialType::EOS_LCT_Password;
	}
	else if (FCString::Stricmp(InString, TEXT("Developer")) == 0)
	{
		OutEnum = EOS_ELoginCredentialType::EOS_LCT_Developer;
	}
	else if (FCString::Stricmp(InString, TEXT("RefreshToken")) == 0)
	{
		OutEnum = EOS_ELoginCredentialType::EOS_LCT_RefreshToken;
	}
	else if (FCString::Stricmp(InString, TEXT("AccountPortal")) == 0)
	{
		OutEnum = EOS_ELoginCredentialType::EOS_LCT_AccountPortal;
	}
	else if (FCString::Stricmp(InString, TEXT("ExternalAuth")) == 0)
	{
		OutEnum = EOS_ELoginCredentialType::EOS_LCT_ExternalAuth;
	}
	else
	{
		return false;
	}
	return true;
}

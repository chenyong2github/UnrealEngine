// Copyright Epic Games, Inc. All Rights Reserved.

#include "EOSShared.h"
#include "EOSSharedTypes.h"

#include "eos_friends_types.h"

DEFINE_LOG_CATEGORY(LogEOSSDK);

bool FCallbackBase::bShouldCancelAllCallbacks = false;

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
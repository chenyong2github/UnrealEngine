// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EOS_SDK

#include "Containers/UnrealString.h"
#include "Logging/LogMacros.h"

#if defined(EOS_PLATFORM_BASE_FILE_NAME)
#include EOS_PLATFORM_BASE_FILE_NAME
#endif
#include "eos_base.h"
#include "eos_common.h"
#include "eos_version.h"

#if defined(DISABLE_EOSVOICECHAT_ENGINE)
#define WITH_EOS_RTC 0
#else
#define WITH_EOS_RTC WITH_EOS_SDK && (EOS_MAJOR_VERSION >= 1 && EOS_MINOR_VERSION >= 13)
#endif

#define EOS_ENUM_FORWARD_DECL(name) enum class name : int32_t;
EOS_ENUM_FORWARD_DECL(EOS_EFriendsStatus);
#undef EOS_ENUM_FORWARD_DECL

DECLARE_LOG_CATEGORY_EXTERN(LogEOSSDK, Log, All);

inline FString LexToString(EOS_EResult EosResult)
{
	return ANSI_TO_TCHAR(EOS_EResult_ToString(EosResult));
}

EOSSHARED_API FString LexToString(const EOS_ProductUserId UserId);
EOSSHARED_API FString LexToString(const EOS_EpicAccountId AccountId);
EOSSHARED_API const TCHAR* LexToString(const EOS_EFriendsStatus FriendStatus);

#endif // WITH_EOS_SDK
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EOS_SDK

#include "Containers/UnrealString.h"
#include "Logging/LogMacros.h"

#if defined(EOS_PLATFORM_BASE_FILE_NAME)
#include EOS_PLATFORM_BASE_FILE_NAME
#endif
#include "eos_common.h"
#include "eos_version.h"

#define WITH_EOS_RTC WITH_EOS_SDK && (EOS_MAJOR_VERSION >= 1 && EOS_MINOR_VERSION >= 13)

DECLARE_LOG_CATEGORY_EXTERN(LogEOSSDK, Log, All);

inline FString LexToString(const EOS_EResult EosResult)
{
	return ANSI_TO_TCHAR(EOS_EResult_ToString(EosResult));
}

FString EOSSHARED_API LexToString(const EOS_ProductUserId UserId);
FString EOSSHARED_API LexToString(const EOS_EpicAccountId AccountId);

#endif // WITH_EOS_SDK
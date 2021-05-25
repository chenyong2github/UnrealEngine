// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EOS_SDK

#include "Containers/UnrealString.h"
#include "Logging/LogMacros.h"

#if defined(EOS_PLATFORM_BASE_FILE_NAME)
#include EOS_PLATFORM_BASE_FILE_NAME
#endif
#include "eos_common.h"

DECLARE_LOG_CATEGORY_EXTERN(LogEOSSDK, Log, All);

inline FString LexToString(EOS_EResult EosResult)
{
	return ANSI_TO_TCHAR(EOS_EResult_ToString(EosResult));
}
#endif // WITH_EOS_SDK
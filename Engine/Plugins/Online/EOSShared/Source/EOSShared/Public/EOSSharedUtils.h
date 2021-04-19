// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/UnrealString.h"

#if defined(EOS_PLATFORM_BASE_FILE_NAME)
#include EOS_PLATFORM_BASE_FILE_NAME
#endif
#include "eos_common.h"

inline FString LexToString(EOS_EResult EosResult)
{
	return ANSI_TO_TCHAR(EOS_EResult_ToString(EosResult));
}
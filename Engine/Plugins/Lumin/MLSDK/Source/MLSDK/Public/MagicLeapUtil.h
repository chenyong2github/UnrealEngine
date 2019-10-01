// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Logging/LogMacros.h"

#define ML_LOG_ONCE(CategoryName, Verbosity, Format, ...) \
	do { static bool _logged = false; if (!_logged) { _logged = true; UE_LOG(CategoryName, Verbosity, Format, ##__VA_ARGS__); } } while (0);

namespace MagicLeap
{
	template<typename T> inline const FString EnumToString(const char* EnumName, T EnumValue)
	{
		static const FString InvalidEnum("Invalid");

		const UEnum* EnumPtr = FindObject<UEnum>(ANY_PACKAGE, UTF8_TO_TCHAR(EnumName), true);
		if (!EnumPtr)
		{
			return InvalidEnum;
		}
		return EnumPtr->GetNameStringByIndex(static_cast<int32>(EnumValue));
	}
}

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PlayerCore.h"


namespace Electra
{

	namespace ISO8601
	{
		UEMediaError ParseDateTime(FTimeValue& OutTimeValue, const FString& DateTime);
		UEMediaError ParseDuration(FTimeValue& OutTimeValue, const TCHAR* InDuration);
	}


	namespace RFC7231
	{
		UEMediaError ParseDateTime(FTimeValue& OutTimeValue, const FString& DateTime);
	}

	namespace RFC2326
	{
		bool ParseNPTTime(FTimeValue& OutTimeValue, const FString& NPTtime);
	}

	namespace UnixEpoch
	{
		bool ParseFloatString(FTimeValue& OutTimeValue, const FString& Seconds);
	}

} // namespace Electra


// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FString;

namespace UE
{
	namespace TextureUtilitiesCommon
	{
		constexpr const TCHAR* DefaultUdimRegexPattern = TEXT(R"((.+?)[._](\d{4})$)");

		TEXTUREUTILITIESCOMMON_API uint32 ParseUDIMName(const FString& Name, const FString& UdimRegexPattern, FString& OutPrefixName, FString& OutPostfixName);

		TEXTUREUTILITIESCOMMON_API int32 GetUDIMIndex(int32 BlockX, int32 BlockY);

		TEXTUREUTILITIESCOMMON_API void ExtractUDIMCoordinates(int32 UDIMIndex, int32& OutBlockX, int32& OutBlockY);
	}
}

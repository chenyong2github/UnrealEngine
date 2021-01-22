// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FString;

namespace UE
{
	namespace TextureUtilitiesCommon
	{
		TEXTUREUTILITIESCOMMON_API uint32 ParseUDIMName(const FString& Name, const FString& UdimRegexPattern, FString& OutPrefixName, FString& OutPostfixName);
	}
}

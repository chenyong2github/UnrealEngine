// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#if __UNREAL__
#include "CoreMinimal.h"
#else
#include "windows.h"
#endif

/**
 * Default names for TextureShareCore module
 */
namespace TextureShareCoreStrings
{
	namespace Default
	{
		static constexpr auto ShareName = TEXT("DefaultShareName");

		namespace ProcessName
		{
			static constexpr auto SDK = TEXT("TextureShareSDK");
			static constexpr auto UE = TEXT("UnrealEngine");
		}

		static constexpr auto ViewId = TEXT("DefaultView");
	}
};

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/EnumRange.h"

#include "CommonInputMode.generated.h"

UENUM(BlueprintType)
enum class ECommonInputMode : uint8
{
	Menu,
	Game,
	All,
	MAX
};

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/EnumRange.h"

#include "CommonInputMode.generated.h"

UENUM(BlueprintType)
enum class ECommonInputMode : uint8
{
	Menu	UMETA(Tooltip="Input is received by the UI only"),
	Game	UMETA(Tooltip="Input is received by the Game only"),
	All		UMETA(Tooltip="Input is received by UI and the Game"),
	
	MAX UMETA(Hidden)
};

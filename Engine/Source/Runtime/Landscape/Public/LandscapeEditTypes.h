// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "LandscapeEditTypes.generated.h"

UENUM()
enum class ELandscapeToolTargetType : uint8
{
	Heightmap = 0,
	Weightmap = 1,
	Visibility = 2,
	Invalid = 3 UMETA(Hidden), // only valid for LandscapeEdMode->CurrentToolTarget.TargetType
};

// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ClothPhysicalMeshData.h"
#include "ChaosWeightMapTarget.generated.h"


/** Targets for painted weight maps (aka masks). */
UENUM()
enum class EChaosWeightMapTarget : uint8
{
	None                = EWeightMapTargetCommon::None,
	MaxDistance         = EWeightMapTargetCommon::MaxDistance,
	BackstopDistance    = EWeightMapTargetCommon::BackstopDistance,
	BackstopRadius      = EWeightMapTargetCommon::BackstopRadius,
	AnimDriveMultiplier = EWeightMapTargetCommon::AnimDriveMultiplier
	// Add Chaos specific maps below this line
};

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LWComponentTypes.h"
#include "HierarchicalHashGrid2D.h"

namespace UE::Mass::ProcessorGroupNames
{
	const FName Avoidance = FName(TEXT("Avoidance"));
}

UENUM()
enum class EMassAvoidanceObstacleItemFlags : uint8
{
	None			= 0,
	HasColliderData = 1 << 0,
};
ENUM_CLASS_FLAGS(EMassAvoidanceObstacleItemFlags)

struct FMassAvoidanceObstacleItem
{
	bool operator==(const FMassAvoidanceObstacleItem& Other) const
	{
		return Entity == Other.Entity;
	}

	FLWEntity Entity;
	EMassAvoidanceObstacleItemFlags ItemFlags = EMassAvoidanceObstacleItemFlags::None;
};

typedef THierarchicalHashGrid2D<2, 4, FMassAvoidanceObstacleItem> FAvoidanceObstacleHashGrid2D;	// 2 levels of hierarchy, 4 ratio between levels
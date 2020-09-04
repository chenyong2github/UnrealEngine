// Copyright (c) Microsoft Corporation. All rights reserved.

#include "CoreMinimal.h"

#pragma once

UENUM(meta = (DeprecationMessage = "Use ESpatialInputGestureAxis."))
enum class ESpatialInputAxisGestureType : uint8
{
	None = 0,
	Manipulation = 1,
	Navigation = 2,
	NavigationRails = 3
};


UENUM()
enum class ESpatialInputSourceKind
{
	Other = 0,
	Hand = 1,
	Voice = 2,
	Controller = 3
};

UENUM()
enum class EGestureType : uint8
{
	TapGesture = 1 << 0,
	HoldGesture = 1 << 1,
	ManipulationGesture = 1 << 2,
	NavigationGesture = 1 << 3,
	NavigationRailsGesture = 1 << 4,
	NavigationGestureX = 1 << 5,
	NavigationGestureY = 1 << 6,
	NavigationGestureZ = 1 << 7
};

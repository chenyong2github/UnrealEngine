// Copyright (c) Microsoft Corporation. All rights reserved.

#include "CoreMinimal.h"

#pragma once

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
	NavigationRailsGesture = 1 << 4
};

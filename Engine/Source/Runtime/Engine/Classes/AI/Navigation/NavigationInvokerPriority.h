// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NavigationInvokerPriority.generated.h"

UENUM()
enum class ENavigationInvokerPriority : uint8
{
	VeryLow = 1,
	Low = 2,
	Default = 3,
	High = 4,
	VeryHigh = 5,
	MAX = 6
};
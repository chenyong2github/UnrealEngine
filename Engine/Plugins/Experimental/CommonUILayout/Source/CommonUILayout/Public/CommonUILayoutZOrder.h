// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CommonUILayoutZOrder.generated.h"

UENUM()
enum class ECommonUILayoutZOrder
{
	Back = 1000,
	Middle = 2000,
	Front = 3000,
	Custom = Middle, // Default value of the custom z order

	// Values used only from code and only in special cases
	CustomMin = 0		UMETA(Hidden), // Min value of the custom z order
	CustomMax = 5000	UMETA(Hidden), // Max value of the custom z order
};
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/ObjectMacros.h"

// This exists purely to guarantee a package is created, or the engine will not boot.
UENUM(BlueprintType)
enum class ECoreOnlineDummy : uint8
{
	Dummy
};
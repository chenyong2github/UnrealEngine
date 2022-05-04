// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGCommon.generated.h"

UENUM(meta = (Bitflags))
enum class EPCGChangeType : uint8
{
	None = 0,
	Cosmetic = 1 << 0,
	Settings = 1 << 1,
	Edge = 1 << 2,
	Node = 1 << 3,
	Structural = 1 << 4
};
ENUM_CLASS_FLAGS(EPCGChangeType);

namespace PCGPinConstants
{
	const FName DefaultInputLabel = TEXT("In");
	const FName DefaultOutputLabel = TEXT("Out");
}
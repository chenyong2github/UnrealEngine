// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace ArtNet
{
	/** Generate and return a UniverseID value from Net, Sub-Net and Universe values. */
	DMXPROTOCOLARTNET_API uint16 ComputeUniverseID(const uint8 Net, const uint8 Subnet, const uint8 Universe);
}

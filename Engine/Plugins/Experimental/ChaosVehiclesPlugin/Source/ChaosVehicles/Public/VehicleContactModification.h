// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PhysicsPublic.h"
#include "Chaos/CollisionResolutionTypes.h"

#if WITH_CHAOS

class FVehicleContactModificationFactory
{
public:
	static Chaos::FCollisionModifierCallback Create();
};

#endif
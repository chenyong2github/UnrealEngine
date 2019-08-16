// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Physics/ImmediatePhysics/ImmediatePhysicsShared/ImmediatePhysicsCore.h"

#if WITH_PHYSX && PHYSICS_INTERFACE_PHYSX

namespace ImmediatePhysics_PhysX
{
	using EActorType = ImmediatePhysics_Shared::EActorType;
	using EForceType = ImmediatePhysics_Shared::EForceType;
}

struct FBodyInstance;
struct FConstraintInstance;

#endif

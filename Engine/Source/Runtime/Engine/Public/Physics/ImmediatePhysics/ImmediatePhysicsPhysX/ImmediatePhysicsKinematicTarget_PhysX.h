// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_PHYSX && PHYSICS_INTERFACE_PHYSX
#include "PhysXPublic.h"

namespace ImmediatePhysics_PhysX
{
	/** Holds shape data*/
	struct FImmediateKinematicTarget
	{
		PxTransform BodyToWorld;
		bool bTargetSet;

		FImmediateKinematicTarget()
			: bTargetSet(false)
		{
		}
	};

}

#endif // WITH_PHYSX

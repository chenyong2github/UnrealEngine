// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Core.h"

namespace Chaos
{
	/**
	 * Data passed down into the collision detection functions.
	 */
	class FCollisionContext
	{
	public:
		FCollisionContext()
			: SpaceTransform(FVec3(0), FRotation3::FromIdentity())
			, bFilteringEnabled(true)
		{
		}

		// The simulation space to world-space transform (e.g., Rigid Body nodes are often simulated relative to a component or bone)
		FRigidTransform3 SpaceTransform;

		// Whether to check the shape query flags (e.g., Rigid Body nodes have already performed filtering prior to collision detection)
		bool bFilteringEnabled;
	};
}
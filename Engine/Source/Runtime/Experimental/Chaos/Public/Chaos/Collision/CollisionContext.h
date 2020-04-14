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
			, bDeferUpdate(true)
			, bAllowManifolds(false)
		{
		}

		// The simulation space to world-space transform (e.g., Rigid Body nodes are often simulated relative to a component or bone)
		FRigidTransform3 SpaceTransform;

		// Whether to check the shape query flags (e.g., Rigid Body nodes have already performed filtering prior to collision detection) [default: true]
		bool bFilteringEnabled;

		// Whether to defer constraint phi/normal calculation to the Apply step [default: true]. If true, constraints are speculatively created for each shape
		// pair passed to the narrow phase. This prevents premature culling of constraints, but it can lead to more items in the constraint graph
		// which could be undesirable in some cases (destruction?).
		bool bDeferUpdate;

		// Whether to use manifolds wheer supported [default: false]
		bool bAllowManifolds;
	};
}
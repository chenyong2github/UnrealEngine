// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Core.h"

namespace Chaos
{
	class FMultiShapePairCollisionDetector;

	/**
	 * Data passed down into the collision detection functions.
	 */
	class FCollisionContext
	{
	public:
		FCollisionContext()
			: bFilteringEnabled(true)
			, bDeferUpdate(false)
			, bAllowManifolds(false)
			, bAllowManifoldReuse(false)
			, bForceDisableCCD(false)
			, CollisionAllocator(nullptr)
		{
		}

		// Whether to check the shape query flags (e.g., Rigid Body nodes have already performed filtering prior to collision detection) [default: true]
		bool bFilteringEnabled;

		// Whether to defer constraint phi/normal calculation to the Apply step [default: true]. If true, constraints are speculatively created for each shape
		// pair passed to the narrow phase. This prevents premature culling of constraints, but it can lead to more items in the constraint graph
		// which could be undesirable in some cases (destruction?).
		bool bDeferUpdate;

		// Whether to use one-shot manifolds where supported [default: false]
		bool bAllowManifolds;

		// Whether we can reuse manifolds between frames if contacts have not moved far [default: false]
		bool bAllowManifoldReuse;

		// Force disable CCD
		bool bForceDisableCCD;

		// This is used in the older collision detection path which is still used for particles that do not flatten their implicit hierrarchies
		// into the Particle's ShapesArray. Currently this is only Clusters.
		// @todo(chaos): remove thsi from here and make it a parameter on ConstructCollisions and all inner functions.
		FMultiShapePairCollisionDetector* CollisionAllocator;
	};
}
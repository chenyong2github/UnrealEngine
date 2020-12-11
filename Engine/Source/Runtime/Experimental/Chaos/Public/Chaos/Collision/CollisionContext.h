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
			: bFilteringEnabled(true)
			, bDeferUpdate(true)
			, bAllowManifolds(false)
		{
		}

		// Whether to check the shape query flags (e.g., Rigid Body nodes have already performed filtering prior to collision detection) [default: true]
		bool bFilteringEnabled;

		// Whether to defer constraint phi/normal calculation to the Apply step [default: true]. If true, constraints are speculatively created for each shape
		// pair passed to the narrow phase. This prevents premature culling of constraints, but it can lead to more items in the constraint graph
		// which could be undesirable in some cases (destruction?).
		bool bDeferUpdate;

		// Whether to use manifolds where supported [default: false]
		bool bAllowManifolds;
	};
}
// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Core.h"

namespace Chaos
{
	/**
	 * Base class for collision detection BroadPhases.
	 *
	 * The BroadPhase produces a list of potentially overlapping particle pairs.
	 *
	 * /see FSpatialAccelerationBroadPhase
	 */
	class FBroadPhase
	{
	public:
		FBroadPhase(const FReal InThickness, const FReal InVelocityInflation)
			: BoundsThickness(InThickness)
			, BoundsThicknessVelocityInflation(InVelocityInflation)
		{
		}

		virtual ~FBroadPhase()
		{
		}

		/**
		 * BoundsThickness is the distance at which we speculatively generate constraints, not a shape padding.
		 */
		void SetBoundsThickness(const FReal Thickness)
		{
			BoundsThickness = Thickness;
		}

		/**
		 * The bounds are expanded by the velocity inflation times the velocity.
		 */
		void SetBoundsVelocityInflation(const FReal VelocityInflation)
		{
			BoundsThicknessVelocityInflation = VelocityInflation;
		}

	protected:
		FReal BoundsThickness;
		FReal BoundsThicknessVelocityInflation;
	};
}
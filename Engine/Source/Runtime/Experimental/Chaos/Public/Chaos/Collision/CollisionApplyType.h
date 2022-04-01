// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

namespace Chaos
{
	// @todo(chaos): move this enum to a constraint type independent location

	/**
	 * What solver method to use for Collision Resolution
	 */
	enum class EConstraintSolverType
	{
		None,
		StandardPbd,				// Solve for position in the Apply step with standard PBD algorithm. Calculate Implicit Velocities. Do nothing in the ApplyPushOut step (Original Chaos RBAN Solver)
		QuasiPbd,					// Solve for position in the Apply step with standard PBD algorithm. Calculate Implicit Velocities. Solve for normal velocity in the ApplyPushOut step (New Chaos Solver)
	};
}
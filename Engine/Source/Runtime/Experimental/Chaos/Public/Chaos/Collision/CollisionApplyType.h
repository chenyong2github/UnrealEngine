// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

namespace Chaos
{
	/**
	 * What solver method to use for Collision Resolution
	 */
	enum class ECollisionApplyType
	{
		None,
		Velocity,	// Solve for velocity in the Apply step 
		Position,	// Solve for position in the Apply step
	};
}
// Copyright Epic Games, Inc.All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Templates/UnrealTemplate.h"

#include "Chaos/Core.h"
#include "Chaos/Vector.h"


namespace Chaos
{
	// 
	// In principle, the low-level collision detection functions should not need to calculate the world-space contact data, but some of them
	// do some of their work in world space and already have the results we need. To avoid duplicate work, all collision detection functions
	// should also fill in the world-space data

	/**
	 * @brief Data returned by the low-level collision functions
	 * 
	 * @note In principle, the low-level collision detection functions should not need to calculate the world-space contact data, but some of them
	 * do some of their work in world space and already have the results we need. To avoid duplicate work, all collision detection functions
	 * should also fill in the world-space data
	*/
	class CHAOS_API FContactPoint
	{
	public:
		// Shape-space contact points on the two bodies
		FVec3 ShapeContactPoints[2];

		// Shape-space contact normal on the second shape with direction that points away from shape 1
		FVec3 ShapeContactNormal;

		// Contact separation (negative for overlap)
		FReal Phi;

		// Face index of the shape we hit. Only valid for Heightfield and Trimesh contact points, otherwise INDEX_NONE
		int32 FaceIndex;

		FContactPoint()
			: Phi(TNumericLimits<FReal>::Max())
			, FaceIndex(INDEX_NONE)
		{
		}

		// Whether the contact point has been set up with contact data
		bool IsSet() const { return (Phi != TNumericLimits<FReal>::Max()); }

		// Switch the shape indices. For use when calling a collision detection method which takes shape types in the opposite order to what you want.
		FContactPoint& SwapShapes()
		{
			if (IsSet())
			{
				Swap(ShapeContactPoints[0], ShapeContactPoints[1]);
				ShapeContactNormal = -ShapeContactNormal;
			}
			return *this;
		}
	};
}
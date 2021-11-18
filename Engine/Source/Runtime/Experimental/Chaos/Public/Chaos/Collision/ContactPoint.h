// Copyright Epic Games, Inc.All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Templates/UnrealTemplate.h"

#include "Chaos/Core.h"
#include "Chaos/Vector.h"


namespace Chaos
{
	// Data returned by the low-level collision functions
	class CHAOS_API FContactPoint
	{
	public:
		FVec3 ShapeContactPoints[2];	// Shape-space contact points on the two bodies, without the margin added (i.e., contact point on the core shape)
		FVec3 ShapeContactNormal;		// Shape-space contact normal relative to the NormalOwner, but with direction that always goes from body 1 to body 0

		// @todo(chaos): Collision detection output should only produce shape-space results so Location and Normal should go
		// E.g., ShapeContactNormal is used in the trianglemesh contact culling
		FVec3 Location;					// World-space contact location
		FVec3 Normal;					// World-space contact normal with direction that always goes from body 1 to body 0

		FReal Phi;						// Contact separation (negative for overlap)

		FContactPoint()
			: Normal(1, 0, 0)
			, Phi(TNumericLimits<FReal>::Max())
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
				Normal = -Normal;
			}
			return *this;
		}
	};
}
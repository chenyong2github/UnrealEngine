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
		FReal ShapeMargins[2];			// Margins used in collision detection
		int32 ContactNormalOwnerIndex;	// The shape which owns the contact normal (usually the second body, but not always for manifolds)

		// @todo(chaos): these do not need to be stored here (they can be derived from above)
		FVec3 Location;					// World-space contact location
		FVec3 Normal;					// World-space contact normal
		FReal Phi;						// Contact separation (negative for overlap)

		FContactPoint()
			: ContactNormalOwnerIndex(1)
			, Normal(1, 0, 0)
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
				Swap(ShapeMargins[0], ShapeMargins[1]);
				ShapeContactNormal = -ShapeContactNormal;
				ContactNormalOwnerIndex = (ContactNormalOwnerIndex == 0) ? 1 : 0;
				Normal = -Normal;
			}
			return *this;
		}
	};
}
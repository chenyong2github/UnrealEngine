// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Math/MathFwd.h"
#include "Math/Vector2D.h"

namespace UE
{
namespace Geometry
{

class FBoxFilter
{
private:
	// Measured in Texel units i.e., Radius of 1 <=> Radius of 1 texel side length
	float Radius = 0.5f;
	
public:
	FBoxFilter(const float RadiusIn)
		: Radius(RadiusIn)
	{		
	}

	/** @return the filter weight given a 2D distance vector, in Texel units. */
	float GetWeight(const FVector2d& Dist) const
	{
		// Returns 1 if Dist is within the region [-Radius, Radius]x[-Radius, Radius] and 0 otherwise.
		//
		// Note: Including the entire boundary of the box region is motivated by considering the filter
		// weight at the center pixel of a 3x3 image with 1 sample per texel. First, a consisten policy
		// of including/excluding the entire boundary preserves mirror symmetry under texture filtering
		// (the sample points in the corner texels all have the same contribution to the center texel),
		// and second, by making this policy inclusive means that when the Radius is exactly 1 the
		// center texel weight changes compared to the weight for 0 < Radius < 1, which seems more
		// intuitive for users.
		//
		return Dist.X >= -Radius && Dist.X <= Radius && Dist.Y >= -Radius && Dist.Y <= Radius; 
	}
};

} // end namespace UE::Geometry
} // end namespace UE
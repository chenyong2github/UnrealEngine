// Copyright Epic Games, Inc. All Rights Reserved.

#define EXPLICIT_VECTOR4 1

#include "Math/Vector.isph"

export void ApplyLongRangeConstraints(uniform FVector P[],
										const uniform FUIntVector2 Constraints[],
										const uniform float Dists[],
										const uniform float Stiffness,
										const uniform int32 NumConstraints)
{
	varying FUIntVector2 Constraint;

#if HW_GATHER_SUPPORTED == 1
	if(programIndex < NumConstraints)
	{
		Constraint = VectorLoad(&Constraints[0]);
	}
#endif

	foreach(i = 0 ... NumConstraints)
	{
#if HW_GATHER_SUPPORTED == 0
		Constraint = VectorLoad(&Constraints[extract(i, 0)]);
#endif

		const varying uint32 i1 = Constraint.V[0];
		const varying uint32 i2 = Constraint.V[1];

		const varying FVector P1 = VectorGather(&P[i1]);
		const varying FVector P2 = VectorGather(&P[i2]);

		FVector Direction = P1 - P2;

		// ComputeGeodesicDistance - This function is used for either Euclidian or Geodisic distances
		const float Distance = VectorSize(Direction);
		Direction = VectorSelect(Distance < KINDA_SMALL_NUMBER, ForwardVector, Direction / Distance);

#if HW_GATHER_SUPPORTED == 1
		if(i + programCount < NumConstraints)
		{
			Constraint = VectorLoad(&Constraints[extract(i, 0) + programCount]);
		}
#endif

		const varying float Dist = Dists[i];

		const float Offset = Distance - Dist;
		const FVector ComputedDelta = Stiffness * Offset * Direction;

		const FVector Delta = VectorSelect(Distance < Dist, ZeroVector, ComputedDelta);

		VectorScatter(&P[i2], P2 + Delta);
	}
}

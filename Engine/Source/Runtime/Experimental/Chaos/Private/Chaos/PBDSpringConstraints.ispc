// Copyright Epic Games, Inc. All Rights Reserved.

#define EXPLICIT_VECTOR4 1

#include "Math/Vector.isph"

static inline float SafeNormalize(FVector &Direction)
{
	const float Size = sqrt(VectorSizeSquared(Direction));
	Direction = VectorSelect((Size < KINDA_SMALL_NUMBER), ForwardVector, Direction / Size);
	return (Size < KINDA_SMALL_NUMBER) ? 0.f : Size;
}

export void ApplySpringConstraints(uniform FVector P[],
									const uniform FIntVector2 AllConstraints[],
									const uniform int32 ActiveConstraints[],
									const uniform float InvM[],
									const uniform float Dists[],
									const uniform float Stiffness,
									const uniform int32 NumConstraints)
{
	foreach(i = 0 ... NumConstraints)
	{
		const varying int32 ActiveConstraint = ActiveConstraints[i];
		#pragma ignore warning(perf)
		const varying FIntVector2 Constraint = AllConstraints[ActiveConstraint];
		#pragma ignore warning(perf)
		const varying float Dist = Dists[ActiveConstraint];
		const varying int32 i1 = Constraint.V[0];
		const varying int32 i2 = Constraint.V[1];

		#pragma ignore warning(perf)
		const varying float M1 = InvM[i1];
		#pragma ignore warning(perf)
		const varying float M2 = InvM[i2];

		#pragma ignore warning(perf)
		const varying FVector P1 = P[i1];
		#pragma ignore warning(perf)
		const varying FVector P2 = P[i2];

		const float CombinedMass = M2 + M1;

		FVector Direction = P1 - P2;
		const float Distance = SafeNormalize(Direction);

		const FVector InnerDelta = (Distance - Dist) * Direction;
		const FVector ComputedDelta = Stiffness * InnerDelta / CombinedMass;

		const FVector Delta = VectorSelect((M2 == 0 && M1 == 0), ZeroVector, ComputedDelta);

		if (M1 > 0)
		{
			#pragma ignore warning(perf)
			P[i1] = P1 - (M1 * Delta);
		}
		if (M2 > 0)
		{
			#pragma ignore warning(perf)
			P[i2] = P2 + (M2 * Delta);
		}
	}
}

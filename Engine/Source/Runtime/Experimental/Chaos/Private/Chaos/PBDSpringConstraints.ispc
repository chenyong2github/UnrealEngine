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
	varying int32 ActiveConstraint;
	varying FIntVector2 Constraint;

#if HW_GATHER_SUPPORTED == 1
	if(programIndex < NumConstraints)
	{
		ActiveConstraint = ActiveConstraints[programIndex];
		#pragma ignore warning(perf)
		Constraint = AllConstraints[ActiveConstraint];
	}
#endif

	foreach(i = 0 ... NumConstraints)
	{
#if HW_GATHER_SUPPORTED == 0
		ActiveConstraint = ActiveConstraints[i];
		#pragma ignore warning(perf)
		Constraint = AllConstraints[ActiveConstraint];
#endif

		const varying int32 i1 = Constraint.V[0];
		const varying int32 i2 = Constraint.V[1];

		const varying FVector P1 = VectorGather(&P[i1]);
		const varying FVector P2 = VectorGather(&P[i2]);

		FVector Direction = P1 - P2;
		const float Distance = SafeNormalize(Direction);

		#pragma ignore warning(perf)
		const varying float Dist = Dists[ActiveConstraint];

#if HW_GATHER_SUPPORTED == 1
		if(i + programCount < NumConstraints)
		{
			ActiveConstraint = ActiveConstraints[i+programCount];
			#pragma ignore warning(perf)
			Constraint = AllConstraints[ActiveConstraint];
		}
#endif

		const FVector InnerDelta = (Distance - Dist) * Direction;

		#pragma ignore warning(perf)
		const varying float InvM1 = InvM[i1];
		#pragma ignore warning(perf)
		const varying float InvM2 = InvM[i2];

		const float CombinedInvMass = InvM2 + InvM1;
		const FVector ComputedDelta = Stiffness * InnerDelta / CombinedInvMass;

		const FVector Delta = VectorSelect((InvM2 == 0 && InvM1 == 0), ZeroVector, ComputedDelta);

		if (InvM1 > 0)
		{
			VectorScatter(&P[i1], P1 - (InvM1 * Delta));
		}
		if (InvM2 > 0)
		{
			VectorScatter(&P[i2], P2 + (InvM2 * Delta));
		}
	}
}

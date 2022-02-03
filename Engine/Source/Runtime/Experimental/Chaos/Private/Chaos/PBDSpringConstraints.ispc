// Copyright Epic Games, Inc. All Rights Reserved.

#define EXPLICIT_VECTOR4 1

#include "Math/Vector.isph"

static inline float SafeNormalize(FVector3f &Direction)
{
	const float SizeSquared = VectorSizeSquared(Direction);
	const float Size = sqrt(SizeSquared);
	Direction = VectorSelect((SizeSquared < FLOAT_KINDA_SMALL_NUMBER), FloatForwardVector, Direction / Size);
	return (SizeSquared < FLOAT_KINDA_SMALL_NUMBER) ? FLOAT_ZERO : Size;
}

export void ApplySpringConstraints(uniform FVector3f P[],
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

		const varying FVector3f P1 = VectorGather(&P[i1]);
		const varying FVector3f P2 = VectorGather(&P[i2]);

		FVector3f Direction = P1 - P2;
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

		const FVector3f InnerDelta = (Distance - Dist) * Direction;

		#pragma ignore warning(perf)
		const varying float InvM1 = InvM[i1];
		#pragma ignore warning(perf)
		const varying float InvM2 = InvM[i2];

		const float CombinedInvMass = InvM2 + InvM1;
		const FVector3f ComputedDelta = Stiffness * InnerDelta / CombinedInvMass;

		const FVector3f Delta = VectorSelect((InvM2 == FLOAT_ZERO && InvM1 == FLOAT_ZERO), FloatZeroVector, ComputedDelta);

		if (InvM1 > FLOAT_ZERO)
		{
			VectorScatter(&P[i1], P1 - (InvM1 * Delta));
		}
		if (InvM2 > FLOAT_ZERO)
		{
			VectorScatter(&P[i2], P2 + (InvM2 * Delta));
		}
	}
}

export void ApplySpringConstraintsWithWeightMaps(uniform FVector3f P[],
									const uniform FIntVector2 AllConstraints[],
									const uniform int32 ActiveConstraints[],
									const uniform float InvM[],
									const uniform float Dists[],
									const uniform uint8 StiffnessIndices[],
									const uniform float StiffnessTable[],
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

		const varying FVector3f P1 = VectorGather(&P[i1]);
		const varying FVector3f P2 = VectorGather(&P[i2]);

		FVector3f Direction = P1 - P2;
		const float Distance = SafeNormalize(Direction);

		#pragma ignore warning(perf)
		const varying float Dist = Dists[ActiveConstraint];

		#pragma ignore warning(perf)
		const varying uint8 StiffnessIndex = StiffnessIndices[ActiveConstraint];

#if HW_GATHER_SUPPORTED == 1
		if(i + programCount < NumConstraints)
		{
			ActiveConstraint = ActiveConstraints[i+programCount];
			#pragma ignore warning(perf)
			Constraint = AllConstraints[ActiveConstraint];
		}
#endif

		const FVector3f InnerDelta = (Distance - Dist) * Direction;

		#pragma ignore warning(perf)
		const varying float InvM1 = InvM[i1];
		#pragma ignore warning(perf)
		const varying float InvM2 = InvM[i2];

		#pragma ignore warning(perf)
		const varying float Stiffness = StiffnessTable[StiffnessIndex];

		const float CombinedInvMass = InvM2 + InvM1;
		const FVector3f ComputedDelta = Stiffness * InnerDelta / CombinedInvMass;

		const FVector3f Delta = VectorSelect((InvM2 == FLOAT_ZERO && InvM1 == FLOAT_ZERO), FloatZeroVector, ComputedDelta);

		if (InvM1 > FLOAT_ZERO)
		{
			VectorScatter(&P[i1], P1 - (InvM1 * Delta));
		}
		if (InvM2 > FLOAT_ZERO)
		{
			VectorScatter(&P[i2], P2 + (InvM2 * Delta));
		}
	}
}

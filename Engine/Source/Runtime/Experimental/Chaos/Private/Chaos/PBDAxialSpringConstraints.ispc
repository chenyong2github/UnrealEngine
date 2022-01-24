// Copyright Epic Games, Inc. All Rights Reserved.

#define EXPLICIT_VECTOR4 1

#include "Math/Vector.isph"

export void ApplyAxialSpringConstraints(uniform FVector P[],
										const uniform FIntVector AllConstraints[],
										const uniform int32 ActiveConstraints[],
										const uniform FReal InvM[],
										const uniform FReal Barys[],
										const uniform FReal Dists[],
										const uniform FReal Stiffness,
										const uniform int32 NumConstraints)
{
	varying int32 ActiveConstraint;
	varying FIntVector Constraint;

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

		#pragma ignore warning(perf)
		const varying FReal Bary = Barys[ActiveConstraint];
		#pragma ignore warning(perf)
		const varying FReal Dist = Dists[ActiveConstraint];

		const varying int32 i1 = Constraint.V[0];
		const varying int32 i2 = Constraint.V[1];
		const varying int32 i3 = Constraint.V[2];

		const varying FVector P2 = VectorGather(&P[i2]);
		const varying FVector P3 = VectorGather(&P[i3]);

#if HW_GATHER_SUPPORTED == 1
		if(i + programCount < NumConstraints)
		{
			ActiveConstraint = ActiveConstraints[i+programCount];
			#pragma ignore warning(perf)
			Constraint = AllConstraints[ActiveConstraint];
		}
#endif

		const varying FVector P1 = VectorGather(&P[i1]);

		const FVector P0 = (P2 - P3) * Bary + P3;
		const FVector Difference = P1 - P0;

		const FReal Distance = VectorSize(Difference);
		const FReal Multiplier = 2 / (max(Bary, 1 - Bary) + 1);

		#pragma ignore warning(perf)
		const varying FReal M1 = InvM[i1];
		#pragma ignore warning(perf)
		const varying FReal M2 = InvM[i2];
		#pragma ignore warning(perf)
		const varying FReal M3 = InvM[i3];

		const FVector Direction = Difference / Distance;
		const FVector InnerDelta = (Distance - Dist) * Direction;

		const FReal PInvMass = M3 * (1 - Bary) + M2 * Bary;
		const FReal CombinedInvMass = PInvMass + M1;
		const FVector ComputedDelta = Stiffness * InnerDelta / CombinedInvMass;

		const FVector Delta = VectorSelect(((M1 == 0 && PInvMass == 0) || Distance <= SMALL_NUMBER), ZeroVector, ComputedDelta);

		if (M1 > 0)
		{
			VectorScatter(&P[i1], P1 - (Multiplier * M1 * Delta));
		}
		if (M2)
		{
			VectorScatter(&P[i2], P2 + (Multiplier * M2 * Bary * Delta));
		}
		if (M3)
		{
			VectorScatter(&P[i3], P3 + (Multiplier * M3 * (1 - Bary) * Delta));
		}
	}
}

export void ApplyAxialSpringConstraintsWithWeightMaps(uniform FVector P[],
										const uniform FIntVector AllConstraints[],
										const uniform int32 ActiveConstraints[],
										const uniform FReal InvM[],
										const uniform FReal Barys[],
										const uniform FReal Dists[],
										const uniform uint8 StiffnessIndices[],
										const uniform FReal StiffnessTable[],
										const uniform int32 NumConstraints)
{
	varying int32 ActiveConstraint;
	varying FIntVector Constraint;

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

		#pragma ignore warning(perf)
		const varying FReal Bary = Barys[ActiveConstraint];
		#pragma ignore warning(perf)
		const varying FReal Dist = Dists[ActiveConstraint];
		#pragma ignore warning(perf)
		const varying uint8 StiffnessIndex = StiffnessIndices[ActiveConstraint];

		const varying int32 i1 = Constraint.V[0];
		const varying int32 i2 = Constraint.V[1];
		const varying int32 i3 = Constraint.V[2];

		const varying FVector P2 = VectorGather(&P[i2]);
		const varying FVector P3 = VectorGather(&P[i3]);

#if HW_GATHER_SUPPORTED == 1
		if(i + programCount < NumConstraints)
		{
			ActiveConstraint = ActiveConstraints[i+programCount];
			#pragma ignore warning(perf)
			Constraint = AllConstraints[ActiveConstraint];
		}
#endif

		const varying FVector P1 = VectorGather(&P[i1]);

		const FVector P0 = (P2 - P3) * Bary + P3;
		const FVector Difference = P1 - P0;

		const FReal Distance = VectorSize(Difference);
		const FReal Multiplier = 2 / (max(Bary, 1 - Bary) + 1);

		#pragma ignore warning(perf)
		const varying FReal M1 = InvM[i1];
		#pragma ignore warning(perf)
		const varying FReal M2 = InvM[i2];
		#pragma ignore warning(perf)
		const varying FReal M3 = InvM[i3];

		#pragma ignore warning(perf)
		const varying FReal Stiffness = StiffnessTable[StiffnessIndex];

		const FVector Direction = Difference / Distance;
		const FVector InnerDelta = (Distance - Dist) * Direction;

		const FReal PInvMass = M3 * (1 - Bary) + M2 * Bary;
		const FReal CombinedInvMass = PInvMass + M1;
		const FVector ComputedDelta = Stiffness * InnerDelta / CombinedInvMass;

		const FVector Delta = VectorSelect(((M1 == 0 && PInvMass == 0) || Distance <= SMALL_NUMBER), ZeroVector, ComputedDelta);

		if (M1 > 0)
		{
			VectorScatter(&P[i1], P1 - (Multiplier * M1 * Delta));
		}
		if (M2)
		{
			VectorScatter(&P[i2], P2 + (Multiplier * M2 * Bary * Delta));
		}
		if (M3)
		{
			VectorScatter(&P[i3], P3 + (Multiplier * M3 * (1 - Bary) * Delta));
		}
	}
}

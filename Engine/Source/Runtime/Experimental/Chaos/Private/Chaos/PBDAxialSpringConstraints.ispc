// Copyright Epic Games, Inc. All Rights Reserved.

#define EXPLICIT_VECTOR4 1

#include "Math/Vector.isph"

export void ApplyAxialSpringConstraints(uniform FVector3f P[],
										const uniform FIntVector Constraints[],
										const uniform float InvM[],
										const uniform float Barys[],
										const uniform float Dists[],
										const uniform float Stiffness,
										const uniform int32 NumConstraints)
{

	foreach(i = 0 ... NumConstraints)
	{
		const varying FIntVector Constraint = VectorLoad(&Constraints[extract(i,0)]);

		const varying float Bary = Barys[i];
		const varying float Dist = Dists[i];

		const varying int32 i1 = Constraint.V[0];
		const varying int32 i2 = Constraint.V[1];
		const varying int32 i3 = Constraint.V[2];

		const varying FVector3f P2 = VectorGather(&P[i2]);
		const varying FVector3f P3 = VectorGather(&P[i3]);

		const varying FVector3f P1 = VectorGather(&P[i1]);

		const FVector3f P0 = (P2 - P3) * Bary + P3;
		const FVector3f Difference = P1 - P0;

		const float Distance = VectorSize(Difference);
		const float Multiplier = 2 / (max(Bary, 1 - Bary) + 1);

		#pragma ignore warning(perf)
		const varying float M1 = InvM[i1];
		#pragma ignore warning(perf)
		const varying float M2 = InvM[i2];
		#pragma ignore warning(perf)
		const varying float M3 = InvM[i3];

		const FVector3f Direction = Difference / Distance;
		const FVector3f InnerDelta = (Distance - Dist) * Direction;

		const float PInvMass = M3 * (1 - Bary) + M2 * Bary;
		const float CombinedInvMass = PInvMass + M1;
		const FVector3f ComputedDelta = Stiffness * InnerDelta / CombinedInvMass;

		const FVector3f Delta = VectorSelect(((M1 == 0 && PInvMass == 0) || Distance <= FLOAT_SMALL_NUMBER), FloatZeroVector, ComputedDelta);

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

export void ApplyAxialSpringConstraintsWithWeightMaps(uniform FVector3f P[],
										const uniform FIntVector Constraints[],
										const uniform float InvM[],
										const uniform float Barys[],
										const uniform float Dists[],
										const uniform uint8 StiffnessIndices[],
										const uniform float StiffnessTable[],
										const uniform int32 NumConstraints)
{

	foreach(i = 0 ... NumConstraints)
	{
		const varying FIntVector Constraint = VectorLoad(&Constraints[extract(i,0)]);

		const varying float Bary = Barys[i];
		const varying float Dist = Dists[i];
		const varying uint8 StiffnessIndex = StiffnessIndices[i];

		const varying int32 i1 = Constraint.V[0];
		const varying int32 i2 = Constraint.V[1];
		const varying int32 i3 = Constraint.V[2];

		const varying FVector3f P2 = VectorGather(&P[i2]);
		const varying FVector3f P3 = VectorGather(&P[i3]);

		const varying FVector3f P1 = VectorGather(&P[i1]);

		const FVector3f P0 = (P2 - P3) * Bary + P3;
		const FVector3f Difference = P1 - P0;

		const float Distance = VectorSize(Difference);
		const float Multiplier = 2 / (max(Bary, 1 - Bary) + 1);

		#pragma ignore warning(perf)
		const varying float M1 = InvM[i1];
		#pragma ignore warning(perf)
		const varying float M2 = InvM[i2];
		#pragma ignore warning(perf)
		const varying float M3 = InvM[i3];

		#pragma ignore warning(perf)
		const varying float Stiffness = StiffnessTable[StiffnessIndex];

		const FVector3f Direction = Difference / Distance;
		const FVector3f InnerDelta = (Distance - Dist) * Direction;

		const float PInvMass = M3 * (1 - Bary) + M2 * Bary;
		const float CombinedInvMass = PInvMass + M1;
		const FVector3f ComputedDelta = Stiffness * InnerDelta / CombinedInvMass;

		const FVector3f Delta = VectorSelect(((M1 == 0 && PInvMass == 0) || Distance <= FLOAT_SMALL_NUMBER), FloatZeroVector, ComputedDelta);

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

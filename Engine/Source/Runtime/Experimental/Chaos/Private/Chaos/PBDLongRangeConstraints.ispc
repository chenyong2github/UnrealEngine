// Copyright Epic Games, Inc. All Rights Reserved.

#define EXPLICIT_VECTOR4 1

#include "Math/Vector.isph"

struct FTether
{
	int32 Start;
	int32 End;
	float RefLength;
};

static inline varying FTether LoadTetherHelper(const uniform FTether *uniform SrcPtr)
{
	varying FTether Tether;
	const varying FIntVector Loader = VectorLoad((const uniform FIntVector *uniform)SrcPtr);
	Tether.Start = Loader.V[0];
	Tether.End = Loader.V[1];
	Tether.RefLength = floatbits(Loader.V[2]);
	return Tether;
}

export void ApplyLongRangeConstraints(uniform FVector P[],
										const uniform FTether Tethers[],
										const uniform float Stiffness,
										const uniform int32 NumConstraints)
{
	varying FTether Tether;

#if HW_GATHER_SUPPORTED == 1
	if(programIndex < NumConstraints)
	{
		Tether = LoadTetherHelper(&Tethers[0]);
	}
#endif

	foreach(i = 0 ... NumConstraints)
	{
#if HW_GATHER_SUPPORTED == 0
		Tether = LoadTetherHelper(&Tethers[extract(i, 0)]);
#endif
		const varying int32 i1 = Tether.Start;
		const varying int32 i2 = Tether.End;
		const varying float RefLength = Tether.RefLength;
		const varying FVector P1 = VectorGather(&P[i1]);
		const varying FVector P2 = VectorGather(&P[i2]);

#if HW_GATHER_SUPPORTED == 1
		if(i + programCount < NumConstraints)
		{
			Tether = LoadTetherHelper(&Tethers[extract(i, 0) + programCount]);
		}
#endif

		FVector Direction = P1 - P2;
		const float Length = VectorSize(Direction);
		Direction = VectorSelect(Length < KINDA_SMALL_NUMBER, ForwardVector, Direction / Length);

		const float Offset = Length - RefLength;
		const FVector ComputedDelta = Stiffness * Offset * Direction;

		const FVector Delta = VectorSelect(Offset < 0.f, ZeroVector, ComputedDelta);

		VectorScatter(&P[i2], P2 + Delta);
	}
}

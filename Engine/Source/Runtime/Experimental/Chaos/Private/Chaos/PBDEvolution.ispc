// Copyright Epic Games, Inc. All Rights Reserved.

#define EXPLICIT_VECTOR4 1

#include "Math/Vector.isph"

export void PostIterationUpdates(uniform FVector ParticlesV[],
								uniform FVector ParticlesX[],
								const uniform FVector ParticlesP[],
								const uniform FReal Dt,
								const uniform int32 Offset,
								const uniform int32 Range)
{
	const uniform int RangeBase = (Range - Offset) & ~(programCount-1);
	const varying FReal vDt = Dt;

	for(uniform int32 Index = Offset; Index < Offset + RangeBase; Index+=programCount)
	{
		uniform FReal *uniform VPtr = (uniform FReal *uniform)&ParticlesV[Index];
		uniform FReal *uniform XPtr = (uniform FReal *uniform)&ParticlesX[Index];
		const uniform FReal *uniform PPtr = (uniform FReal *uniform)&ParticlesP[Index];

		const varying FReal P0 = PPtr[programIndex];
		const varying FReal P1 = PPtr[programCount + programIndex];
		const varying FReal P2 = PPtr[2*programCount + programIndex];

		const varying FReal X0 = XPtr[programIndex];
		const varying FReal X1 = XPtr[programCount + programIndex];
		const varying FReal X2 = XPtr[2*programCount + programIndex];

		VPtr[programIndex] = (P0 - X0) / vDt;
		VPtr[programCount + programIndex] = (P1 - X1) / vDt;
		VPtr[2*programCount + programIndex] = (P2 - X2) / vDt;

		XPtr[programIndex] = P0;
		XPtr[programCount + programIndex] = P1;
		XPtr[2*programCount + programIndex] = P2;
	}

	for(uniform int32 Index = Offset + RangeBase; Index < Range; ++Index)
	{
		const uniform FVector P = ParticlesP[Index];
		const uniform FVector X = ParticlesX[Index];

		ParticlesV[Index] = (P - X) / Dt;
		ParticlesX[Index] = P;
	}
}

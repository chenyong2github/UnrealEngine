// Copyright Epic Games, Inc. All Rights Reserved.

#define EXPLICIT_VECTOR4 1

#include "Math/Vector.isph"

export void PostIterationUpdates(uniform FVector3f ParticlesV[],
								uniform FVector3f ParticlesX[],
								const uniform FVector3f ParticlesP[],
								const uniform float Dt,
								const uniform int32 Offset,
								const uniform int32 Range)
{
	const uniform int RangeBase = (Range - Offset) & ~(programCount-1);
	const varying float vDt = Dt;

	for(uniform int32 Index = Offset; Index < Offset + RangeBase; Index+=programCount)
	{
		uniform float *uniform VPtr = (uniform float *uniform)&ParticlesV[Index];
		uniform float *uniform XPtr = (uniform float *uniform)&ParticlesX[Index];
		const uniform float *uniform PPtr = (uniform float *uniform)&ParticlesP[Index];

		const varying float P0 = PPtr[programIndex];
		const varying float P1 = PPtr[programCount + programIndex];
		const varying float P2 = PPtr[2*programCount + programIndex];

		const varying float X0 = XPtr[programIndex];
		const varying float X1 = XPtr[programCount + programIndex];
		const varying float X2 = XPtr[2*programCount + programIndex];

		VPtr[programIndex] = (P0 - X0) / vDt;
		VPtr[programCount + programIndex] = (P1 - X1) / vDt;
		VPtr[2*programCount + programIndex] = (P2 - X2) / vDt;

		XPtr[programIndex] = P0;
		XPtr[programCount + programIndex] = P1;
		XPtr[2*programCount + programIndex] = P2;
	}

	for(uniform int32 Index = Offset + RangeBase; Index < Range; ++Index)
	{
		const uniform FVector3f P = ParticlesP[Index];
		const uniform FVector3f X = ParticlesX[Index];

		ParticlesV[Index] = (P - X) / Dt;
		ParticlesX[Index] = P;
	}
}

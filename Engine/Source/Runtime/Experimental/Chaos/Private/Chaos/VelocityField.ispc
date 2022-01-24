// Copyright Epic Games, Inc. All Rights Reserved.

#define EXPLICIT_VECTOR4 1

#include "Math/Vector.isph"

static const FReal OneThird = 1.0/3.0;

static inline FReal SafeNormalize(FVector &Direction)
{
	const FReal Size = sqrt(VectorSizeSquared(Direction));
	Direction = VectorSelect((Size < KINDA_SMALL_NUMBER), ForwardVector, Direction / Size);
	return (Size < KINDA_SMALL_NUMBER) ? 0 : Size;
}

export void UpdateField(uniform FVector Forces[],
								const uniform FIntVector Elements[],
								const uniform FVector V[],
								const uniform FVector X[],
								const uniform FVector& Velocity,
								const uniform FReal QuarterRho,
								const uniform FReal Cd,
								const uniform FReal Cl,
								const uniform int32 NumElements)
{
	varying FIntVector Element;
	uniform int Offset = 0;

#if HW_GATHER_SUPPORTED == 1
	if(programIndex < NumElements)
	{
		Element = VectorLoad(&Elements[Offset]);
	}
#endif

	foreach(ElementIndex = 0 ... NumElements)
	{
#if HW_GATHER_SUPPORTED == 0
		Element = VectorLoad(&Elements[Offset]);
#endif

		const FVector X0 = VectorGather(&X[Element.V[0]]);
		const FVector X1 = VectorGather(&X[Element.V[1]]);
		const FVector X2 = VectorGather(&X[Element.V[2]]);

		// Calculate the normal and the area of the surface exposed to the flow
		FVector N = VectorCross(X1 - X0, X2 - X0);
		const FReal DoubleArea = SafeNormalize(N);

		const FVector V0 = VectorGather(&V[Element.V[0]]);
		const FVector V1 = VectorGather(&V[Element.V[1]]);
		const FVector V2 = VectorGather(&V[Element.V[2]]);

		// Calculate the direction and the relative velocity of the triangle to the flow
		const FVector SurfaceVelocity = OneThird * (V0 + V1 + V2);
		const FVector RelVelocity = Velocity - SurfaceVelocity;

		// Set the aerodynamic forces
		const FReal VDotN = VectorDot(RelVelocity, N);
		const FReal VSquare = VectorDot(RelVelocity, RelVelocity);

#if HW_GATHER_SUPPORTED == 1
		if(ElementIndex + programCount < NumElements)
		{
			Element = VectorLoad(&Elements[Offset + programCount]);
		}
#endif

		const FVector EvenForce = (Cd - Cl) * VDotN * RelVelocity + Cl * VSquare * N;
		const FVector OddForce = (Cl - Cd) * VDotN * RelVelocity - Cl * VSquare * N;

		const FVector Force = QuarterRho * DoubleArea * VectorSelect(VDotN >= 0, EvenForce, OddForce);

		VectorStore(&Forces[Offset], Force);

		Offset += programCount;
	}
}

export void UpdateFieldWithWeightMaps(uniform FVector Forces[],
								const uniform FIntVector Elements[],
								const uniform FVector V[],
								const uniform FVector X[],
								const uniform FVector2 Multipliers[],
								const uniform FVector& Velocity,
								const uniform FReal QuarterRho,
								const uniform FReal DragBase,
								const uniform FReal DragRange,
								const uniform FReal LiftBase,
								const uniform FReal LiftRange,
								const uniform int32 NumElements)
{
	varying FIntVector Element;
	uniform int Offset = 0;

#if HW_GATHER_SUPPORTED == 1
	if(programIndex < NumElements)
	{
		Element = VectorLoad(&Elements[Offset]);
	}
#endif

	foreach(ElementIndex = 0 ... NumElements)
	{
#if HW_GATHER_SUPPORTED == 0
		Element = VectorLoad(&Elements[Offset]);
#endif
		const FVector X0 = VectorGather(&X[Element.V[0]]);
		const FVector X1 = VectorGather(&X[Element.V[1]]);
		const FVector X2 = VectorGather(&X[Element.V[2]]);

		const FVector2 Multiplier = VectorGather(&Multipliers[ElementIndex]);

		// Calculate the normal and the area of the surface exposed to the flow
		FVector N = VectorCross(X1 - X0, X2 - X0);
		const FReal DoubleArea = SafeNormalize(N);

		const FVector V0 = VectorGather(&V[Element.V[0]]);
		const FVector V1 = VectorGather(&V[Element.V[1]]);
		const FVector V2 = VectorGather(&V[Element.V[2]]);

		// Calculate the direction and the relative velocity of the triangle to the flow
		const FVector SurfaceVelocity = OneThird * (V0 + V1 + V2);
		const FVector RelVelocity = Velocity - SurfaceVelocity;

		// Set the aerodynamic forces
		const FReal VDotN = VectorDot(RelVelocity, N);
		const FReal VSquare = VectorDot(RelVelocity, RelVelocity);

		const FReal Cd = DragBase + DragRange * Multiplier.V[0];
		const FReal Cl = LiftBase + LiftRange * Multiplier.V[1];

#if HW_GATHER_SUPPORTED == 1
		if(ElementIndex + programCount < NumElements)
		{
			Element = VectorLoad(&Elements[Offset + programCount]);
		}
#endif

		const FVector EvenForce = (Cd - Cl) * VDotN * RelVelocity + Cl * VSquare * N;
		const FVector OddForce = (Cl - Cd) * VDotN * RelVelocity - Cl * VSquare * N;

		const FVector Force = QuarterRho * DoubleArea * VectorSelect(VDotN >= 0, EvenForce, OddForce);

		VectorStore(&Forces[Offset], Force);

		Offset += programCount;
	}
}

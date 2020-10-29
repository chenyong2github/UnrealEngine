// Copyright Epic Games, Inc. All Rights Reserved.

#define EXPLICIT_VECTOR4 1
#define EXPLICIT_MATRIX 1

#include "Math/Vector.isph"
#include "Chaos/Matrix33.isph"

static inline uniform bool IsFinite(uniform float A)
{
	return (intbits(A) & 0x7F800000U) != 0x7F800000U;
}

export void UpdatePositionBasedState(uniform FVector& MXcm,
									uniform FVector& MVcm,
									uniform FVector& MOmega,
									const uniform FVector X[],
									const uniform FVector V[],
									const uniform float M[],
									const uniform float InvM[],
									const uniform int Offset,
									const uniform int32 Range)
{
	varying FVector Xcm = ZeroVector;
	varying FVector Vcm = ZeroVector;
	varying float Mcm = 0.0f;

	foreach(Index = Offset ... Range)
	{
		const varying float IM = InvM[Index];

		if(all(!IM))
		{
			continue;
		}

		if(!IM)
		{
			continue;
		}

		const varying FVector MX = VectorLoad(&X[extract(Index, 0)]);
		const varying FVector MV = VectorLoad(&V[extract(Index, 0)]);
		const varying float MM = M[Index];

		Xcm = Xcm + (MX * MM);
		Vcm = Vcm + (MV * MM);
		Mcm += MM;
	}

	MXcm = VectorReduceAdd(Xcm);
	MVcm = VectorReduceAdd(Vcm);
	uniform float MMcm = reduce_add(Mcm);

	if (MMcm != 0.0f)
	{
		MXcm = MXcm / MMcm;
		MVcm = MVcm / MMcm;
	}

	varying FVector VL = ZeroVector;
	varying FMatrix33 VI = Matrix33Zero;

	foreach(Index = Offset ... Range)
	{
		const varying float IM = InvM[Index];

		if(all(!IM))
		{
			continue;
		}

		if(!IM)
		{
			continue;
		}

		const varying FVector MX = VectorLoad(&X[extract(Index, 0)]);
		const varying FVector MV = VectorLoad(&V[extract(Index, 0)]);
		const varying float MM = M[Index];

		const varying FVector VX = MX - MXcm;
		VL = VL + VectorCross(VX, MM * MV);

		const varying FMatrix33 Mat = SetMatrix33(0, VX.V[2], -VX.V[1], -VX.V[2], 0, VX.V[0], VX.V[1], -VX.V[0], 0);
		VI = AddAB(VI, Multiply(MultiplyABt(Mat, Mat), MM));
	}

	const uniform FVector L = VectorReduceAdd(VL);
	const uniform FMatrix33 I = MatrixReduceAdd(VI);

	const uniform float Det = MatrixDeterminant(I);

	MOmega = VectorSelect(Det < SMALL_NUMBER || !IsFinite(Det), ZeroVector, MatrixInverseTransformVector(I, L)); // Calls FMatrix::InverseFast(), which tests against SMALL_NUMBER
}

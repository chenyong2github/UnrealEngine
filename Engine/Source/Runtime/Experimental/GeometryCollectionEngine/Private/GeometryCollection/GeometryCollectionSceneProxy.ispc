// Copyright Epic Games, Inc. All Rights Reserved.

#include "Math/Vector.isph"
#include "Math/Matrix.isph"

// Calculate homogeneous transform. W component assumed to be 1.0
inline static FVector VectorTransformVector(const FVector &P, const uniform FMatrix &M)
{
	FVector4 VTempX, VTempY, VTempZ, VTempW;

	// Splat x,y,z and w
	VTempX = SetVector4(P.V[0], P.V[0], P.V[0], P.V[0]);
	VTempY = SetVector4(P.V[1], P.V[1], P.V[1], P.V[1]);
	VTempZ = SetVector4(P.V[2], P.V[2], P.V[2], P.V[2]);
	VTempW = SetVector4(1.0f, 1.0f, 1.0f, 1.0f);

	// Mul by the matrix
	VTempX = VTempX * SetVector4(M.M[0], M.M[1], M.M[2], M.M[3]);
	VTempY = VTempY * SetVector4(M.M[4], M.M[5], M.M[6], M.M[7]);
	VTempZ = VTempZ * SetVector4(M.M[8], M.M[9], M.M[10], M.M[11]);
	VTempW = VTempW * SetVector4(M.M[12], M.M[13], M.M[14], M.M[15]);

	// Add them all together
	VTempX = VTempX + VTempY;
	VTempZ = VTempZ + VTempW;
	VTempX = VTempX + VTempZ;

	return SetVector(VTempX.V[0], VTempX.V[1], VTempX.V[2]);
}

export void SetDynamicData_RenderThread(uniform FVector PositionBuffer[], 
										const uniform unsigned int NumVertices, 
										const uniform unsigned int Stride, 
										const uniform int32 BoneMap[], 
										const uniform FMatrix Transforms[], 
										const uniform FVector Vertices[])
{
	uniform unsigned int Chunk = 0;

	foreach(i = 0 ... NumVertices)
	{
		uniform float * uniform pVertices = (uniform float * uniform)&Vertices[Chunk];
		uniform float * uniform pPosition = (uniform float * uniform)&PositionBuffer[Chunk];

		FVector P;
		aos_to_soa3(pVertices, &P.V[0], &P.V[1], &P.V[2]);

		FVector Out;
		const int32 Bone = BoneMap[i];
		foreach_unique(Index in Bone)
		{
			Out = VectorTransformVector(P, Transforms[Index]);
		}

		soa_to_aos3(Out.V[0], Out.V[1], Out.V[2], pPosition);

		Chunk += programCount;
	}
}
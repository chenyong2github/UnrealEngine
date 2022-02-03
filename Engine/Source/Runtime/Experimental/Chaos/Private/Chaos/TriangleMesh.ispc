// Copyright Epic Games, Inc. All Rights Reserved.

#define EXPLICIT_VECTOR4 1

#include "Math/Vector.isph"

export void GetFaceNormals(uniform FVector3f Normals[],
							const uniform FVector3f Points[],
							const uniform FIntVector Elements[],
							const uniform int32 NumElements)
{
	foreach(i = 0 ... NumElements)
	{
		const varying FIntVector Tri = VectorLoad(&Elements[extract(i, 0)]);

		const varying FVector3f p0 = VectorGather(&Points[Tri.V[0]]);
		const varying FVector3f p1 = VectorGather(&Points[Tri.V[1]]);
		const varying FVector3f p2 = VectorGather(&Points[Tri.V[2]]);

		const varying FVector3f p10 = p1 - p0;
		const varying FVector3f p20 = p2 - p0;
		const varying FVector3f Cross = VectorCross(p20, p10);
		const varying FVector3f CrossNormalized = VectorGetSafeNormal(Cross);

		VectorStore(&Normals[extract(i, 0)], CrossNormalized);
	}
}

struct TArrayInt
{
	int32 *Data;
	int32 ArrayNum;
};

export void GetPointNormals(uniform FVector3f PointNormals[],
							const uniform FVector3f FaceNormals[],
							const uniform TArrayInt PointToTriangleMap[],
							const uniform int32 StartIndex,
							const uniform int32 NumFaceNormals,
							const uniform int32 NumIndices)
{
	foreach(Element = 0 ... NumIndices)
	{
		varying FVector3f Normal = FloatZeroVector;

		#pragma ignore warning(perf)
		const varying TArrayInt TriangleMap = PointToTriangleMap[Element];  // Access MPointToTriangleMap with local index

		for (int32 k = 0; k < TriangleMap.ArrayNum; ++k)
		{
			#pragma ignore warning(perf)
			const int32 Index = TriangleMap.Data[k];

			if (Index >= 0 && Index < NumFaceNormals)
			{
				Normal = Normal + VectorGather(&FaceNormals[Index]);
			}
		}

		VectorStore(&PointNormals[extract(Element, 0) + StartIndex], VectorGetSafeNormal(Normal));
	}
}

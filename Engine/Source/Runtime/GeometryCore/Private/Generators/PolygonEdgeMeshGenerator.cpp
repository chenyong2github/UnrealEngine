// Copyright Epic Games, Inc. All Rights Reserved.

#include "Generators/PolygonEdgeMeshGenerator.h"

using namespace UE::Geometry;

FPolygonEdgeMeshGenerator::FPolygonEdgeMeshGenerator(const TArray<FFrame3d>& InPolygon,
	const TArray<double>& InOffsetScaleFactors,
	double InWidth,
	FVector3d InNormal) :
	Polygon(InPolygon),
	OffsetScaleFactors(InOffsetScaleFactors),
	Width(InWidth),
	Normal(InNormal)
{
	check(Polygon.Num() == OffsetScaleFactors.Num());
}


// Generate triangulation
// TODO: Enable more subdivisions along the width and length dimensions if requested
FMeshShapeGenerator& FPolygonEdgeMeshGenerator::Generate()
{
	const int NumInputVertices = Polygon.Num();
	check(NumInputVertices >= 3);
	if (NumInputVertices < 3)
	{
		return *this;
	}
	const int NumVertices = 2 * NumInputVertices;
	const int NumUVs = 2 * NumInputVertices + 2;
	const int NumTriangles = 2 * NumInputVertices;
	SetBufferSizes(NumVertices, NumTriangles, NumUVs, NumVertices);

	// Trace the input path, placing vertices on either side of each input vertex 
	const FVector3d LeftVertex{ 0, -Width, 0 };
	const FVector3d RightVertex{ 0, Width, 0 };
	for (int CurrentInputVertex = 0; CurrentInputVertex < NumInputVertices; ++CurrentInputVertex)
	{
		const FFrame3d& CurrentFrame = Polygon[CurrentInputVertex];
		const int NewVertexAIndex = 2 * CurrentInputVertex;
		const int NewVertexBIndex = NewVertexAIndex + 1;
		Vertices[NewVertexAIndex] = CurrentFrame.FromFramePoint(OffsetScaleFactors[CurrentInputVertex] * LeftVertex);
		Vertices[NewVertexBIndex] = CurrentFrame.FromFramePoint(OffsetScaleFactors[CurrentInputVertex] * RightVertex);
	}

	// Triangulate the vertices we just placed
	int PolyIndex = 0;
	for (int CurrentVertex = 0; CurrentVertex < NumInputVertices; ++CurrentVertex)
	{
		const int NewVertexA = 2 * CurrentVertex;
		const int NewVertexB = NewVertexA + 1;
		const int NewVertexC = (NewVertexA + 2) % NumVertices;
		const int NewVertexD = (NewVertexA + 3) % NumVertices;

		FIndex3i NewTriA{ NewVertexA, NewVertexB, NewVertexC };
		const int NewTriAIndex = 2 * CurrentVertex;

		SetTriangle(NewTriAIndex, NewTriA);
		SetTriangleUVs(NewTriAIndex, NewTriA);
		SetTriangleNormals(NewTriAIndex, NewTriA);
		SetTrianglePolygon(NewTriAIndex, PolyIndex);

		const int NewTriBIndex = NewTriAIndex + 1;
		FIndex3i NewTriB{ NewVertexC, NewVertexB, NewVertexD };

		SetTriangle(NewTriBIndex, NewTriB);
		SetTriangleUVs(NewTriBIndex, NewTriB);
		SetTriangleNormals(NewTriBIndex, NewTriB);
		SetTrianglePolygon(NewTriBIndex, PolyIndex);

		if (!bSinglePolyGroup)
		{
			PolyIndex++;
		}
	}

	for (int NewVertexIndex = 0; NewVertexIndex < NumVertices; ++NewVertexIndex)
	{
		Normals[NewVertexIndex] = FVector3f(Normal);
		NormalParentVertex[NewVertexIndex] = NewVertexIndex;
	}

	// Create a UV strip for the path
	const float UVLeft = 0.0f, UVBottom = 0.0f;
	float UVRight = 1.0f, UVTop = 1.0f;
	if (bScaleUVByAspectRatio && UVWidth != UVHeight)
	{
		if (UVWidth > UVHeight)
		{
			UVTop = UVHeight / UVWidth;
		}
		else
		{
			UVRight = UVWidth / UVHeight;
		}
	}
	FVector2f UV00 = FVector2f(UVLeft, UVBottom);
	FVector2f UV01 = FVector2f(UVRight, UVBottom);
	FVector2f UV11 = FVector2f(UVRight, UVTop);
	FVector2f UV10 = FVector2f(UVLeft, UVTop);

	for (int CurrentInputVertex = 0; CurrentInputVertex < NumInputVertices; ++CurrentInputVertex)
	{
		const int NewVertexAIndex = 2 * CurrentInputVertex;
		const int NewVertexBIndex = NewVertexAIndex + 1;

		float UParam = (float)CurrentInputVertex / (float)NumInputVertices;	// TODO: Parametrize based on arc length
		UVs[NewVertexAIndex] = BilinearInterp(UV00, UV01, UV11, UV10, UParam, 0.0f);
		UVs[NewVertexBIndex] = BilinearInterp(UV00, UV01, UV11, UV10, UParam, 1.0f);
		UVParentVertex[NewVertexAIndex] = NewVertexAIndex;
		UVParentVertex[NewVertexBIndex] = NewVertexBIndex;
	}

	// Final UVs
	{
		const int NewTriAIndex = 2 * (NumInputVertices - 1);
		const int NewTriBIndex = NewTriAIndex + 1;

		const int NewVertexA = 2 * (NumInputVertices - 1);
		const int NewVertexB = NewVertexA + 1;
		const int NewVertexC = NewVertexA + 2;
		const int NewVertexD = NewVertexA + 3;

		ensure(NewVertexD < UVs.Num());

		FIndex3i NewTriA{ NewVertexA, NewVertexB, NewVertexC };
		SetTriangleUVs(NewTriAIndex, NewTriA);

		FIndex3i NewTriB{ NewVertexC, NewVertexB, NewVertexD };
		SetTriangleUVs(NewTriBIndex, NewTriB);

		UVs[2 * NumInputVertices] = { UVRight, UVBottom };  // BilinearInterp(UV00, UV01, UV11, UV10, 1.0, 0.0);		// == (UVRight, UVBottom)?
		UVs[2 * NumInputVertices + 1] = { UVRight, UVTop }; // BilinearInterp(UV00, UV01, UV11, UV10, 1.0, 1.0);   // == (UVRight, UVTop)?
		UVParentVertex[2 * NumInputVertices] = 0;
		UVParentVertex[2 * NumInputVertices + 1] = 1;
	}

	return *this;
}

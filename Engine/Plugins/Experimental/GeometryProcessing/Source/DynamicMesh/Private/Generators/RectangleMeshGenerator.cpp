// Copyright Epic Games, Inc. All Rights Reserved.

#include "Generators/RectangleMeshGenerator.h"


FRectangleMeshGenerator::FRectangleMeshGenerator()
{
	Origin = FVector3d::Zero();
	Width = 10.0f;
	Height = 10.0f;
	WidthVertexCount = HeightVertexCount = 8;
	Normal = FVector3f::UnitZ();
	IndicesMap = FIndex2i(0, 1);
	bScaleUVByAspectRatio = true;
}



FMeshShapeGenerator& FRectangleMeshGenerator::Generate()
{
	check(IndicesMap.A >= 0 && IndicesMap.A <= 2);
	check(IndicesMap.B >= 0 && IndicesMap.B <= 2);

	int WidthNV = (WidthVertexCount > 1) ? WidthVertexCount : 2;
	int HeightNV = (HeightVertexCount > 1) ? HeightVertexCount : 2;

	int TotalNumVertices = WidthNV * HeightNV;
	int TotalNumTriangles = 2 * (WidthNV - 1) * (HeightNV - 1);
	SetBufferSizes(TotalNumVertices, TotalNumTriangles, TotalNumVertices, TotalNumVertices);

	// corner vertices
	FVector3d v00 = MakeVertex(-Width / 2.0f, -Height / 2.0f);
	FVector3d v01 = MakeVertex(Width / 2.0f, -Height / 2.0f);
	FVector3d v11 = MakeVertex(Width / 2.0f, Height / 2.0f);
	FVector3d v10 = MakeVertex(-Width / 2.0f, Height / 2.0f);

	// corner UVs
	float uvleft = 0.0f, uvright = 1.0f, uvbottom = 0.0f, uvtop = 1.0f;
	if (bScaleUVByAspectRatio && Width != Height)
	{
		if (Width > Height)
		{
			uvtop = Height / Width;
		}
		else
		{
			uvright = Width / Height;
		}
	}

	FVector2f uv00 = FVector2f(uvleft, uvbottom);
	FVector2f uv01 = FVector2f(uvright, uvbottom);
	FVector2f uv11 = FVector2f(uvright, uvtop);
	FVector2f uv10 = FVector2f(uvleft, uvtop);


	int vi = 0;
	int ti = 0;

	// add vertex rows
	int start_vi = vi;
	for (int yi = 0; yi < HeightNV; ++yi)
	{
		double ty = (double)yi / (double)(HeightNV - 1);
		for (int xi = 0; xi < WidthNV; ++xi)
		{
			double tx = (double)xi / (double)(WidthNV - 1);
			Normals[vi] = Normal;
			NormalParentVertex[vi] = vi;
			UVs[vi] = BilinearInterp(uv00, uv01, uv11, uv10, (float)tx, (float)ty);
			UVParentVertex[vi] = vi;
			Vertices[vi++] = BilinearInterp(v00, v01, v11, v10, tx, ty);
		}
	}

	// add triangulated quads
	int PolyIndex = 0;
	for (int y0 = 0; y0 < HeightNV - 1; ++y0)
	{
		for (int x0 = 0; x0 < WidthNV - 1; ++x0)
		{
			int i00 = start_vi + y0 * WidthNV + x0;
			int i10 = start_vi + (y0 + 1)*WidthNV + x0;
			int i01 = i00 + 1, i11 = i10 + 1;

			SetTriangle(ti, i00, i11, i01);
			SetTrianglePolygon(ti, PolyIndex);
			SetTriangleUVs(ti, i00, i11, i01);
			SetTriangleNormals(ti, i00, i11, i01);

			ti++;

			SetTriangle(ti, i00, i10, i11);
			SetTrianglePolygon(ti, PolyIndex);
			SetTriangleUVs(ti, i00, i10, i11);
			SetTriangleNormals(ti, i00, i10, i11);

			ti++;
			PolyIndex++;
		}
	}

	return *this;
}
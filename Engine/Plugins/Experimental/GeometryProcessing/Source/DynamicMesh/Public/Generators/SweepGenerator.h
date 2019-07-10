// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

// Port of geometry3Sharp CylinderGenerator

#pragma once

#include "Math/UnrealMathUtility.h"
#include "MeshShapeGenerator.h"
#include "Misc/AssertionMacros.h"

#include "CompGeom/PolygonTriangulation.h"
#include "FrameTypes.h"
#include "MatrixTypes.h"
#include "Polygon2.h"

/**
 * ECapType indicates the type of cap to use on a sweep
 */
enum class /*DYNAMICMESH_API*/ ECapType
{
	None = 0,
	FlatTriangulation = 1
	// TODO: Cone, other caps ...
};

class /*DYNAMICMESH_API*/ FSweepGeneratorBase : public FMeshShapeGenerator
{
public:
	virtual ~FSweepGeneratorBase()
	{
	}

protected:
	int32 CapVertStart[2], CapNormalStart[2], CapUVStart[2], CapTriangleStart[2], CapPolygonStart[2];

	/**
	 * Shared logic for creating vertex buffers and triangulations across all sweep primitives
	 * Note: Does not set vertex positions or normals; a separate call must do that.
	 */
	void ConstructMeshTopology(const FPolygon2d& CrossSection,
							   const TArrayView<const int32>& UVSections,
							   const TArrayView<const int32>& NormalSections,
							   int32 NumCrossSections,
							   const ECapType Caps[2],
							   FVector2d UVScale, FVector2d UVOffset)
	{
		// per cross section
		int32 XVerts = CrossSection.VertexCount();
		int32 XNormals = XVerts + NormalSections.Num();
		int32 XUVs = XVerts + UVSections.Num() + 1;

		int32 NumVerts = XVerts * NumCrossSections;
		int32 NumNormals = NumCrossSections > 1 ? XNormals * NumCrossSections : 0;
		int32 NumUVs = NumCrossSections > 1 ? XUVs * NumCrossSections : 0;
		int32 NumPolygons = (NumCrossSections - 1) * XVerts;
		int32 NumTriangles = NumPolygons * 2;

		TArray<FIndex3i> OutTriangles;

		for (int32 CapIdx = 0; CapIdx < 2; CapIdx++)
		{
			CapVertStart[CapIdx] = NumVerts;
			CapNormalStart[CapIdx] = NumNormals;
			CapUVStart[CapIdx] = NumUVs;
			CapTriangleStart[CapIdx] = NumTriangles;
			CapPolygonStart[CapIdx] = NumPolygons;

			if (Caps[CapIdx] == ECapType::FlatTriangulation)
			{
				NumTriangles += XVerts - 2;
				NumPolygons++;
				NumUVs += XVerts;
				NumNormals += XVerts;
			}

			// TODO: support more cap type; e.g.:
			//else if (Caps[CapIdx] == ECapType::FlatMidpointFan)
			//{
			//	NumTriangles += XVerts;
			//	NumPolygons += XVerts;
			//	NumUVs += XVerts + 1;
			//	NumNormals += XVerts + 1;
			//	NumVerts += 1;
			//}
			//else if (Caps[CapIdx] == ECapType::Cone)
			//{
			//	NumTriangles += XVerts;
			//	NumPolygons += XVerts;
			//	NumUVs += XVerts + 1;
			//	NumNormals += XVerts * 2;
			//	NumVerts += 1;
			//}
		}

		SetBufferSizes(NumVerts, NumTriangles, NumUVs, NumNormals);

		for (int32 CapIdx = 0; CapIdx < 2; CapIdx++)
		{

			if (Caps[CapIdx] == ECapType::FlatTriangulation)
			{
				int32 VertOffset = CapIdx * (XVerts * (NumCrossSections - 1));

				PolygonTriangulation::TriangulateSimplePolygon(CrossSection.GetVertices(), OutTriangles);
				int32 TriIdx = CapTriangleStart[CapIdx];
				int32 PolyIdx = CapPolygonStart[CapIdx];
				for (const FIndex3i& Triangle : OutTriangles)
				{
					bool Flipped = CapIdx == 0;
					Flipped = !Flipped;
					SetTriangle(TriIdx,
								Triangle.A + VertOffset, Triangle.B + VertOffset, Triangle.C + VertOffset,
								Flipped);
					SetTriangleUVs(TriIdx,
								   Triangle.A + CapUVStart[CapIdx],
								   Triangle.B + CapUVStart[CapIdx],
								   Triangle.C + CapUVStart[CapIdx],
								   Flipped);
					SetTriangleNormals(TriIdx,
									   Triangle.A + CapNormalStart[CapIdx],
									   Triangle.B + CapNormalStart[CapIdx],
									   Triangle.C + CapNormalStart[CapIdx],
									   Flipped);
					SetTrianglePolygon(TriIdx, PolyIdx);
					TriIdx++;
				}
				for (int32 Idx = 0; Idx < XVerts; Idx++)
				{
					FVector2d CenteredVert = CrossSection.GetVertices()[Idx] * UVScale + UVOffset;
					SetUV(CapUVStart[CapIdx] + Idx, FVector2f(CenteredVert.X, CenteredVert.Y), VertOffset + Idx);
					SetNormal(CapNormalStart[CapIdx] + Idx, FVector3f::Zero(), VertOffset + Idx);
				}
			}
		}

		// fill in UVs and triangles along length
		if (NumCrossSections > 1)
		{
			int32 UVSection = 0, UVSubIdx = 0;

			int32 NumSections = UVSections.Num();
			int32 NextDupVertIdx = UVSection < NumSections ? UVSections[UVSection] : -1;
			for (int32 VertSubIdx = 0; VertSubIdx < XVerts; UVSubIdx++)
			{
				float UVX = VertSubIdx / float(XVerts);
				for (int32 XIdx = 0; XIdx < NumCrossSections; XIdx++)
				{
					float UVY = XIdx / float(NumCrossSections - 1);
					SetUV(XIdx * XUVs + UVSubIdx, FVector2f(UVX, UVY), XIdx * XVerts + VertSubIdx);
				}

				if (VertSubIdx == NextDupVertIdx)
				{
					NextDupVertIdx = UVSection < NumSections ? UVSections[UVSection] : -1;
				}
				else
				{
					for (int32 XIdx = 0; XIdx + 1 < NumCrossSections; XIdx++)
					{
						SetTriangleUVs(
							XVerts * 2 * XIdx + 2 * VertSubIdx,
							XIdx * XUVs + UVSubIdx,
							XIdx * XUVs + UVSubIdx + 1,
							(XIdx + 1) * XUVs + UVSubIdx, true);
						SetTriangleUVs(
							XVerts * 2 * XIdx + 2 * VertSubIdx + 1,
							(XIdx + 1) * XUVs + UVSubIdx + 1,
							(XIdx + 1) * XUVs + UVSubIdx,
							XIdx * XUVs + UVSubIdx + 1, true);
					}
					VertSubIdx++;
				}
			}
			{
				// final UV
				float UVX = 1.0f;
				int32 VertSubIdx = 0;
				for (int32 XIdx = 0; XIdx < NumCrossSections; XIdx++)
				{
					float UVY = XIdx / float(NumCrossSections - 1);
					SetUV(XIdx * XUVs + UVSubIdx, FVector2f(UVX, UVY), XIdx * XVerts + VertSubIdx);
				}
			}
			NumSections = NormalSections.Num();
			int32 NormalSection = 0;
			NextDupVertIdx = NormalSection < NumSections ? NormalSections[NormalSection] : -1;
			check(NextDupVertIdx < XVerts);
			for (int32 VertSubIdx = 0, NormalSubIdx = 0; VertSubIdx < XVerts; NormalSubIdx++)
			{
				for (int32 XIdx = 0; XIdx < NumCrossSections; XIdx++)
				{
					// just set the normal parent; don't compute normal yet
					SetNormal(XIdx * XNormals + NormalSubIdx, FVector3f(0, 0, 0), XIdx * XVerts + VertSubIdx);
				}

				if (VertSubIdx == NextDupVertIdx)
				{
					NextDupVertIdx = NormalSection < NumSections ? NormalSections[NormalSection] : -1;
					check(NextDupVertIdx < XVerts);
				}
				else
				{
					int32 WrappedNextNormalSubIdx = (NormalSubIdx + 1) % XNormals;
					int32 WrappedNextVertexSubIdx = (VertSubIdx + 1) % XVerts;
					for (int32 XIdx = 0; XIdx + 1 < NumCrossSections; XIdx++)
					{
						int32 T0Idx = XVerts * 2 * XIdx + 2 * VertSubIdx;
						int32 T1Idx = T0Idx + 1;
						int32 PIdx = XVerts * XIdx + VertSubIdx;
						SetTrianglePolygon(T0Idx, PIdx);
						SetTrianglePolygon(T1Idx, PIdx);
						SetTriangle(T0Idx,
									XIdx * XVerts + VertSubIdx,
									XIdx * XVerts + WrappedNextVertexSubIdx,
									(XIdx + 1) * XVerts + VertSubIdx, true);
						SetTriangle(T1Idx,
									(XIdx + 1) * XVerts + WrappedNextVertexSubIdx,
									(XIdx + 1) * XVerts + VertSubIdx,
									XIdx * XVerts + WrappedNextVertexSubIdx, true);
						SetTriangleNormals(
							T0Idx,
							XIdx * XNormals + NormalSubIdx,
							XIdx * XNormals + WrappedNextNormalSubIdx,
							(XIdx + 1) * XNormals + NormalSubIdx, true);
						SetTriangleNormals(
							T1Idx,
							(XIdx + 1) * XNormals + WrappedNextNormalSubIdx,
							(XIdx + 1) * XNormals + NormalSubIdx,
							XIdx * XNormals + WrappedNextNormalSubIdx, true);
					}
					VertSubIdx++;
				}
			}
		}
	}
};

/**
 * Generate a cylinder with optional end caps
 */
class /*DYNAMICMESH_API*/ FCylinderGenerator : public FSweepGeneratorBase
{
public:
	float Radius[2] = {1.0f, 1.0f};
	float Height = 1.0f;
	int AngleSamples = 16;
	bool bCapped = false;

public:
	/** Generate the mesh */
	virtual FMeshShapeGenerator& Generate() override
	{
		FPolygon2d X = FPolygon2d::MakeCircle(1.0, AngleSamples);
		const TArray<FVector2d>& XVerts = X.GetVertices();
		ECapType Caps[2] = {ECapType::None, ECapType::None};

		if (bCapped)
		{
			Caps[0] = ECapType::FlatTriangulation;
			Caps[1] = ECapType::FlatTriangulation;
		}
		ConstructMeshTopology(X, {}, {}, 2, Caps, FVector2d(1, 1), FVector2d(0, 0));

		for (int SubIdx = 0; SubIdx < X.VertexCount(); SubIdx++)
		{
			for (int XIdx = 0; XIdx < 2; ++XIdx)
			{
				Vertices[SubIdx + XIdx * AngleSamples] =
					FVector3d(XVerts[SubIdx].X * Radius[XIdx], XVerts[SubIdx].Y * Radius[XIdx], XIdx * Height);
				Normals[SubIdx + XIdx * AngleSamples] = FVector3f(XVerts[SubIdx].X, XVerts[SubIdx].Y, 0);

				if (bCapped)
				{
					Normals[CapNormalStart[XIdx] + SubIdx] = FVector3f(0, 0, 2 * XIdx - 1);
				}
			}
		}

		for (int k = 0; k < Normals.Num(); ++k)
		{
			Normals[k].Normalize();
		}

		return *this;
	}
};

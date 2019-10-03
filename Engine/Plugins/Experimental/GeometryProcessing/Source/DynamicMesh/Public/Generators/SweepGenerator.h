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
#include "Curve/CurveUtil.h"

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
				float SideScale = 2 * CapIdx - 1;
				for (int32 Idx = 0; Idx < XVerts; Idx++)
				{
					FVector2d CenteredVert = CrossSection.GetVertices()[Idx] * UVScale + UVOffset;
					SetUV(CapUVStart[CapIdx] + Idx, FVector2f(CenteredVert.X * SideScale, CenteredVert.Y), VertOffset + Idx);
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
					SetUV(XIdx * XUVs + UVSubIdx, FVector2f(1-UVX, 1-UVY), XIdx * XVerts + VertSubIdx);
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
					SetUV(XIdx * XUVs + UVSubIdx, FVector2f(1-UVX, 1-UVY), XIdx * XVerts + VertSubIdx);
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
	int LengthSamples = 0;
	bool bCapped = false;
	bool bUVScaleMatchSidesAndCaps = true;

public:
	/** Generate the mesh */
	virtual FMeshShapeGenerator& Generate() override
	{
		FPolygon2d X = FPolygon2d::MakeCircle(1.0, AngleSamples);
		const TArray<FVector2d>& XVerts = X.GetVertices();
		ECapType Caps[2] = {ECapType::None, ECapType::None};

		FVector2d NormalSide = (FVector2d(Radius[1], Height) - FVector2d(Radius[0], 0)).Perp().Normalized();

		if (bCapped)
		{
			Caps[0] = ECapType::FlatTriangulation;
			Caps[1] = ECapType::FlatTriangulation;
		}

		int NumX = LengthSamples + 2;
		ConstructMeshTopology(X, {}, {}, NumX, Caps, FVector2d(.5, .5), FVector2d(.5, .5));

		// set vertex positions and normals for all cross sections along length
		double LengthFactor = 1.0 / double(NumX-1);
		for (int SubIdx = 0; SubIdx < X.VertexCount(); SubIdx++)
		{
			for (int XIdx = 0; XIdx < NumX; ++XIdx)
			{
				double Along = XIdx * LengthFactor;
				double AlongRadius = FMath::Lerp(Radius[0], Radius[1], Along);
				Vertices[SubIdx + XIdx * AngleSamples] =
					FVector3d(XVerts[SubIdx].X * AlongRadius, XVerts[SubIdx].Y * AlongRadius, Height * Along);
				Normals[SubIdx + XIdx * AngleSamples] = FVector3f(XVerts[SubIdx].X*NormalSide.X, XVerts[SubIdx].Y*NormalSide.X, NormalSide.Y);
			}
		}
		// if capped, set top/bottom normals
		if (bCapped)
		{
			for (int SubIdx = 0; SubIdx < X.VertexCount(); SubIdx++)
			{
				for (int XBotTop = 0; XBotTop < 2; ++XBotTop)
				{
					Normals[CapNormalStart[XBotTop] + SubIdx] = FVector3f(0, 0, 2 * XBotTop - 1);
				}
			}
		}

		for (int k = 0; k < Normals.Num(); ++k)
		{
			Normals[k].Normalize();
		}

		if (bUVScaleMatchSidesAndCaps)
		{
			float MaxAbsRad = FMathf::Max(FMathf::Abs(Radius[0]), FMathf::Abs(Radius[1]));
			float AbsHeight = FMathf::Abs(Height);
			float MaxAbsCircumference = MaxAbsRad * FMathf::TwoPi;
			
			// scales to put each differently-scaled UV coordinate into the same space
			float ThetaScale = MaxAbsCircumference;
			float HeightScale = AbsHeight;
			float CapScale = MaxAbsRad*2;

			float MaxScale = FMathf::Max3(ThetaScale, HeightScale, CapScale);
			ThetaScale /= MaxScale;
			HeightScale /= MaxScale;
			CapScale /= MaxScale;
			for (int UVIdx = 0; UVIdx < CapUVStart[0]; UVIdx++)
			{
				UVs[UVIdx].X *= ThetaScale;
				UVs[UVIdx].Y *= HeightScale;
			}
			for (int UVIdx = CapUVStart[0]; UVIdx < UVs.Num(); UVIdx++)
			{
				UVs[UVIdx] *= CapScale;
			}
		}

		return *this;
	}
};


/**
 * Sweep a 2D Profile Polygon along a 3D Path.
 * 
 * TODO: 
 *  - Loop path support
 *  - Mitering cross sections support?
 */
class /*DYNAMICMESH_API*/ FGeneralizedCylinderGenerator : public FSweepGeneratorBase
{
public:
	FPolygon2d CrossSection;
	TArray<FVector3d> Path;

	FFrame3d InitialFrame;

	bool bCapped = false;

public:
	/** Generate the mesh */
	virtual FMeshShapeGenerator& Generate() override
	{
		const bool bLoop = false; // TODO: support loops -- note this requires updating FSweepGeneratorBase to set up the mesh topology correctly!
		
		const TArray<FVector2d>& XVerts = CrossSection.GetVertices();
		ECapType Caps[2] = {ECapType::None, ECapType::None};

		if (bCapped)
		{
			Caps[0] = ECapType::FlatTriangulation;
			Caps[1] = ECapType::FlatTriangulation;
		}
		int PathNum = Path.Num();
		ConstructMeshTopology(CrossSection, {}, {}, PathNum, Caps, FVector2d(.5, .5), FVector2d(.5, .5));

		int XNum = CrossSection.VertexCount();
		TArray<FVector2d> XNormals; XNormals.SetNum(XNum);
		for (int Idx = 0; Idx < XNum; Idx++)
		{
			XNormals[Idx] = CrossSection.GetNormal_FaceAvg(Idx);
		}

		FFrame3d CrossSectionFrame = InitialFrame;
		for (int PathIdx = 0; PathIdx < PathNum; ++PathIdx)
		{
			FVector3d Tangent = TCurveUtil<double>::Tangent(Path, PathIdx, bLoop);
			CrossSectionFrame.AlignAxis(2, Tangent);
			FVector3d C = Path[PathIdx];
			FVector3d X = CrossSectionFrame.X();
			FVector3d Y = CrossSectionFrame.Y();
			for (int SubIdx = 0; SubIdx < XNum; SubIdx++)
			{
				FVector2d XP = CrossSection[SubIdx];
				FVector2d XN = XNormals[SubIdx];
				Vertices[SubIdx + PathIdx * XNum] = C + X * XP.X + Y * XP.Y;
				Normals[SubIdx + PathIdx * XNum] = FVector3f(X * XN.X + Y * XN.Y);
			}
		}
		if (bCapped && !bLoop)
		{
			for (int CapIdx = 0; CapIdx < 2; CapIdx++)
			{
				FVector3f Normal(TCurveUtil<double>::Tangent(Path, CapIdx * (PathNum-1), bLoop) * (CapIdx * 2 - 1));
				for (int SubIdx = 0; SubIdx < XNum; SubIdx++)
				{
					Normals[CapNormalStart[CapIdx] + SubIdx] = Normal;
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

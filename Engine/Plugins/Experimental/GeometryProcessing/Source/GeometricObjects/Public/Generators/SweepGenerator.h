// Copyright Epic Games, Inc. All Rights Reserved.

// Port of geometry3Sharp CylinderGenerator

#pragma once

#include "CoreMinimal.h"

#include "Math/UnrealMathUtility.h"
#include "MeshShapeGenerator.h"
#include "Misc/AssertionMacros.h"

#include "CompGeom/PolygonTriangulation.h"
#include "FrameTypes.h"
#include "MatrixTypes.h"
#include "Polygon2.h"
#include "Curve/CurveUtil.h"

#include "Util/ProgressCancel.h"

/**
 * ECapType indicates the type of cap to use on a sweep
 */
enum class /*GEOMETRICOBJECTS_API*/ ECapType
{
	None = 0,
	FlatTriangulation = 1
	// TODO: Cone, other caps ...
};

class /*GEOMETRICOBJECTS_API*/ FSweepGeneratorBase : public FMeshShapeGenerator
{
public:
	virtual ~FSweepGeneratorBase()
	{
	}

	/** If true, each quad gets a separate polygroup */
	bool bPolygroupPerQuad = false;

protected:
	int32 CapVertStart[2], CapNormalStart[2], CapUVStart[2], CapTriangleStart[2], CapPolygonStart[2];

	/**
	 * Shared logic for creating vertex buffers and triangulations across all sweep primitives
	 * Note: Does not set vertex positions or normals; a separate call must do that.
	 */
	void ConstructMeshTopology(const FPolygon2d& CrossSection,
							   const TArrayView<const int32>& UVSections,
							   const TArrayView<const int32>& NormalSections,
							   const TArrayView<const int32>& SharpNormalsAlongLength,
							   bool bEvenlySpaceUVs,
							   const TArrayView<const FVector3d>& Path, // can be empty unless bEvenlySpaceUVs is true
							   int32 NumCrossSections,
							   bool bLoop,
							   const ECapType Caps[2],
							   FVector2f SectionsUVScale, FVector2f CapUVScale, FVector2f CapUVOffset)
	{
		// per cross section
		int32 XVerts = CrossSection.VertexCount();
		int32 XNormals = XVerts + NormalSections.Num();
		int32 XUVs = XVerts + UVSections.Num() + 1;

		double TotalPerimeter = 0, TotalPathLength = 0;
		TArray<double> CrossSectionPercentages, PathPercentages;
		if (bEvenlySpaceUVs)
		{
			CrossSectionPercentages.Add(0);
			PathPercentages.Add(0);
			for (int XIdx = 0, XNum = CrossSection.VertexCount(); XIdx < XNum; XIdx++)
			{
				double SegLen = CrossSection[XIdx].Distance(CrossSection[(XIdx + 1) % XNum]);
				TotalPerimeter += SegLen;
				CrossSectionPercentages.Add(TotalPerimeter);
			}
			TotalPerimeter = FMath::Max(TotalPerimeter, FMathd::ZeroTolerance);
			for (int Idx = 1; Idx < CrossSectionPercentages.Num(); Idx++)
			{
				CrossSectionPercentages[Idx] /= TotalPerimeter;
			}
			int NumPathSegs = bLoop ? Path.Num() : Path.Num() - 1;
			for (int PIdx = 0; PIdx < NumPathSegs; PIdx++)
			{
				double SegLen = Path[PIdx].Distance(Path[(PIdx + 1) % Path.Num()]);
				TotalPathLength += SegLen;
				PathPercentages.Add(TotalPathLength);
			}
			TotalPathLength = FMath::Max(TotalPathLength, FMathd::ZeroTolerance);
			for (int Idx = 1; Idx < PathPercentages.Num(); Idx++)
			{
				PathPercentages[Idx] /= TotalPathLength;
			}
		}

		int32 NumVerts = XVerts * NumCrossSections - (bLoop ? XVerts : 0);
		int32 NumNormals = NumCrossSections > 1 ? (XNormals * NumCrossSections - (bLoop ? XNormals : 0)) : 0;
		NumNormals += XNormals * SharpNormalsAlongLength.Num();
		int32 NumUVs = NumCrossSections > 1 ? XUVs * NumCrossSections : 0;
		int32 NumPolygons = (NumCrossSections - 1) * XVerts;
		int32 NumTriangles = NumPolygons * 2;

		TArray<FIndex3i> OutTriangles;

		// doesn't make sense to have cap types if the sweep is a loop
		ensure(!bLoop || (Caps[0] == ECapType::None && Caps[1] == ECapType::None));
		
		if (!bLoop)
		{
			for (int32 CapIdx = 0; !bLoop && CapIdx < 2; CapIdx++)
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
		}

		SetBufferSizes(NumVerts, NumTriangles, NumUVs, NumNormals);

		if (!bLoop)
		{
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
						FVector2f CenteredVert = (FVector2f)CrossSection.GetVertices()[Idx] * CapUVScale + CapUVOffset;
						SetUV(CapUVStart[CapIdx] + Idx, FVector2f(CenteredVert.X * SideScale, CenteredVert.Y), VertOffset + Idx);
						SetNormal(CapNormalStart[CapIdx] + Idx, FVector3f::Zero(), VertOffset + Idx);
					}
				}
			}
		}

		// fill in UVs and triangles along length
		int MinValidCrossSections = bLoop ? 3 : 2;
		int CurFaceGroupIndex = NumPolygons;
		if (NumCrossSections >= MinValidCrossSections)
		{
			int32 UVSection = 0, UVSubIdx = 0;

			int CrossSectionsMod = NumCrossSections;
			if (bLoop)
			{
				CrossSectionsMod--; // last cross section becomes the first
			}
			int NormalCrossSectionsMod = CrossSectionsMod + SharpNormalsAlongLength.Num();

			int32 NumSections = UVSections.Num();
			int32 NextDupVertIdx = UVSection < NumSections ? UVSections[UVSection] : -1;
			for (int32 VertSubIdx = 0; VertSubIdx < XVerts; UVSubIdx++)
			{
				float UVX = bEvenlySpaceUVs ? CrossSectionPercentages[VertSubIdx] : VertSubIdx / float(XVerts);
				for (int32 XIdx = 0; XIdx < NumCrossSections; XIdx++)
				{
					float UVY = bEvenlySpaceUVs ? PathPercentages[XIdx] : XIdx / float(NumCrossSections - 1);
					SetUV(XIdx * XUVs + UVSubIdx, FVector2f(1-UVX, 1-UVY) * SectionsUVScale, (XIdx % CrossSectionsMod) * XVerts + VertSubIdx);
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
					float UVY = bEvenlySpaceUVs ? PathPercentages[XIdx] : XIdx / float(NumCrossSections - 1);
					SetUV(XIdx * XUVs + UVSubIdx, FVector2f(1-UVX, 1-UVY) * SectionsUVScale, (XIdx % CrossSectionsMod) * XVerts + VertSubIdx);
				}
			}
			NumSections = NormalSections.Num();
			int32 NormalSection = 0;
			NextDupVertIdx = NormalSection < NumSections ? NormalSections[NormalSection] : -1;
			check(NextDupVertIdx < XVerts);
			for (int32 VertSubIdx = 0, NormalSubIdx = 0; VertSubIdx < XVerts; NormalSubIdx++)
			{
				int SharpNormalIdx = 0;
				for (int32 XIdx = 0, NormalXIdx = 0; XIdx < NumCrossSections; XIdx++, NormalXIdx++)
				{
					// just set the normal parent; don't compute normal yet
					SetNormal((NormalXIdx % NormalCrossSectionsMod) * XNormals + NormalSubIdx, FVector3f(0, 0, 0), (XIdx % CrossSectionsMod) * XVerts + VertSubIdx);
					// duplicate normals for cross sections that are 'sharp'
					if (SharpNormalIdx < SharpNormalsAlongLength.Num() && XIdx == SharpNormalsAlongLength[SharpNormalIdx])
					{
						NormalXIdx++;
						SetNormal((NormalXIdx % NormalCrossSectionsMod) * XNormals + NormalSubIdx, FVector3f(0, 0, 0), (XIdx % CrossSectionsMod) * XVerts + VertSubIdx);
						SharpNormalIdx++;
					}
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
					SharpNormalIdx = 0;
					for (int32 XIdx = 0, NXIdx = 0; XIdx + 1 < NumCrossSections; XIdx++, NXIdx++)
					{
						int32 T0Idx = XVerts * 2 * XIdx + 2 * VertSubIdx;
						int32 T1Idx = T0Idx + 1;
						int32 PIdx = XVerts * XIdx + VertSubIdx;
						int32 NextXIdx = (XIdx + 1) % CrossSectionsMod;
						int32 NextNXIdx = (NXIdx + 1) % NormalCrossSectionsMod;
						SetTrianglePolygon(T0Idx, (bPolygroupPerQuad) ? PIdx : (CurFaceGroupIndex+XIdx) );
						SetTrianglePolygon(T1Idx, (bPolygroupPerQuad) ? PIdx : (CurFaceGroupIndex+XIdx) );
						SetTriangle(T0Idx,
									XIdx * XVerts + VertSubIdx,
									XIdx * XVerts + WrappedNextVertexSubIdx,
									NextXIdx * XVerts + VertSubIdx, true);
						SetTriangle(T1Idx,
									NextXIdx * XVerts + WrappedNextVertexSubIdx,
									NextXIdx * XVerts + VertSubIdx,
									XIdx * XVerts + WrappedNextVertexSubIdx, true);
						SetTriangleNormals(
							T0Idx,
							NXIdx * XNormals + NormalSubIdx,
							NXIdx * XNormals + WrappedNextNormalSubIdx,
							NextNXIdx * XNormals + NormalSubIdx, true);
						SetTriangleNormals(
							T1Idx,
							NextNXIdx * XNormals + WrappedNextNormalSubIdx,
							NextNXIdx * XNormals + NormalSubIdx,
							NXIdx * XNormals + WrappedNextNormalSubIdx, true);
						if (SharpNormalIdx < SharpNormalsAlongLength.Num() && XIdx+1 == SharpNormalsAlongLength[SharpNormalIdx])
						{
							NXIdx++;
							SharpNormalIdx++;
						}
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
class /*DYNAMICMESH_API*/ FVerticalCylinderGeneratorBase : public FSweepGeneratorBase
{
public:
	int AngleSamples = 16;
	bool bCapped = false;
	bool bUVScaleMatchSidesAndCaps = true;

	static float ComputeSegLengths(const TArrayView<float>& Radii, const TArrayView<float>& Heights, TArray<float>& AlongPercents)
	{
		float LenAlong = 0;
		int32 NumX = Radii.Num();
		AlongPercents.SetNum(NumX);
		AlongPercents[0] = 0;
		for (int XIdx = 0; XIdx+1 < NumX; XIdx++)
		{
			float Dist = FVector2d(Radii[XIdx], Heights[XIdx]).Distance(FVector2d(Radii[XIdx + 1], Heights[XIdx + 1]));
			LenAlong += Dist;
			AlongPercents[XIdx + 1] = LenAlong;
		}
		for (int XIdx = 0; XIdx+1 < NumX; XIdx++)
		{
			AlongPercents[XIdx+1] /= LenAlong;
		}
		return LenAlong;
	}

	bool GenerateVerticalCircleSweep(const TArrayView<float>& Radii, const TArrayView<float>& Heights, const TArrayView<int>& SharpNormalsAlongLength)
	{
		FPolygon2d X = FPolygon2d::MakeCircle(1.0, AngleSamples);
		const TArray<FVector2d>& XVerts = X.GetVertices();
		ECapType Caps[2] = {ECapType::None, ECapType::None};

		if (bCapped)
		{
			Caps[0] = ECapType::FlatTriangulation;
			Caps[1] = ECapType::FlatTriangulation;
		}

		int NumX = Radii.Num();
		if (!ensure(NumX == Heights.Num()))
		{
			return false;
		}
		// first and last cross sections can't be sharp, so can't have more than NumX-2 sharp normal indices
		if (!ensure(SharpNormalsAlongLength.Num() + 2 <= NumX))
		{
			return false;
		}

		TArray<float> AlongPercents;
		float LenAlong = ComputeSegLengths(Radii, Heights, AlongPercents);

		ConstructMeshTopology(X, {}, {}, SharpNormalsAlongLength, false, {}, NumX, false, Caps, FVector2f(1, 1), FVector2f(.5f, .5f), FVector2f(.5f, .5f));

		TArray<FVector2d> NormalSides; NormalSides.SetNum(NumX - 1);
		for (int XIdx = 0; XIdx+1 < NumX; XIdx++)
		{
			NormalSides[XIdx] = (FVector2d(Radii[XIdx+1], Heights[XIdx+1]) - FVector2d(Radii[XIdx], Heights[XIdx])).Perp().Normalized();
		}
		TArray<FVector2d> SmoothedNormalSides; SmoothedNormalSides.SetNum(NumX);
		// smooth internal normals
		SmoothedNormalSides[0] = NormalSides[0];
		SmoothedNormalSides.Last() = NormalSides.Last();
		for (int XIdx = 1; XIdx + 1 < NumX; XIdx++)
		{
			SmoothedNormalSides[XIdx] = (NormalSides[XIdx] + NormalSides[XIdx - 1]).Normalized();
		}


		// set vertex positions and normals for all cross sections along length
		for (int SubIdx = 0; SubIdx < X.VertexCount(); SubIdx++)
		{
			int SharpNormalIdx = 0;
			for (int XIdx = 0, NormalXIdx = 0; XIdx < NumX; ++XIdx, ++NormalXIdx)
			{
				double Along = AlongPercents[XIdx];
				double AlongRadius = Radii[XIdx];
				Vertices[SubIdx + XIdx * AngleSamples] =
					FVector3d(XVerts[SubIdx].X * AlongRadius, XVerts[SubIdx].Y * AlongRadius, Heights[XIdx]);
				if (SharpNormalIdx < SharpNormalsAlongLength.Num() && XIdx == SharpNormalsAlongLength[SharpNormalIdx])
				{
					// write sharp normals
					if (ensure(XIdx > 0)) // very first index cannot be sharp
					{
						Normals[SubIdx + NormalXIdx * AngleSamples] = FVector3f(XVerts[SubIdx].X*NormalSides[XIdx-1].X, XVerts[SubIdx].Y*NormalSides[XIdx-1].X, NormalSides[XIdx-1].Y);
					}
					NormalXIdx++;
					if (ensure(XIdx + 1 < NumX)) // very last index cannot be sharp
					{
						Normals[SubIdx + NormalXIdx * AngleSamples] = FVector3f(XVerts[SubIdx].X*NormalSides[XIdx].X, XVerts[SubIdx].Y*NormalSides[XIdx].X, NormalSides[XIdx].Y);
					}
					SharpNormalIdx++;
				}
				else
				{
					// write smoothed normal
					Normals[SubIdx + NormalXIdx * AngleSamples] = FVector3f(XVerts[SubIdx].X*SmoothedNormalSides[XIdx].X, XVerts[SubIdx].Y*SmoothedNormalSides[XIdx].X, SmoothedNormalSides[XIdx].Y);
				}
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

		if (bUVScaleMatchSidesAndCaps)
		{
			float MaxAbsRad = FMathf::Abs(Radii[0]);
			for (int XIdx = 0; XIdx < NumX; XIdx++)
			{
				MaxAbsRad = FMathf::Max(FMathf::Abs(Radii[XIdx]), MaxAbsRad);
			}
			float AbsHeight = LenAlong;
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

		return true;
	}
};

/**
 * Generate a cylinder with optional end caps
 */
class /*DYNAMICMESH_API*/ FCylinderGenerator : public FVerticalCylinderGeneratorBase
{
public:
	float Radius[2] = {1.0f, 1.0f};
	float Height = 1.0f;
	int LengthSamples = 0;

public:
	/** Generate the mesh */
	virtual FMeshShapeGenerator& Generate() override
	{
		if (bCapped)
		{
			AngleSamples = FMath::Max(AngleSamples, 3);
		}

		TArray<float> Radii, Heights;

		Radii.Add(Radius[0]);
		Heights.Add(0);
		for (int ExtraIdx = 0; ExtraIdx < LengthSamples; ExtraIdx++)
		{
			float Along = float(ExtraIdx + 1) / float(LengthSamples + 1);
			Radii.Add(FMath::Lerp(Radius[0], Radius[1], Along));
			Heights.Add(Height * Along);
		}
		Radii.Add(Radius[1]);
		Heights.Add(Height);

		GenerateVerticalCircleSweep(Radii, Heights, {});

		return *this;
	}
};

/**
* Generate a 3D arrow
*/
class /*DYNAMICMESH_API*/ FArrowGenerator : public FVerticalCylinderGeneratorBase
{
public:
	float StickRadius = 0.5f;
	float StickLength = 1.0f;
	float HeadBaseRadius = 1.0f;
	float HeadTipRadius = 0.01f;
	float HeadLength = 0.5f;

	int AdditionalLengthSamples[3]{ 0,0,0 }; // additional length-wise samples on the three segments (along stick, along arrow base, along arrow cone)

	void DistributeAdditionalLengthSamples(int TargetSamples)
	{
		TArray<float> AlongPercents;
		TArray<float> Radii{ StickRadius, StickRadius, HeadBaseRadius, HeadTipRadius };
		TArray<float> Heights{ 0, StickLength, StickLength, StickLength + HeadLength };
		float LenAlong = ComputeSegLengths(Radii, Heights, AlongPercents);
		for (int Idx = 0; Idx < 3; Idx++)
		{
			AdditionalLengthSamples[Idx] = (int)(.5f+AlongPercents[Idx + 1] * TargetSamples);
		}
	}

public:
	/** Generate the mesh */
	virtual FMeshShapeGenerator& Generate() override
	{
		TArray<float> Radii, Heights;
		TArray<int> SharpNormalsAlongLength;
		float SrcRadii[] { StickRadius, StickRadius, HeadBaseRadius, HeadTipRadius };
		float SrcHeights[] { 0, StickLength, StickLength, StickLength + HeadLength };

		int SegIdx = 0;
		for (; SegIdx < 3; SegIdx++)
		{
			Radii.Add(SrcRadii[SegIdx]);
			Heights.Add(SrcHeights[SegIdx]);
			for (int ExtraIdx = 0; ExtraIdx < AdditionalLengthSamples[SegIdx]; ExtraIdx++)
			{
				float Along = float(ExtraIdx + 1) / float(AdditionalLengthSamples[SegIdx] + 1);
				Radii.Add(FMath::Lerp(SrcRadii[SegIdx], SrcRadii[SegIdx + 1], Along));
				Heights.Add(FMath::Lerp(SrcHeights[SegIdx], SrcHeights[SegIdx + 1], Along));
			}
		}
		SharpNormalsAlongLength.Add(1 + AdditionalLengthSamples[0]);
		SharpNormalsAlongLength.Add(SharpNormalsAlongLength.Last() + 1 + AdditionalLengthSamples[1]);
		Radii.Add(SrcRadii[SegIdx]);
		Heights.Add(SrcHeights[SegIdx]);
		
		GenerateVerticalCircleSweep(Radii, Heights, SharpNormalsAlongLength);

		return *this;
	}
};


/**
 * Sweep a 2D Profile Polygon along a 3D Path.
 * 
 * TODO: 
 *  - a custom variant for toruses specifically (would be faster)
 *  - Mitering cross sections support?
 */
class /*DYNAMICMESH_API*/ FGeneralizedCylinderGenerator : public FSweepGeneratorBase
{
public:
	FPolygon2d CrossSection;
	TArray<FVector3d> Path;

	FFrame3d InitialFrame;

	bool bCapped = false;
	bool bLoop = false;

	// When true, the generator attempts to scale UV's in a way that preserves scaling across different mesh
	// results, aiming for 1.0 in UV space to be equal to UnitUVInWorldCoordinates in world space. This in
	// practice means adjusting the U scale relative to the CrossSection curve length and V scale relative
	// to the distance between vertices on the Path.
	bool bUVScaleRelativeWorld = false;

	// Only relevant if bUVScaleRelativeWorld is true (see that description)
	float UnitUVInWorldCoordinates = 100;

public:
	/** Generate the mesh */
	virtual FMeshShapeGenerator& Generate() override
	{
		const TArray<FVector2d>& XVerts = CrossSection.GetVertices();
		ECapType Caps[2] = {ECapType::None, ECapType::None};

		if (bCapped && !bLoop)
		{
			Caps[0] = ECapType::FlatTriangulation;
			Caps[1] = ECapType::FlatTriangulation;
		}
		int PathNum = Path.Num();
		
		FAxisAlignedBox2f Bounds = (FAxisAlignedBox2f)CrossSection.Bounds();
		double BoundsMaxDimInv = 1.0 / FMathd::Max(Bounds.MaxDim(), .001);
		FVector2f SectionScale(1, 1), CapScale(BoundsMaxDimInv, BoundsMaxDimInv);
		if (bUVScaleRelativeWorld)
		{
			double Perimeter = CrossSection.Perimeter();
			double PathLen = TCurveUtil<double>::ArcLength(Path, bLoop);
			SectionScale.X = Perimeter / UnitUVInWorldCoordinates;
			SectionScale.Y = PathLen / UnitUVInWorldCoordinates;
			CapScale.X = CapScale.Y = 1.0f / UnitUVInWorldCoordinates;
		}
		ConstructMeshTopology(CrossSection, {}, {}, {}, true, Path, PathNum + (bLoop ? 1 : 0), bLoop, Caps, SectionScale, CapScale, Bounds.Center());

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

enum class EProfileSweepPolygonGrouping : uint8
{
	/** One polygroup for entire output mesh */
	Single,
	/** One polygroup per mesh quad/triangle */
	PerFace,

	/* One polygroup per strip that represents a step along the sweep curve. */
	PerSweepSegment,
	/* One polygroup per strip coming from each individual edge of the profile curve. */
	PerProfileSegment
};

enum class EProfileSweepQuadSplit : uint8
{
	/** Always split the quad in the same way relative sweep direction and profile direction. */
	Uniform,
	/** Split the quad to connect the shortest diagonal. */
	ShortestDiagonal
};

/**
 * Much like FGeneralizedCylinderGenerator, but allows an arbitrary profile curve to be swept, and gives
 * control over the frames of the sweep curve. A mesh will be properly oriented if the profile curve is
 * oriented counterclockwise when facing down the direction in which it is being swept.
 *
 * Because it supports open profile curves, as well as welded points (for welding points on an axis of rotation), 
 * it cannot actually use the utility function from FSweepGeneratorBase, and so it doesn't inherit from 
 * that class.
 */
class GEOMETRICOBJECTS_API FProfileSweepGenerator : public FMeshShapeGenerator
{
public:

	// Curve that will be swept along the curve, given in coordinates of the frames used in the sweep curve.
	TArray<FVector3d> ProfileCurve;

	// Curve along which to sweep the profile curve
	TArray<FFrame3d> SweepCurve;

	// Indices into ProfileCurve that should not be swept along the curve, instead being instantiated
	// just once. This is useful for welding vertices on an axis of rotation if the sweep curve denotes
	// a revolution.
	TSet<int32> WeldedVertices;

	// Generated UV coordinates will be multiplied by these values.
	FVector2d UVScale = FVector2d(1,1);

	// These values will be added to the generated UV coordinates after applying UVScale.
	FVector2d UVOffset = FVector2d(0, 0);

	// When true, the generator attempts to scale UV's in a way that preserves scaling across different mesh
	// results, aiming for 1.0 in UV space to be equal to UnitUVInWorldCoordinates in world space. This is 
	// generally speaking unrealistic because UV's are going to be variably stretched no matter what, but 
	// in practice it means adjusting the V scale relative to the profile curve length and U scale relative
	// to a very crude measurement of movement across sweep frames.
	bool bUVScaleRelativeWorld = false;

	// Only relevant if bUVScaleRelativeWorld is true (see that description)
	float UnitUVInWorldCoordinates = 100;

	// If true, the last point of the sweep curve is considered to be connected to the first.
	bool bSweepCurveIsClosed = false;

	// If true, the last point of the profile curve is considered to be connected to the first.
	bool bProfileCurveIsClosed = false;

	// If true, each triangle will have its own normals at each vertex, rather than sharing averaged
	// ones with nearby triangles.
	bool bSharpNormals = true;

	// If true, welded-to-welded connections in the profile curve (which can't result in triangles)
	// do not affect the UV layout.
	bool bUVsSkipFullyWeldedEdges = true;

	EProfileSweepQuadSplit QuadSplitMethod = EProfileSweepQuadSplit::ShortestDiagonal;

	// When QuadSplitMode is ShortestDiagonal, biases one of the diagonals so that symmetric
	// quads are split uniformly. The tolerance is a proportion allowable difference.
	double DiagonalTolerance = 0.01;

	EProfileSweepPolygonGrouping PolygonGroupingMode = EProfileSweepPolygonGrouping::PerFace;

	// If not null, this pointer is intermittently used to check whether the current operation should stop early
	FProgressCancel* Progress = nullptr;

	// TODO: We could allow the user to dissallow bowtie vertex creation, which currently could 
	// happen depending on which vertices are welded.

public:

	/** Generate the mesh */
	virtual FMeshShapeGenerator& Generate() override;

	/** If the sweep curve is not closed, this will store the vertex ids of the first and last instances
	 * of the profile curve. Note that even if the profile curve is closed, depending on the welding,
	 * these could be part of a single boundary (ie, a square revolved 90 degrees around a welded side
	 * actually has one open boundary rather than two, since they are joined), but the user likely
	 * wants to be given them separately for ease in making end caps.
	 */
	TArray<int32> EndProfiles[2];

	// TODO: We could output other boundaries too, but that's probably only worth doing once we find
	// a case where we would actually use them.
protected:

	void InitializeUvBuffer(const TArray<int32>& VertPositionOffsets, 
		int32& NumUvRowsOut, int32& NumUvColumnsOut);
	void AdjustNormalsForTriangle(int32 TriIndex, int32 FirstIndex, int32 SecondIndex, int32 ThirdIndex,
		TArray<FVector3d>& WeightedNormals);
	void AdjustNormalsForTriangle(int32 TriIndex, int32 FirstIndex, int32 SecondIndex, int32 ThirdIndex,
		TArray<FVector3d>& WeightedNormals, const FVector3d& AbNormalized);
};

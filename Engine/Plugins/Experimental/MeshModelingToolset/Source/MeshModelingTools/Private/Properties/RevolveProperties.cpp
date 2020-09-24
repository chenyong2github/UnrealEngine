// Copyright Epic Games, Inc. All Rights Reserved.

#include "Properties/RevolveProperties.h"

#include "CompositionOps/CurveSweepOp.h"
#include "Properties/MeshMaterialProperties.h"
#include "Properties/RevolveProperties.h"
#include "Util/RevolveUtil.h"

void URevolveProperties::ApplyToCurveSweepOp(const UNewMeshMaterialProperties& MaterialProperties,
	const FVector3d& RevolutionAxisOrigin, const FVector3d& RevolutionAxisDirection,
	FCurveSweepOp& CurveSweepOpOut) const
{
	// Reversing the profile curve flips the mesh. This may need to be done if the curve wasn't drawn in the default
	// (counterclockwise) direction or if we're not revolving in the default (counterclockwise) direction, or if
	// the user asks.
	bool bReverseProfileCurve = !RevolveUtil::ProfileIsCCWRelativeRevolve(CurveSweepOpOut.ProfileCurve,
		RevolutionAxisOrigin, RevolutionAxisDirection, CurveSweepOpOut.bProfileCurveIsClosed);
	bReverseProfileCurve = bReverseProfileCurve ^ bFlipMesh ^ bReverseRevolutionDirection;
	if (bReverseProfileCurve)
	{
		for (int32 i = 0; i < CurveSweepOpOut.ProfileCurve.Num() / 2; ++i)
		{
			Swap(CurveSweepOpOut.ProfileCurve[i], CurveSweepOpOut.ProfileCurve[CurveSweepOpOut.ProfileCurve.Num() - 1 - i]);
		}
	}

	double DegreesOffset = RevolutionDegreesOffset;
	double DegreesPerStep = RevolutionDegrees / Steps;
	if (bReverseRevolutionDirection)
	{
		DegreesPerStep *= -1;
		DegreesOffset *= -1;
	}

	if (bProfileIsCrossSectionOfSide && DegreesPerStep != 0 && abs(DegreesPerStep) < 180)
	{
		RevolveUtil::MakeProfileCurveMidpointOfFirstStep(CurveSweepOpOut.ProfileCurve, DegreesPerStep, RevolutionAxisOrigin, RevolutionAxisDirection);
	}

	// Generate the sweep curve
	CurveSweepOpOut.bSweepCurveIsClosed = bWeldFullRevolution && RevolutionDegrees == 360;
	int32 NumSweepFrames = CurveSweepOpOut.bSweepCurveIsClosed ? Steps : Steps + 1; // If closed, last sweep frame is also first
	CurveSweepOpOut.SweepCurve.Reserve(NumSweepFrames);
	RevolveUtil::GenerateSweepCurve(RevolutionAxisOrigin, RevolutionAxisDirection, DegreesOffset,
		DegreesPerStep, NumSweepFrames, bWeldFullRevolution, CurveSweepOpOut.SweepCurve);

	// Weld any vertices that are on the axis
	if (bWeldVertsOnAxis)
	{
		RevolveUtil::WeldPointsOnAxis(CurveSweepOpOut.ProfileCurve, RevolutionAxisOrigin,
			RevolutionAxisDirection, AxisWeldTolerance, CurveSweepOpOut.ProfileVerticesToWeld);
	}
	CurveSweepOpOut.bSharpNormals = bSharpNormals;
	CurveSweepOpOut.SharpNormalAngleTolerance = SharpNormalAngleTolerance;
	CurveSweepOpOut.DiagonalTolerance = DiagonalProportionTolerance;
	double UVScale = MaterialProperties.UVScale;
	CurveSweepOpOut.UVScale = FVector2d(UVScale, UVScale);
	if (bReverseProfileCurve ^ bFlipVs)
	{
		CurveSweepOpOut.UVScale[1] *= -1;
		CurveSweepOpOut.UVOffset = FVector2d(0, UVScale);
	}
	CurveSweepOpOut.bUVsSkipFullyWeldedEdges = bUVsSkipFullyWeldedEdges;
	CurveSweepOpOut.bUVScaleRelativeWorld = MaterialProperties.bWorldSpaceUVScale;
	CurveSweepOpOut.UnitUVInWorldCoordinates = 100; // This seems to be the case in the AddPrimitiveTool
	switch (PolygroupMode)
	{
	case ERevolvePropertiesPolygroupMode::Single:
		CurveSweepOpOut.PolygonGroupingMode = EProfileSweepPolygonGrouping::Single;
		break;
	case ERevolvePropertiesPolygroupMode::PerFace:
		CurveSweepOpOut.PolygonGroupingMode = EProfileSweepPolygonGrouping::PerFace;
		break;
	case ERevolvePropertiesPolygroupMode::PerStep:
		CurveSweepOpOut.PolygonGroupingMode = EProfileSweepPolygonGrouping::PerSweepSegment;
		break;
	case ERevolvePropertiesPolygroupMode::AccordingToProfileCurve:
		CurveSweepOpOut.PolygonGroupingMode = EProfileSweepPolygonGrouping::PerProfileSegment;
		break;
	}
	switch (QuadSplitMode)
	{
	case ERevolvePropertiesQuadSplit::ShortestDiagonal:
		CurveSweepOpOut.QuadSplitMode = EProfileSweepQuadSplit::ShortestDiagonal;
		break;
	case ERevolvePropertiesQuadSplit::Uniform:
		CurveSweepOpOut.QuadSplitMode = EProfileSweepQuadSplit::Uniform;
		break;
	}
	switch (CapFillMode)
	{
	case ERevolvePropertiesCapFillMode::None:
		CurveSweepOpOut.CapFillMode = FCurveSweepOp::ECapFillMode::None;
		break;
	case ERevolvePropertiesCapFillMode::Delaunay:
		CurveSweepOpOut.CapFillMode = FCurveSweepOp::ECapFillMode::Delaunay;
		break;
	case ERevolvePropertiesCapFillMode::EarClipping:
		CurveSweepOpOut.CapFillMode = FCurveSweepOp::ECapFillMode::EarClipping;
		break;
	case ERevolvePropertiesCapFillMode::CenterFan:
		CurveSweepOpOut.CapFillMode = FCurveSweepOp::ECapFillMode::CenterFan;
		break;
	}
}

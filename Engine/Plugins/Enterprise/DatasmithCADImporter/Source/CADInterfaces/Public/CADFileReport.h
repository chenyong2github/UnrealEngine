// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CADKernel/Core/Chrono.h"
#include "CADKernel/Core/Types.h"
#include "CADKernel/UI/Message.h"

namespace CADKernel
{

struct FCADFileReport
{
	int32 BodyCount = 0;
	int32 ShellCount = 0;
	int32 FaceCount = 0;
	int32 LoopCount = 0;
	int32 EdgeCount = 0;

	int32 FailedFaceCount = 0;
	int32 DegeneratedLoopCount = 0;
	int32 DoubtfulLoopOrientationCount = 0;
	int32 DegeneratedEdgeCount = 0;

	int32 SurfaceCount = 0;
	int32 DegeneratedSurfaceCount = 0;
	int32 ConeSurfaceCount = 0;
	int32 CylinderSurfaceCount = 0;
	int32 LinearTransfoSurfaceCount = 0;
	int32 NurbsSurfaceCount = 0;
	int32 OffsetSurfaceCount = 0;
	int32 PlaneSurfaceCount = 0;
	int32 RevolutionSurfaceCount = 0;
	int32 RuledSurfaceCount = 0;
	int32 SphereSurfaceCount = 0;
	int32 TorusSurfaceCount = 0;

	int32 Blend01SurfaceCount = 0;
	int32 Blend02SurfaceCount = 0;
	int32 Blend03SurfaceCount = 0;
	int32 CylindricalSurfaceCount = 0;
	int32 PipeSurfaceCount = 0;
	int32 ExtrusionSurfaceCount = 0;
	int32 SurfaceFromCurvesCount = 0;
	int32 TransformSurfaceCount = 0;

	int32 SurfaceAsNurbsCount = 0;
	int32 SurfaceNurbsCount = 0;

	int32 RestrictionCurveCount = 0;

	int32 CurveCount = 0;
	int32 CurveCircleCount = 0;
	int32 CurveCompositeCount = 0;
	int32 CurveEllipseCount = 0;
	int32 CurveHelixCount = 0;
	int32 CurveHyperbolaCount = 0;
	int32 CurveLineCount = 0;
	int32 CurveNurbsCount = 0;
	int32 CurveParabolaCount = 0;
	int32 CurvePolyLineCount = 0;
	int32 CurveBlend02BoundaryCount = 0;
	int32 CurveEquationCount = 0;
	int32 CurveIntersectionCount = 0;
	int32 CurveOffsetCount = 0;
	int32 CurveOnSurfCount = 0;
	int32 CurveTransformCount = 0;

	int32 CurveAsNurbsCount = 0;

	FDuration LoadFileDuration;
	FDuration ParsingDuration;

	void Print();
};

}

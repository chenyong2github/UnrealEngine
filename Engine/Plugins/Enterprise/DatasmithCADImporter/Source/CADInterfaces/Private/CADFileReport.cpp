// Copyright Epic Games, Inc. All Rights Reserved.

#include "CADFileReport.h"

#include "CADKernel/Core/Chrono.h"
#include "CADKernel/Core/Types.h"
#include "CADKernel/UI/Message.h"

namespace CADKernel
{

void FCADFileReport::Print()
{
	FMessage::FillReportFile(TEXT("Body"), BodyCount);   
	FMessage::FillReportFile(TEXT("Shell"), ShellCount);
	FMessage::FillReportFile(TEXT("Face"), FaceCount);
	FMessage::FillReportFile(TEXT("Failed Face"), FailedFaceCount);
	FMessage::FillReportFile(TEXT("Loop"), LoopCount);
	FMessage::FillReportFile(TEXT("Deg Loop"), DegeneratedLoopCount);
	FMessage::FillReportFile(TEXT("Doubt Ori"), DoubtfulLoopOrientationCount);

	FMessage::FillReportFile(TEXT("Edge"), EdgeCount);
	FMessage::FillReportFile(TEXT("Deg Edge"), DegeneratedEdgeCount);
	
	FMessage::FillReportFile(TEXT(""), TEXT(""));
	FMessage::FillReportFile(TEXT("Surface"), SurfaceCount);
	FMessage::FillReportFile(TEXT("Deg Surf"), DegeneratedSurfaceCount);

	FMessage::FillReportFile(TEXT(""), TEXT(""));
	FMessage::FillReportFile(TEXT("Cone"), ConeSurfaceCount);
	FMessage::FillReportFile(TEXT("Cylin"), CylinderSurfaceCount);
	FMessage::FillReportFile(TEXT("LTrans"), LinearTransfoSurfaceCount);
	FMessage::FillReportFile(TEXT("Nurbs"), NurbsSurfaceCount);
	FMessage::FillReportFile(TEXT("Offset"), OffsetSurfaceCount);
	FMessage::FillReportFile(TEXT("Plane"), PlaneSurfaceCount);
	FMessage::FillReportFile(TEXT("Revol"), RevolutionSurfaceCount);
	FMessage::FillReportFile(TEXT("Ruled"), RuledSurfaceCount);
	FMessage::FillReportFile(TEXT("Sphere"), SphereSurfaceCount);
	FMessage::FillReportFile(TEXT("Torus"), TorusSurfaceCount);

	FMessage::FillReportFile(TEXT("Blend01"), Blend01SurfaceCount);
	FMessage::FillReportFile(TEXT("Blend02"), Blend02SurfaceCount);
	FMessage::FillReportFile(TEXT("Blend03"), Blend03SurfaceCount);
	FMessage::FillReportFile(TEXT("Cylin"), CylindricalSurfaceCount);
	FMessage::FillReportFile(TEXT("PipeS"), PipeSurfaceCount);
	FMessage::FillReportFile(TEXT("Extru"), ExtrusionSurfaceCount);
	FMessage::FillReportFile(TEXT("SurfFCur"), SurfaceFromCurvesCount);
	FMessage::FillReportFile(TEXT("SurfTrans"), TransformSurfaceCount);

	FMessage::FillReportFile(TEXT("SurfAsNurbs"), SurfaceAsNurbsCount);
	FMessage::FillReportFile(TEXT(""), TEXT(""));

	FMessage::FillReportFile(TEXT("Restric"), RestrictionCurveCount);

	FMessage::FillReportFile(TEXT("Curve"), CurveCount);
	FMessage::FillReportFile(TEXT("Circle"), CurveCircleCount);
	FMessage::FillReportFile(TEXT("Composite"), CurveCompositeCount);
	FMessage::FillReportFile(TEXT("Ellipse"), CurveEllipseCount);
	FMessage::FillReportFile(TEXT("Helix"), CurveHelixCount);
	FMessage::FillReportFile(TEXT("Hyperbola"), CurveHyperbolaCount);
	FMessage::FillReportFile(TEXT("Line"), CurveLineCount);
	FMessage::FillReportFile(TEXT("Nurbs"), CurveNurbsCount);
	FMessage::FillReportFile(TEXT("Parabola"), CurveParabolaCount);
	FMessage::FillReportFile(TEXT("PolyLine"), CurvePolyLineCount);
	FMessage::FillReportFile(TEXT("Bld02Bound"), CurveBlend02BoundaryCount);
	FMessage::FillReportFile(TEXT("Equation"), CurveEquationCount);
	FMessage::FillReportFile(TEXT("Intersection"), CurveIntersectionCount);
	FMessage::FillReportFile(TEXT("Offset"), CurveOffsetCount);
	FMessage::FillReportFile(TEXT("CrvOnSurf"), CurveOnSurfCount);
	FMessage::FillReportFile(TEXT("CrvTrans"), CurveTransformCount);
	FMessage::FillReportFile(TEXT("CrvAsNurbs"), CurveAsNurbsCount);

	FMessage::FillReportFile(TEXT(""), TEXT(""));
	FMessage::FillReportFile(TEXT("Load"), LoadFileDuration);
	FMessage::FillReportFile(TEXT("Pars"), ParsingDuration);

	FMessage::FillReportFile(TEXT(""), TEXT(""));
}

}

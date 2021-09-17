// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryScript/MeshPrimitiveFunctions.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/MeshNormals.h"
#include "DynamicMesh/MeshTransforms.h"
#include "DynamicMeshEditor.h"
#include "UDynamicMesh.h"

#include "Generators/GridBoxMeshGenerator.h"
#include "Generators/SphereGenerator.h"
#include "Generators/BoxSphereGenerator.h"
#include "Generators/CapsuleGenerator.h"
#include "Generators/RectangleMeshGenerator.h"
#include "Generators/DiscMeshGenerator.h"
#include "Generators/SweepGenerator.h"
#include "Generators/FlatTriangulationMeshGenerator.h"
#include "ConstrainedDelaunay2.h"
#include "Arrangement2d.h"
#include "Util/RevolveUtil.h"

#include "CompositionOps/CurveSweepOp.h"

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;

#define LOCTEXT_NAMESPACE "UGeometryScriptLibrary_MeshPrimitiveFunctions"


static void AppendPrimitive(
	UDynamicMesh* TargetMesh, 
	FMeshShapeGenerator* Generator, 
	FTransform Transform, 
	FGeometryScriptPrimitiveOptions PrimitiveOptions,
	FVector3d PreTranslate = FVector3d::Zero())
{
	auto ApplyOptionsToMesh = [&Transform, &PrimitiveOptions, PreTranslate](FDynamicMesh3& Mesh)
	{
		if (PreTranslate.SquaredLength() > 0)
		{
			MeshTransforms::Translate(Mesh, PreTranslate);
		}

		MeshTransforms::ApplyTransform(Mesh, (UE::Geometry::FTransform3d)Transform);
		if (PrimitiveOptions.PolygroupMode == EGeometryScriptPrimitivePolygroupMode::SingleGroup)
		{
			for (int32 tid : Mesh.TriangleIndicesItr())
			{
				Mesh.SetTriangleGroup(tid, 0);
			}
		}
		if (PrimitiveOptions.bFlipOrientation)
		{
			Mesh.ReverseOrientation(true);
			if (Mesh.HasAttributes())
			{
				FDynamicMeshNormalOverlay* Normals = Mesh.Attributes()->PrimaryNormals();
				for (int elemid : Normals->ElementIndicesItr())
				{
					Normals->SetElement(elemid, -Normals->GetElement(elemid));
				}
			}
		}
	};

	if (TargetMesh->IsEmpty())
	{
		TargetMesh->EditMesh([&](FDynamicMesh3& EditMesh)
		{
			EditMesh.Copy(Generator);
			ApplyOptionsToMesh(EditMesh);
		}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, false);
	}
	else
	{
		FDynamicMesh3 TempMesh(Generator);
		ApplyOptionsToMesh(TempMesh);
		TargetMesh->EditMesh([&](FDynamicMesh3& EditMesh)
		{
			FMeshIndexMappings TmpMappings;
			FDynamicMeshEditor Editor(&EditMesh);
			Editor.AppendMesh(&TempMesh, TmpMappings);

		}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, false);
	}
}




UDynamicMesh* UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(
	UDynamicMesh* TargetMesh,
	FGeometryScriptPrimitiveOptions PrimitiveOptions,
	FTransform Transform,
	float DimensionX,
	float DimensionY,
	float DimensionZ,
	int32 StepsX,
	int32 StepsY,
	int32 StepsZ,
	EGeometryScriptPrimitiveOriginMode Origin,
	UGeometryScriptDebug* Debug)
{
	if (TargetMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("PrimitiveFunctions_AppendBox", "AppendBox: TargetMesh is Null"));
		return TargetMesh;
	}

	UE::Geometry::FAxisAlignedBox3d ConvertBox(
		FVector3d(-DimensionX / 2, -DimensionY / 2, 0),
		FVector3d(DimensionX / 2, DimensionY / 2, DimensionZ));
	
	// todo: if Steps X/Y/Z are zero, can use trivial box generator

	FGridBoxMeshGenerator GridBoxGenerator;
	GridBoxGenerator.Box = UE::Geometry::FOrientedBox3d(ConvertBox);
	GridBoxGenerator.EdgeVertices = FIndex3i(FMath::Max(0, StepsX), FMath::Max(0, StepsY), FMath::Max(0, StepsZ));
	GridBoxGenerator.bPolygroupPerQuad = (PrimitiveOptions.PolygroupMode == EGeometryScriptPrimitivePolygroupMode::PerQuad);
	GridBoxGenerator.Generate();

	FVector3d OriginShift = (Origin == EGeometryScriptPrimitiveOriginMode::Center) ? FVector3d(0, 0, -DimensionZ/2) : FVector3d::Zero();
	AppendPrimitive(TargetMesh, &GridBoxGenerator, Transform, PrimitiveOptions, OriginShift);

	return TargetMesh;
}



UDynamicMesh* UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendSphereLatLong(
	UDynamicMesh* TargetMesh,
	FGeometryScriptPrimitiveOptions PrimitiveOptions,
	FTransform Transform,
	float Radius,
	int32 StepsPhi,
	int32 StepsTheta,
	EGeometryScriptPrimitiveOriginMode Origin,
	UGeometryScriptDebug* Debug)
{
	if (TargetMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("PrimitiveFunctions_AppendSphereLatLong", "AppendSphereLatLong: TargetMesh is Null"));
		return TargetMesh;
	}

	FSphereGenerator SphereGenerator;
	SphereGenerator.Radius = FMath::Max(FMathf::ZeroTolerance, Radius);
	SphereGenerator.NumPhi = FMath::Max(3, StepsPhi);
	SphereGenerator.NumTheta = FMath::Max(3, StepsTheta);
	SphereGenerator.bPolygroupPerQuad = (PrimitiveOptions.PolygroupMode == EGeometryScriptPrimitivePolygroupMode::PerQuad);
	SphereGenerator.Generate();

	FVector3d OriginShift = (Origin == EGeometryScriptPrimitiveOriginMode::Base) ? FVector3d(0, 0, Radius) : FVector3d::Zero();
	AppendPrimitive(TargetMesh, &SphereGenerator, Transform, PrimitiveOptions, OriginShift);

	return TargetMesh;
}



UDynamicMesh* UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendSphereBox(
	UDynamicMesh* TargetMesh,
	FGeometryScriptPrimitiveOptions PrimitiveOptions,
	FTransform Transform,
	float Radius,
	int32 StepsX,
	int32 StepsY,
	int32 StepsZ,
	EGeometryScriptPrimitiveOriginMode Origin,
	UGeometryScriptDebug* Debug)
{
	if (TargetMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("PrimitiveFunctions_AppendSphereBox", "AppendSphereBox: TargetMesh is Null"));
		return TargetMesh;
	}

	FBoxSphereGenerator SphereGenerator;
	SphereGenerator.Radius = FMath::Max(FMathf::ZeroTolerance, Radius);
	SphereGenerator.EdgeVertices = FIndex3i(FMath::Max(0, StepsX), FMath::Max(0, StepsY), FMath::Max(0, StepsZ));
	SphereGenerator.bPolygroupPerQuad = (PrimitiveOptions.PolygroupMode == EGeometryScriptPrimitivePolygroupMode::PerQuad);
	SphereGenerator.Generate();

	FVector3d OriginShift = (Origin == EGeometryScriptPrimitiveOriginMode::Base) ? FVector3d(0, 0, Radius) : FVector3d::Zero();
	AppendPrimitive(TargetMesh, &SphereGenerator, Transform, PrimitiveOptions, OriginShift);

	return TargetMesh;
}




UDynamicMesh* UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendCapsule(
	UDynamicMesh* TargetMesh,
	FGeometryScriptPrimitiveOptions PrimitiveOptions,
	FTransform Transform,
	float Radius,
	float LineLength,
	int32 HemisphereSteps,
	int32 CircleSteps,
	EGeometryScriptPrimitiveOriginMode Origin,
	UGeometryScriptDebug* Debug)
{
	if (TargetMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("PrimitiveFunctions_AppendCapsule", "AppendCapsule: TargetMesh is Null"));
		return TargetMesh;
	}

	FCapsuleGenerator CapsuleGenerator;
	CapsuleGenerator.Radius = FMath::Max(FMathf::ZeroTolerance, Radius);
	CapsuleGenerator.SegmentLength = FMath::Max(FMathf::ZeroTolerance, LineLength);
	CapsuleGenerator.NumHemisphereArcSteps = FMath::Max(2, HemisphereSteps);
	CapsuleGenerator.NumCircleSteps = FMath::Max(3, CircleSteps);
	CapsuleGenerator.bPolygroupPerQuad = (PrimitiveOptions.PolygroupMode == EGeometryScriptPrimitivePolygroupMode::PerQuad);
	CapsuleGenerator.Generate();

	FVector3d OriginShift = FVector3d::Zero();
	OriginShift.Z = (Origin == EGeometryScriptPrimitiveOriginMode::Center) ? -(LineLength / 2) : Radius;
	AppendPrimitive(TargetMesh, &CapsuleGenerator, Transform, PrimitiveOptions, OriginShift);

	return TargetMesh;
}




UDynamicMesh* UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendCylinder(
	UDynamicMesh* TargetMesh,
	FGeometryScriptPrimitiveOptions PrimitiveOptions,
	FTransform Transform,
	float Radius,
	float Height,
	int32 RadialSteps,
	int32 HeightSteps,
	bool bCapped,
	EGeometryScriptPrimitiveOriginMode Origin,
	UGeometryScriptDebug* Debug)
{
	if (TargetMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("PrimitiveFunctions_AppendCylinder", "AppendCylinder: TargetMesh is Null"));
		return TargetMesh;
	}

	FCylinderGenerator CylinderGenerator;
	CylinderGenerator.Radius[0] = FMath::Max(FMathf::ZeroTolerance, Radius);
	CylinderGenerator.Radius[1] = CylinderGenerator.Radius[0];
	CylinderGenerator.Height = FMath::Max(FMathf::ZeroTolerance, Height);
	CylinderGenerator.AngleSamples = FMath::Max(3, RadialSteps);
	CylinderGenerator.LengthSamples = FMath::Max(0, HeightSteps);
	CylinderGenerator.bCapped = bCapped;
	CylinderGenerator.bPolygroupPerQuad = (PrimitiveOptions.PolygroupMode == EGeometryScriptPrimitivePolygroupMode::PerQuad);
	CylinderGenerator.Generate();

	FVector3d OriginShift = (Origin == EGeometryScriptPrimitiveOriginMode::Center) ? FVector3d(0, 0, -(Height/2)) : FVector3d::Zero();
	AppendPrimitive(TargetMesh, &CylinderGenerator, Transform, PrimitiveOptions, OriginShift);

	return TargetMesh;
}



UDynamicMesh* UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendCone(
	UDynamicMesh* TargetMesh,
	FGeometryScriptPrimitiveOptions PrimitiveOptions,
	FTransform Transform,
	float BaseRadius,
	float TopRadius,
	float Height,
	int32 RadialSteps,
	int32 HeightSteps,
	bool bCapped,
	EGeometryScriptPrimitiveOriginMode Origin,
	UGeometryScriptDebug* Debug)
{
	if (TargetMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("PrimitiveFunctions_AppendCone", "AppendCone: TargetMesh is Null"));
		return TargetMesh;
	}

	FCylinderGenerator CylinderGenerator;
	CylinderGenerator.Radius[0] = FMath::Max(FMathf::ZeroTolerance, BaseRadius);
	CylinderGenerator.Radius[1] = FMath::Max(0, TopRadius);
	CylinderGenerator.Height = FMath::Max(FMathf::ZeroTolerance, Height);
	CylinderGenerator.AngleSamples = FMath::Max(3, RadialSteps);
	CylinderGenerator.LengthSamples = FMath::Max(0, HeightSteps);
	CylinderGenerator.bCapped = bCapped;
	CylinderGenerator.bPolygroupPerQuad = (PrimitiveOptions.PolygroupMode == EGeometryScriptPrimitivePolygroupMode::PerQuad);
	CylinderGenerator.Generate();

	FVector3d OriginShift = (Origin == EGeometryScriptPrimitiveOriginMode::Center) ? FVector3d(0, 0, -(Height/2)) : FVector3d::Zero();
	AppendPrimitive(TargetMesh, &CylinderGenerator, Transform, PrimitiveOptions, OriginShift);

	return TargetMesh;
}


UDynamicMesh* UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendTorus(
	UDynamicMesh* TargetMesh,
	FGeometryScriptPrimitiveOptions PrimitiveOptions,
	FTransform Transform,
	float MajorRadius,
	float MinorRadius,
	int32 MajorSteps,
	int32 MinorSteps,
	EGeometryScriptPrimitiveOriginMode Origin,
	UGeometryScriptDebug* Debug)
{
	FPolygon2d Circle = FPolygon2d::MakeCircle(FMath::Max(FMathf::ZeroTolerance, MinorRadius), FMath::Max(3, MinorSteps));
	TArray<FVector2D> PolygonVertices;
	FVector2d Shift = (Origin == EGeometryScriptPrimitiveOriginMode::Base) ? FVector2d(0, MinorRadius) : FVector2d::Zero();
	for (FVector2d v : Circle.GetVertices())
	{
		PolygonVertices.Add((FVector2D)(v+Shift));
	}
	return AppendSimpleRevolvePolygon(TargetMesh, PrimitiveOptions, Transform, PolygonVertices, MajorRadius, MajorSteps, Debug);
}


UDynamicMesh* UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendSimpleRevolvePolygon(
	UDynamicMesh* TargetMesh,
	FGeometryScriptPrimitiveOptions PrimitiveOptions,
	FTransform Transform,
	const TArray<FVector2D>& PolygonVertices,
	float Radius,
	int32 Steps,
	UGeometryScriptDebug* Debug)
{
	if (TargetMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("AppendRevolvePolygon_NullMesh", "AppendRevolvePolygon: TargetMesh is Null"));
		return TargetMesh;
	}
	if (PolygonVertices.Num() < 3)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("AppendRevolvePolygon_InvalidPolygon", "AppendRevolvePolygon: PolygonVertices array requires at least 3 positions"));
		return TargetMesh;
	}

	FGeneralizedCylinderGenerator RevolveGen;
	for (FVector2D Point : PolygonVertices)
	{
		RevolveGen.CrossSection.AppendVertex(FVector2d(Point.X, Point.Y));
	}
	
	FPolygon2d PathPoly = FPolygon2d::MakeCircle(FMath::Max(FMathf::ZeroTolerance, Radius), FMath::Max(3, Steps));
	for (FVector2d v : PathPoly.GetVertices())
	{
		RevolveGen.Path.Add(FVector3d(v.X, v.Y, 0.0));
	}
	Algo::Reverse(RevolveGen.Path);

	RevolveGen.bLoop = true;
	RevolveGen.bCapped = false;
	RevolveGen.bPolygroupPerQuad = (PrimitiveOptions.PolygroupMode == EGeometryScriptPrimitivePolygroupMode::PerQuad);
	RevolveGen.InitialFrame = FFrame3d(RevolveGen.Path[0], FVector3d::UnitX(), FVector3d::UnitZ(), -FVector3d::UnitY());
	RevolveGen.Generate();

	AppendPrimitive(TargetMesh, &RevolveGen, Transform, PrimitiveOptions);
	return TargetMesh;
}




UDynamicMesh* UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendRevolvePath(
	UDynamicMesh* TargetMesh,
	FGeometryScriptPrimitiveOptions PrimitiveOptions,
	FTransform Transform,
	const TArray<FVector2D>& PathVertices,
	FGeometryScriptRevolveOptions RevolveOptions,
	int32 Steps,
	bool bCapped,
	UGeometryScriptDebug* Debug)
{
	if (TargetMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("AppendRevolvePath_NullMesh", "AppendRevolvePath: TargetMesh is Null"));
		return TargetMesh;
	}
	if (PathVertices.Num() < 2)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("AppendRevolvePath_InvalidPolygon", "AppendRevolvePath: PathVertices array requires at least 2 positions"));
		return TargetMesh;
	}

	FCurveSweepOp CurveSweepOp;

	FVector3d AxisDirection(0, 0, 1);
	FVector3d AxisOrigin(0, 0, 0);
	for (FVector2D Point : PathVertices)
	{
		CurveSweepOp.ProfileCurve.Add(FVector3d(Point.X, 0, Point.Y));
	}
	// unclear why but the sweep code seems to be written for a clockwise ordering...
	Algo::Reverse(CurveSweepOp.ProfileCurve);

	// Project first and last points onto the revolution axis to cap it
	if (bCapped)
	{
		FVector3d FirstPoint = CurveSweepOp.ProfileCurve[0];
		FVector3d LastPoint = CurveSweepOp.ProfileCurve.Last();

		double DistanceAlongAxis = AxisDirection.Dot(LastPoint - AxisOrigin);
		FVector3d ProjectedPoint = AxisOrigin + (AxisDirection * DistanceAlongAxis);
		CurveSweepOp.ProfileCurve.Add(ProjectedPoint);

		DistanceAlongAxis = AxisDirection.Dot(FirstPoint - AxisOrigin);
		ProjectedPoint = AxisOrigin + (AxisDirection * DistanceAlongAxis);
		CurveSweepOp.ProfileCurve.Add(ProjectedPoint);

		CurveSweepOp.bProfileCurveIsClosed = true;
	}
	else
	{
		CurveSweepOp.bProfileCurveIsClosed = false;
	}

	//double TotalRevolutionDegrees = RevolveDegrees;// (AlongAxisOffsetPerDegree == 0) ? ClampedRevolutionDegrees : RevolutionDegrees;
	double TotalRevolutionDegrees = FMath::Clamp(RevolveOptions.RevolveDegrees, 0.1f, 360.0f);

	double DegreesPerStep = TotalRevolutionDegrees / (double)Steps;
	double DegreesOffset = RevolveOptions.DegreeOffset;
	if (RevolveOptions.ReverseDirection)
	{
		DegreesPerStep *= -1;
		DegreesOffset *= -1;
	}
	//double DownAxisOffsetPerStep = TotalRevolutionDegrees * AlongAxisOffsetPerDegree / Steps;

	if (RevolveOptions.bProfileAtMidpoint && DegreesPerStep != 0 && FMathd::Abs(DegreesPerStep) < 180)
	{
		RevolveUtil::MakeProfileCurveMidpointOfFirstStep(CurveSweepOp.ProfileCurve, DegreesPerStep, AxisOrigin, AxisDirection);
	}

	// Generate the sweep curve
	//CurveSweepOpOut.bSweepCurveIsClosed = bWeldFullRevolution && AlongAxisOffsetPerDegree == 0 && TotalRevolutionDegrees == 360;
	CurveSweepOp.bSweepCurveIsClosed = (TotalRevolutionDegrees == 360);
	int32 NumSweepFrames = CurveSweepOp.bSweepCurveIsClosed ? Steps : Steps + 1; // If closed, last sweep frame is also first
	CurveSweepOp.SweepCurve.Reserve(NumSweepFrames);
	RevolveUtil::GenerateSweepCurve(AxisOrigin, AxisDirection, DegreesOffset,
		DegreesPerStep, 0.0, NumSweepFrames, CurveSweepOp.SweepCurve);

	// Weld any vertices that are on the axis
	RevolveUtil::WeldPointsOnAxis(CurveSweepOp.ProfileCurve, AxisOrigin,
		AxisDirection, 0.1, CurveSweepOp.ProfileVerticesToWeld);

	CurveSweepOp.bSharpNormals = RevolveOptions.bHardNormals;
	CurveSweepOp.SharpNormalAngleTolerance = RevolveOptions.HardNormalAngle;
	//CurveSweepOpOut.DiagonalTolerance = DiagonalProportionTolerance;
	//double UVScale = MaterialProperties.UVScale;
	double UVScale = 1.0;
	CurveSweepOp.UVScale = FVector2d(UVScale, UVScale);
	//if (bReverseProfileCurve ^ bFlipVs)
	//{
	//	CurveSweepOpOut.UVScale[1] *= -1;
	//	CurveSweepOpOut.UVOffset = FVector2d(0, UVScale);
	//}
	CurveSweepOp.bUVsSkipFullyWeldedEdges = true;
	CurveSweepOp.bUVScaleRelativeWorld = true; // MaterialProperties.bWorldSpaceUVScale;
	CurveSweepOp.UnitUVInWorldCoordinates = 100; // This seems to be the case in the AddPrimitiveTool
	CurveSweepOp.QuadSplitMode = EProfileSweepQuadSplit::Uniform;

	if (PrimitiveOptions.PolygroupMode == EGeometryScriptPrimitivePolygroupMode::PerQuad)
	{
		CurveSweepOp.PolygonGroupingMode = EProfileSweepPolygonGrouping::PerFace;
	}
	if (PrimitiveOptions.PolygroupMode == EGeometryScriptPrimitivePolygroupMode::PerFace)
	{
		CurveSweepOp.PolygonGroupingMode = EProfileSweepPolygonGrouping::PerProfileSegment;
	}
	else
	{
		CurveSweepOp.PolygonGroupingMode = EProfileSweepPolygonGrouping::Single;
	}

	CurveSweepOp.CapFillMode = (bCapped) ?
		FCurveSweepOp::ECapFillMode::EarClipping : FCurveSweepOp::ECapFillMode::None;		// delaunay? hits ensure...

	CurveSweepOp.CalculateResult(nullptr);
	TUniquePtr<FDynamicMesh3> ResultMesh = CurveSweepOp.ExtractResult();
	MeshTransforms::ApplyTransform(*ResultMesh, (UE::Geometry::FTransform3d)Transform);

	if (PrimitiveOptions.bFlipOrientation)
	{
		ResultMesh->ReverseOrientation(true);
		if (ResultMesh->HasAttributes() && ResultMesh->Attributes()->PrimaryNormals() != nullptr)
		{
			FDynamicMeshNormalOverlay* Normals = ResultMesh->Attributes()->PrimaryNormals();
			for (int elemid : Normals->ElementIndicesItr())
			{
				Normals->SetElement(elemid, -Normals->GetElement(elemid));
			}
		}
	}

	if (TargetMesh->IsEmpty())
	{
		TargetMesh->SetMesh(MoveTemp(*ResultMesh));
	}
	else
	{
		TargetMesh->EditMesh([&](FDynamicMesh3& EditMesh)
		{
			FMeshIndexMappings TmpMappings;
			FDynamicMeshEditor Editor(&EditMesh);
			Editor.AppendMesh(ResultMesh.Get(), TmpMappings);

		}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, false);
	}

	return TargetMesh;
}



UDynamicMesh* UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendSimpleExtrudePolygon(
	UDynamicMesh* TargetMesh,
	FGeometryScriptPrimitiveOptions PrimitiveOptions,
	FTransform Transform,
	const TArray<FVector2D>& PolygonVertices,
	float Height,
	int32 HeightSteps,
	bool bCapped,
	EGeometryScriptPrimitiveOriginMode Origin,
	UGeometryScriptDebug* Debug)
{
	if (TargetMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("AppendSimpleExtrudePolygon_NullMesh", "AppendSimpleExtrudePolygon: TargetMesh is Null"));
		return TargetMesh;
	}
	if (PolygonVertices.Num() < 3)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("AppendSimpleExtrudePolygon_InvalidPolygon", "AppendSimpleExtrudePolygon: PolygonVertices array requires at least 3 positions"));
		return TargetMesh;
	}

	FGeneralizedCylinderGenerator ExtrudeGen;
	for (const FVector2D& Point : PolygonVertices)
	{
		ExtrudeGen.CrossSection.AppendVertex((FVector2d)Point);
	}

	int32 NumDivisions = FMath::Max(1, HeightSteps - 1);
	int32 NumPathSteps = NumDivisions + 1;
	double StepSize = (double)Height / (double)NumDivisions;

	for (int32 k = 0; k <= NumPathSteps; ++k)
	{
		double StepHeight = (k == NumPathSteps) ? Height : ((double)k * StepSize);
		ExtrudeGen.Path.Add(FVector3d(0, 0, StepHeight));
	}

	ExtrudeGen.InitialFrame = FFrame3d();
	ExtrudeGen.bCapped = bCapped;
	ExtrudeGen.bPolygroupPerQuad = (PrimitiveOptions.PolygroupMode == EGeometryScriptPrimitivePolygroupMode::PerQuad);
	ExtrudeGen.Generate();

	FVector3d OriginShift = (Origin == EGeometryScriptPrimitiveOriginMode::Center) ? FVector3d(0, 0, -(Height/2)) : FVector3d::Zero();
	AppendPrimitive(TargetMesh, &ExtrudeGen, Transform, PrimitiveOptions, OriginShift);
	return TargetMesh;
}



UDynamicMesh* UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendSimpleSweptPolygon(
	UDynamicMesh* TargetMesh,
	FGeometryScriptPrimitiveOptions PrimitiveOptions,
	FTransform Transform,
	const TArray<FVector2D>& PolygonVertices,
	const TArray<FVector>& SweepPath,
	bool bLoop,
	bool bCapped,
	float StartScale,
	float EndScale,
	UGeometryScriptDebug* Debug)
{
	if (TargetMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("AppendSimpleSweptPolygon_NullMesh", "AppendSimpleSweptPolygon: TargetMesh is Null"));
		return TargetMesh;
	}
	if (PolygonVertices.Num() < 3)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("AppendSimpleSweptPolygon_InvalidPolygon", "AppendSimpleSweptPolygon: PolygonVertices array requires at least 3 positions"));
		return TargetMesh;
	}

	FGeneralizedCylinderGenerator SweepGen;
	for (FVector2D Point : PolygonVertices)
	{
		SweepGen.CrossSection.AppendVertex(FVector2d(Point.X, Point.Y));
	}
	for (FVector SweepPathPos : SweepPath)
	{
		SweepGen.Path.Add(SweepPathPos);
	}

	SweepGen.bLoop = bLoop;
	SweepGen.bCapped = bCapped;
	SweepGen.bPolygroupPerQuad = (PrimitiveOptions.PolygroupMode == EGeometryScriptPrimitivePolygroupMode::PerQuad);
	SweepGen.InitialFrame = FFrame3d(SweepGen.Path[0]);
	SweepGen.StartScale = StartScale;
	SweepGen.EndScale = EndScale;

	SweepGen.Generate();

	AppendPrimitive(TargetMesh, &SweepGen, Transform, PrimitiveOptions);
	return TargetMesh;
}




UDynamicMesh* UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendRectangle(
	UDynamicMesh* TargetMesh,
	FGeometryScriptPrimitiveOptions PrimitiveOptions,
	FTransform Transform,
	float DimensionX,
	float DimensionY,
	int32 StepsWidth,
	int32 StepsHeight,
	UGeometryScriptDebug* Debug)
{
	if (TargetMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("PrimitiveFunctions_AppendRectangle", "AppendRectangle: TargetMesh is Null"));
		return TargetMesh;
	}

	FRectangleMeshGenerator RectGenerator;
	RectGenerator.Origin = FVector3d(0, 0, 0);
	RectGenerator.Normal = FVector3f::UnitZ();
	RectGenerator.Width = DimensionX / 2;
	RectGenerator.Height = DimensionY / 2;
	RectGenerator.WidthVertexCount = FMath::Max(0, StepsWidth);
	RectGenerator.HeightVertexCount = FMath::Max(0, StepsHeight);
	RectGenerator.bSinglePolygroup = (PrimitiveOptions.PolygroupMode != EGeometryScriptPrimitivePolygroupMode::PerQuad);
	RectGenerator.Generate();

	AppendPrimitive(TargetMesh, &RectGenerator, Transform, PrimitiveOptions);

	return TargetMesh;
}




UDynamicMesh* UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendRoundRectangle(
	UDynamicMesh* TargetMesh,
	FGeometryScriptPrimitiveOptions PrimitiveOptions,
	FTransform Transform,
	float DimensionX,
	float DimensionY,
	float CornerRadius,
	int32 StepsWidth,
	int32 StepsHeight,
	int32 StepsRound,
	UGeometryScriptDebug* Debug)
{
	if (TargetMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("PrimitiveFunctions_AppendRoundRectangle", "AppendRoundRectangle: TargetMesh is Null"));
		return TargetMesh;
	}

	FRoundedRectangleMeshGenerator RectGenerator;
	RectGenerator.Origin = FVector3d(0, 0, 0);
	RectGenerator.Normal = FVector3f::UnitZ();
	RectGenerator.Width = DimensionX / 2;
	RectGenerator.Height = DimensionY / 2;
	RectGenerator.WidthVertexCount = FMath::Max(0, StepsWidth);
	RectGenerator.HeightVertexCount = FMath::Max(0, StepsHeight);
	RectGenerator.Radius = FMath::Max(FMathf::ZeroTolerance, CornerRadius);
	RectGenerator.AngleSamples = FMath::Max(StepsRound, 3);
	RectGenerator.bSinglePolygroup = (PrimitiveOptions.PolygroupMode != EGeometryScriptPrimitivePolygroupMode::PerQuad);
	RectGenerator.Generate();

	AppendPrimitive(TargetMesh, &RectGenerator, Transform, PrimitiveOptions);

	return TargetMesh;
}



UDynamicMesh* UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendDisc(
	UDynamicMesh* TargetMesh,
	FGeometryScriptPrimitiveOptions PrimitiveOptions,
	FTransform Transform,
	float Radius,
	int32 AngleSteps,
	int32 SpokeSteps,
	float StartAngle,
	float EndAngle,
	float HoleRadius,
	UGeometryScriptDebug* Debug)
{
	if (TargetMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("PrimitiveFunctions_AppendDisc", "AppendDisc: TargetMesh is Null"));
		return TargetMesh;
	}

	FDiscMeshGenerator DiscGenerator;
	FPuncturedDiscMeshGenerator PuncturedDiscGenerator;
	FDiscMeshGenerator* UseGenerator = &DiscGenerator;

	if (HoleRadius > 0)
	{
		PuncturedDiscGenerator.HoleRadius = HoleRadius;
		UseGenerator = &PuncturedDiscGenerator;
	}

	UseGenerator->Radius = FMath::Max(FMathf::ZeroTolerance, Radius);
	UseGenerator->Normal = FVector3f::UnitZ();
	UseGenerator->AngleSamples = FMath::Max(3, AngleSteps);
	UseGenerator->RadialSamples = FMath::Max(3, SpokeSteps);
	UseGenerator->StartAngle = StartAngle;
	UseGenerator->EndAngle = EndAngle;
	UseGenerator->bSinglePolygroup = (PrimitiveOptions.PolygroupMode != EGeometryScriptPrimitivePolygroupMode::PerQuad);
	UseGenerator->Generate();
	AppendPrimitive(TargetMesh, UseGenerator, Transform, PrimitiveOptions);

	return TargetMesh;
}


UDynamicMesh* UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendTriangulatedPolygon(
	UDynamicMesh* TargetMesh,
	FGeometryScriptPrimitiveOptions PrimitiveOptions,
	FTransform Transform,
	const TArray<FVector2D>& PolygonVertices,
	bool bAllowSelfIntersections,
	UGeometryScriptDebug* Debug)
{
	if (TargetMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("AppendTriangulatedPolygon_InvalidInput", "AppendTriangulatedPolygon: TargetMesh is Null"));
		return TargetMesh;
	}
	if (PolygonVertices.Num() < 3)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("AppendTriangulatedPolygon_InvalidPolygon", "AppendTriangulatedPolygon: PolygonVertices array requires at least 3 positions"));
		return TargetMesh;
	}

	FPolygon2d Polygon;
	for (FVector2D Vertex : PolygonVertices)
	{
		Polygon.AppendVertex(Vertex);
	}
	FGeneralPolygon2d GeneralPolygon(Polygon);

	FConstrainedDelaunay2d Triangulator;
	if (bAllowSelfIntersections)
	{
		FArrangement2d Arrangement(Polygon.Bounds());
		// arrangement2d builds a general 2d graph that discards orientation info ...
		Triangulator.FillRule = FConstrainedDelaunay2d::EFillRule::Odd;
		Triangulator.bOrientedEdges = false;
		Triangulator.bSplitBowties = true;
		for (FSegment2d Seg : GeneralPolygon.GetOuter().Segments())
		{
			Arrangement.Insert(Seg);
		}
		Triangulator.Add(Arrangement.Graph);
	}
	else
	{
		Triangulator.Add(GeneralPolygon);
	}

	bool bTriangulationSuccess = Triangulator.Triangulate([&GeneralPolygon](const TArray<FVector2d>& Vertices, FIndex3i Tri) 
	{
		return GeneralPolygon.Contains((Vertices[Tri.A] + Vertices[Tri.B] + Vertices[Tri.C]) / 3.0);	// keep triangles based on the input polygon's winding
	});

	// even if bTriangulationSuccess is false, there may still be some triangles, so only fail if the mesh is empty
	if (Triangulator.Triangles.Num() == 0)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::OperationFailed, LOCTEXT("AppendTriangulatedPolygon_Failed", "AppendTriangulatedPolygon: Failed to triangulate polygon"));
		return TargetMesh;
	}

	FFlatTriangulationMeshGenerator TriangulationMeshGen;
	TriangulationMeshGen.Vertices2D = Triangulator.Vertices;
	TriangulationMeshGen.Triangles2D = Triangulator.Triangles;
	AppendPrimitive(TargetMesh, &TriangulationMeshGen.Generate(), Transform, PrimitiveOptions);

	return TargetMesh;
}



#undef LOCTEXT_NAMESPACE
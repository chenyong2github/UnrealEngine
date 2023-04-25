// Copyright Epic Games, Inc. All Rights Reserved.

#include "CurveOps/TriangulateCurvesOp.h"

#include "Util/ProgressCancel.h"
#include "BoxTypes.h"
#include "CompGeom/PolygonTriangulation.h"
#include "Operations/ExtrudeMesh.h"
#include "DynamicMesh/MeshNormals.h"

#include "Components/SplineComponent.h"

using namespace UE::Geometry;


void FTriangulateCurvesOp::AddSpline(USplineComponent* Spline, double ErrorTolerance)
{
	FCurvePath& Path = Paths.Emplace_GetRef();
	Spline->ConvertSplineToPolyLine(ESplineCoordinateSpace::Type::World, ErrorTolerance * ErrorTolerance, Path.Vertices);
	Path.bClosed = Spline->IsClosedLoop();
	if (Paths.Num() == 1)
	{
		FirstPathTransform = Spline->GetComponentTransform();
	}
}

void FTriangulateCurvesOp::CalculateResult(FProgressCancel* Progress)
{
	if (Progress && Progress->Cancelled())
	{
		return;
	}

	ResultMesh->EnableAttributes();
	ResultMesh->EnableTriangleGroups();
	ResultMesh->EnableVertexNormals(FVector3f::ZAxisVector);

	SetResultTransform(FTransformSRT3d(FirstPathTransform));

	FAxisAlignedBox3d InputBounds;
	for (const FCurvePath& Path : Paths)
	{
		InputBounds.Contain(Path.Vertices);
	}
	double UseUVScaleFactor = UVScaleFactor / InputBounds.MaxDim();

	auto AppendTriangles = [this, UseUVScaleFactor](FDynamicMesh3& Mesh, const TArray<FVector3d>& Vertices, const TArray<FIndex3i>& Tris, int32 GroupID, FVector3d PlaneOrigin, FVector3d Normal) -> void
	{
		if (Tris.IsEmpty())
		{
			return;
		}

		FFrame3d ProjectionFrame(ResultTransform.GetTranslation(), Normal);
		FVector3f LocalNormal = (FVector3f)ResultTransform.InverseTransformNormal(Normal);

		checkSlow(Mesh.IsCompact());
		int32 VertStart = Mesh.MaxVertexID();
		int32 TriStart = Mesh.MaxTriangleID();
		for (const FVector3d V : Vertices)
		{
			int32 VID = Mesh.AppendVertex(ResultTransform.InverseTransformPosition(V));
			Mesh.SetVertexNormal(VID, LocalNormal);
			int32 NormalEID = Mesh.Attributes()->PrimaryNormals()->AppendElement(LocalNormal);
			int32 UVEID = Mesh.Attributes()->PrimaryUV()->AppendElement(FVector2f(ProjectionFrame.ToPlaneUV(V) * UseUVScaleFactor));
			// since we always add vertices and overlay elements at the same time, the IDs must match up
			checkSlow(VID == NormalEID && NormalEID == UVEID);
		}
		for (const FIndex3i& T : Tris)
		{
			FIndex3i NewTri(T.A + VertStart, T.B + VertStart, T.C + VertStart);
			int32 NewTID = Mesh.AppendTriangle(NewTri, GroupID);
			Mesh.Attributes()->PrimaryNormals()->SetTriangle(NewTID, NewTri);
			Mesh.Attributes()->PrimaryUV()->SetTriangle(NewTID, NewTri);
		}
	};

	for (int32 PathIdx = 0; PathIdx < Paths.Num(); ++PathIdx)
	{
		FVector3d Normal, PlanePoint;
		PolygonTriangulation::ComputePolygonPlane<double>(Paths[PathIdx].Vertices, Normal, PlanePoint);
		TArray<FIndex3i> Tris;
		PolygonTriangulation::TriangulateSimplePolygon<double>(Paths[PathIdx].Vertices, Tris, false);
		AppendTriangles(*ResultMesh, Paths[PathIdx].Vertices, Tris, PathIdx, PlanePoint, Normal);
	}

	if (Thickness > 0)
	{
		FExtrudeMesh ExtrudeMesh(ResultMesh.Get());
		ExtrudeMesh.DefaultExtrudeDistance = Thickness;
		ExtrudeMesh.UVScaleFactor = UseUVScaleFactor;
		ExtrudeMesh.Apply();
	}

	if (Progress && Progress->Cancelled())
	{
		return;
	}
}


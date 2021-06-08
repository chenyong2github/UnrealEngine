// Copyright Epic Games, Inc. All Rights Reserved.

#include "CADKernel/Topo/TopologicalEdge.h"

#include "CADKernel/Core/KernelParameters.h"
#include "CADKernel/Geo/Curves/NURBSCurve.h"
#include "CADKernel/Geo/Curves/SegmentCurve.h"
#include "CADKernel/Geo/GeoEnum.h"
#include "CADKernel/Geo/Sampling/PolylineTools.h"
#include "CADKernel/Geo/Sampler/SamplerOnParam.h"
#include "CADKernel/Math/BSpline.h"
#include "CADKernel/Math/Point.h"
#include "CADKernel/Math/SlopeUtils.h"
#include "CADKernel/Mesh/Structure/EdgeMesh.h"
#include "CADKernel/Topo/TopologicalLoop.h"
#include "CADKernel/Topo/TopologicalLink.h"
#include "CADKernel/Topo/TopologicalFace.h"
#include "CADKernel/Topo/TopologicalVertex.h"

using namespace CADKernel;

FTopologicalEdge::FTopologicalEdge(const TSharedRef<FRestrictionCurve>& InCurve, const TSharedRef<FTopologicalVertex>& InVertex1, const TSharedRef<FTopologicalVertex>& InVertex2, const FLinearBoundary& InBoundary)
	: TLinkable<FTopologicalEdge, FEdgeLink>()
	, StartVertex(InVertex1)
	, EndVertex(InVertex2)
	, Boundary(InBoundary)
	, Curve(InCurve)
	, Length3D(-1)
	, Loop(TSharedPtr<FTopologicalLoop>())
	, Mesh(TSharedPtr<FEdgeMesh>())
{
	ensureCADKernel(Boundary.IsValid());
}

FTopologicalEdge::FTopologicalEdge(const TSharedRef<FRestrictionCurve>& InCurve, const TSharedRef<FTopologicalVertex>& InVertex1, const TSharedRef<FTopologicalVertex>& InVertex2)
	: TLinkable<FTopologicalEdge, FEdgeLink>()
	, StartVertex(InVertex1)
	, EndVertex(InVertex2)
	, Curve(InCurve)
	, Length3D(-1)
	, Loop(TSharedPtr<FTopologicalLoop>())
	, Mesh(TSharedPtr<FEdgeMesh>())
{
	Boundary = Curve->GetBoundary();
	ensureCADKernel(Boundary.IsValid());
}

FTopologicalEdge::FTopologicalEdge(const TSharedRef<FRestrictionCurve>& InCurve, const FLinearBoundary& InBoundary)
	: TLinkable<FTopologicalEdge, FEdgeLink>()
	, Boundary(InBoundary)
	, Curve(InCurve)
	, Length3D(-1)
	, Loop(TSharedPtr<FTopologicalLoop>())
	, Mesh(TSharedPtr<FEdgeMesh>())
{
	TArray<double> Coordinates = { Boundary.Min, Boundary.Max };
	TArray<FCurvePoint> Points;
	Curve->EvaluatePoints(Coordinates, Points);

	StartVertex = FEntity::MakeShared<FTopologicalVertex>(Points[0].Point);
	EndVertex = FEntity::MakeShared<FTopologicalVertex>(Points[1].Point);
}

FTopologicalEdge::FTopologicalEdge(const TSharedRef<FSurface>& InSurface, const FPoint2D& InCoordinateVertex1, const TSharedRef<FTopologicalVertex>& InVertex1, const FPoint2D& InCoordinateVertex2, const TSharedRef<FTopologicalVertex>& InVertex2)
	: TLinkable<FTopologicalEdge, FEdgeLink>()
	, StartVertex(InVertex1)
	, EndVertex(InVertex2)
	, Length3D(-1)
	, Loop(TSharedPtr<FTopologicalLoop>())
	, Mesh(TSharedPtr<FEdgeMesh>())
{
	TSharedRef<FSegmentCurve> Curve2D = FEntity::MakeShared<FSegmentCurve>(InCoordinateVertex1, InCoordinateVertex2, 2);
	Curve = FEntity::MakeShared<FRestrictionCurve>(InSurface, Curve2D);
}

FTopologicalEdge::FTopologicalEdge(const TSharedRef<FRestrictionCurve>& InCurve)
	: FTopologicalEdge(InCurve, InCurve->GetBoundary())
{
}

void FTopologicalEdge::LinkVertex()
{
	StartVertex->AddConnectedEdge(StaticCastSharedRef<FTopologicalEdge>(AsShared()));
	EndVertex->AddConnectedEdge(StaticCastSharedRef<FTopologicalEdge>(AsShared()));

	if (IsDegenerated())
	{
		StartVertex->Link(EndVertex.ToSharedRef());
	}
}

bool FTopologicalEdge::CheckVertices()
{
	TArray<double> Coordinates = { Boundary.Min, Boundary.Max };
	TArray<FPoint> Points;
	Curve->Approximate3DPoints(Coordinates, Points);

	double ToleranceGeo = GetTolerance3D();
	TFunction<bool(TSharedPtr<FTopologicalVertex>, FPoint)> CheckExtremityGap = [&](TSharedPtr<FTopologicalVertex> Vertex, FPoint Point)
	{
		double GapToVertex = Vertex->GetCoordinates().Distance(Point);
		return (GapToVertex < ToleranceGeo);
	};

	if (!CheckExtremityGap(StartVertex, Points[0]))
	{
		if (CheckExtremityGap(StartVertex, Points[1]) && CheckExtremityGap(EndVertex, Points[0]))
		{
			Swap(StartVertex, EndVertex);
			return true;
		}
		return false;
	}
	return CheckExtremityGap(EndVertex, Points[1]);
}

double FTopologicalEdge::GetTolerance3D()
{
	return GetCurve()->GetCarrierSurface()->Get3DTolerance();
}

bool FTopologicalEdge::CheckIfDegenerated()
{
	bool bDegeneration2D = false;
	bool bDegeneration3D = false;

	Curve->CheckIfDegenerated(Boundary, bDegeneration2D, bDegeneration3D, Length3D);

	if (bDegeneration3D)
	{
		SetAsDegenerated();
	}

	return bDegeneration2D;
}

TSharedPtr<FTopologicalEdge> FTopologicalEdge::Make(const TSharedRef<FRestrictionCurve>& InCurve, const TSharedRef<FTopologicalVertex>& InVertex1, const TSharedRef<FTopologicalVertex>& InVertex2, const FLinearBoundary& InBoundary)
{
	TSharedRef<FTopologicalEdge> Edge = FEntity::MakeShared<FTopologicalEdge>(InCurve, InVertex1, InVertex2, InBoundary);
	if (Edge->CheckIfDegenerated())
	{
		return TSharedPtr<FTopologicalEdge>();
	}

	if (!Edge->CheckVertices())
	{
		return TSharedPtr<FTopologicalEdge>();
	}

	Edge->LinkVertex();
	return Edge;
}

TSharedPtr<FTopologicalEdge> FTopologicalEdge::Make(const TSharedRef<FRestrictionCurve>& InCurve, const TSharedRef<FTopologicalVertex>& InVertex1, const TSharedRef<FTopologicalVertex>& InVertex2)
{
	TSharedRef<FTopologicalEdge> Edge = FEntity::MakeShared<FTopologicalEdge>(InCurve, InVertex1, InVertex2);
	if (Edge->CheckIfDegenerated())
	{
		return TSharedPtr<FTopologicalEdge>();
	}

	if (!Edge->CheckVertices())
	{
		return TSharedPtr<FTopologicalEdge>();
	}

	Edge->LinkVertex();
	return Edge;
}

TSharedPtr<FTopologicalEdge> FTopologicalEdge::Make(const TSharedRef<FRestrictionCurve>& InCurve, const FLinearBoundary& InBoundary)
{
	TSharedRef<FTopologicalEdge> Edge = FEntity::MakeShared<FTopologicalEdge>(InCurve, InBoundary);
	if (Edge->CheckIfDegenerated())
	{
		return TSharedPtr<FTopologicalEdge>();
	}

	Edge->LinkVertex();
	return Edge;
}

TSharedPtr<FTopologicalEdge> FTopologicalEdge::Make(const TSharedRef<FRestrictionCurve>& InCurve)
{
	TSharedRef<FTopologicalEdge> Edge = FEntity::MakeShared<FTopologicalEdge>(InCurve);
	if (Edge->CheckIfDegenerated())
	{
		return TSharedPtr<FTopologicalEdge>();
	}

	Edge->LinkVertex();
	return Edge;
}

TSharedPtr<FTopologicalEdge> FTopologicalEdge::Make(const TSharedRef<FSurface>& InSurface, const FPoint2D& InCoordinateVertex1, const TSharedRef<FTopologicalVertex>& InVertex1, const FPoint2D& InCoordinateVertex2, const TSharedRef<FTopologicalVertex>& InVertex2)
{
	TSharedRef<FTopologicalEdge> Edge = FEntity::MakeShared<FTopologicalEdge>(InSurface, InCoordinateVertex1, InVertex1, InCoordinateVertex2, InVertex2);
	if (Edge->CheckIfDegenerated())
	{
		return TSharedPtr<FTopologicalEdge>();
	}

	Edge->LinkVertex();
	return Edge;
}

void FTopologicalEdge::Link(const TSharedRef<FTopologicalEdge>& Twin, double SquareJoiningTolerance)
{
	// Degenerated twin edges are not linked
	if (IsDegenerated() || Twin->IsDegenerated())
	{
		SetAsDegenerated();
		Twin->SetAsDegenerated();
		return;
	}

	const FPoint& Edge1Vertex1 = GetStartVertex()->GetBarycenter();
	const FPoint& Edge1Vertex2 = GetEndVertex()->GetBarycenter();
	const FPoint& Edge2Vertex1 = Twin->GetStartVertex()->GetBarycenter();
	const FPoint& Edge2Vertex2 = Twin->GetEndVertex()->GetBarycenter();

	// Define the orientation
	const double SquareDistanceE1V1_E2V1 = GetStartVertex()->IsLinkedTo(Twin->GetStartVertex()) ? 0. : Edge1Vertex1.SquareDistance(Edge2Vertex1);
	const double SquareDistanceE1V2_E2V2 = GetEndVertex()->IsLinkedTo(Twin->GetEndVertex()) ? 0. : Edge1Vertex2.SquareDistance(Edge2Vertex2);
							 
	const double SquareDistanceE1V1_E2V2 = GetStartVertex()->IsLinkedTo(Twin->GetEndVertex()) ? 0. : Edge1Vertex1.SquareDistance(Edge2Vertex2);
	const double SquareDistanceE1V2_E2V1 = GetEndVertex()->IsLinkedTo(Twin->GetStartVertex()) ? 0. : Edge1Vertex2.SquareDistance(Edge2Vertex1);

	bool bCanMergeEdge = true;
	const double SquareDistanceSameOrientation = SquareDistanceE1V1_E2V1 + SquareDistanceE1V2_E2V2;
	const double SquareDistanceReverseOrientation = SquareDistanceE1V1_E2V2 + SquareDistanceE1V2_E2V1;
	if (SquareDistanceSameOrientation < SquareDistanceReverseOrientation)
	{
		if (SquareDistanceE1V1_E2V1 < SquareJoiningTolerance)
		{
			GetStartVertex()->Link(Twin->GetStartVertex());
		}
		else
		{
			FMessage::Printf(Log, TEXT("Edge %d and Edge %d are to far (%f) to be connected\n"), GetId(), Twin->GetId(), sqrt(SquareDistanceE1V1_E2V1));
			bCanMergeEdge = false;
		}

		if (SquareDistanceE1V2_E2V2 < SquareJoiningTolerance)
		{
			GetEndVertex()->Link(Twin->GetEndVertex());
		}
		else
		{
			FMessage::Printf(Log, TEXT("Edge %d and Edge %d are to far (%f) to be connected\n"), GetId(), Twin->GetId(), sqrt(SquareDistanceE1V2_E2V2));
			bCanMergeEdge = false;
		}
	}
	else
	{
		if (SquareDistanceE1V1_E2V2 < SquareJoiningTolerance)
		{
			GetStartVertex()->Link(Twin->GetEndVertex());
		}
		else
		{
			FMessage::Printf(Log, TEXT("Edge %d and Edge %d are to far (%f) to be connected\n"), GetId(), Twin->GetId(), sqrt(SquareDistanceE1V1_E2V2));
			bCanMergeEdge = false;
		}

		if (SquareDistanceE1V2_E2V1 < SquareJoiningTolerance)
		{
			GetEndVertex()->Link(Twin->GetStartVertex());
		}
		else
		{
			FMessage::Printf(Log, TEXT("Edge %d and Edge %d are to far (%f) to be connected\n"), GetId(), Twin->GetId(), sqrt(SquareDistanceE1V2_E2V1));
			bCanMergeEdge = false;
		}
	}

	if (bCanMergeEdge)
	{
		MakeLink(Twin);
	}
}

void FTopologicalEdge::Delete()
{
	StartVertex->RemoveConnectedEdge(StaticCastSharedRef<FTopologicalEdge>(AsShared()));
	EndVertex->RemoveConnectedEdge(StaticCastSharedRef<FTopologicalEdge>(AsShared()));

	StartVertex.Reset();
	EndVertex.Reset();

    Curve.Reset();
	Loop.Reset();
	Mesh.Reset();
	SetDeleted();
}

TSharedRef<FTopologicalFace> FTopologicalEdge::GetFace() const
{
	ensureCADKernel(Loop.IsValid());
	return Loop.Pin()->GetFace();
}

void FTopologicalEdge::ComputeCrossingPointCoordinates()
{
	TSharedRef<FTopologicalEdge> ActiveEdge = GetLinkActiveEdge();
	ensureCADKernel(this == &ActiveEdge.Get());

	double Tolerance = GetTolerance3D();

	FSurfacicPolyline Presampling;
	FSurfacicCurveSamplerOnParam Sampler(*Curve.ToSharedRef(), Boundary, Tolerance * 10., Tolerance, Presampling);
	Sampler.Sample();

	TArray<double>& TabCrossingPointU = ActiveEdge->GetCrossingPointUs();
	Presampling.SwapCoordinates(TabCrossingPointU);
}

void FTopologicalEdge::SetStartVertex(const double NewCoordinate)
{
	ensureCADKernel(Curve->GetUMax() > NewCoordinate);
	Boundary.SetMin(NewCoordinate);
	FCurvePoint OutPoint;
	Curve->EvaluatePoint(NewCoordinate, OutPoint);
	StartVertex->SetCoordinates(OutPoint.Point);
}

void FTopologicalEdge::SetEndVertex(const double NewCoordinate)
{
	ensureCADKernel(Curve->GetUMin() < NewCoordinate);
	Boundary.SetMax(NewCoordinate);
	FCurvePoint OutPoint;
	Curve->EvaluatePoint(NewCoordinate, OutPoint);
	EndVertex->SetCoordinates(OutPoint.Point);
}

void FTopologicalEdge::SetStartVertex(const double NewCoordinate, const FPoint& NewPoint3D)
{
	ensureCADKernel(Curve->GetUMin() < NewCoordinate);
	Boundary.SetMin(NewCoordinate);
	StartVertex->SetCoordinates(NewPoint3D);
}

void FTopologicalEdge::SetEndVertex(const double NewCoordinate, const FPoint& NewPoint3D)
{
	ensureCADKernel(Curve->GetUMax() > NewCoordinate);
	Boundary.SetMax(NewCoordinate);
	EndVertex->SetCoordinates(NewPoint3D);
}

double FTopologicalEdge::Length() const
{
	if (Length3D < 0)
	{
		Length3D = Curve->ApproximateLength(Boundary);
	}
	return Length3D;
}

void FTopologicalEdge::GetTangentsAtExtremities(FPoint& StartTangent, FPoint& EndTangent, bool bForward) const
{
	ensureCADKernel(Curve->Polyline.Size());

	FDichotomyFinder Finder(Curve->Polyline.GetCoordinates());
	int32 StartIndex = Finder.Find(Boundary.Min);
	int32 EndIndex = Finder.Find(Boundary.Max);

	const TArray<FPoint>& Polyline3D = Curve->Polyline.GetPoints();
	if(bForward)
	{
		StartTangent = Polyline3D[StartIndex + 1] - Polyline3D[StartIndex];
		EndTangent = Polyline3D[EndIndex] - Polyline3D[EndIndex + 1];
	}
	else
	{
		EndTangent = Polyline3D[StartIndex + 1] - Polyline3D[StartIndex];
		StartTangent = Polyline3D[EndIndex] - Polyline3D[EndIndex + 1];
	}
}


void FTopologicalEdge::Sample(const double DesiredSegmentLength, TArray<double>& OutCoordinates) const
{
	Curve->Sample(Boundary, DesiredSegmentLength, OutCoordinates);
}

int32 FTopologicalEdge::EvaluateCuttingPointNum()
{
	double Num = 0;
	for (int32 Index = 0; Index < CrossingPointUs.Num() - 1; Index++)
	{
		Num += ((CrossingPointUs[Index + 1] - CrossingPointUs[Index]) / CrossingPointDeltaUMaxs[Index]);
	}
	Num *= 1.5;
	return (int32) Num;
}

double FTopologicalEdge::TransformLocalCoordinateToActiveEdgeCoordinate(const double InLocalCoordinate)
{
	if (IsActiveEntity())
	{
		return InLocalCoordinate;
	}

	TSharedPtr<FTopologicalEdge> ActiveEdge = GetLinkActiveEntity();
	FPoint PointOnEdge = Curve->Approximate3DPoint(InLocalCoordinate);
	FPoint ProjectedPoint;
	return ActiveEdge->GetCurve()->GetCoordinateOfProjectedPoint(Boundary, PointOnEdge, ProjectedPoint);
}

double FTopologicalEdge::TransformActiveEdgeCoordinateToLocalCoordinate(const double InActiveEdgeCoordinate)
{
	if (IsActiveEntity())
	{
		return InActiveEdgeCoordinate;
	}

 	TSharedPtr<FTopologicalEdge> ActiveEdge = GetLinkActiveEntity();
	FPoint PointOnEdge = ActiveEdge->GetCurve()->Approximate3DPoint(InActiveEdgeCoordinate);
	FPoint ProjectedPoint;
	return Curve->GetCoordinateOfProjectedPoint(Boundary, PointOnEdge, ProjectedPoint);
}

void FTopologicalEdge::TransformLocalCoordinatesToActiveEdgeCoordinates(const TArray<double>& InLocalCoordinate, TArray<double>& OutActiveEdgeCoordinates)
{
	if (IsActiveEntity())
	{
		OutActiveEdgeCoordinates = InLocalCoordinate;
	}
	TSharedPtr<FTopologicalEdge> ActiveEdge = GetLinkActiveEntity();
	TArray<FPoint> EdgePoints;
	Curve->Approximate3DPoints(InLocalCoordinate, EdgePoints);
	TArray<FPoint> ProjectedPoints;
	ActiveEdge->GetCurve()->ProjectPoints(Boundary, EdgePoints, OutActiveEdgeCoordinates, ProjectedPoints);
}

void FTopologicalEdge::TransformActiveEdgeCoordinatesToLocalCoordinates(const TArray<double>& InActiveEdgeCoordinate, TArray<double>& OutLocalCoordinates)
{
	if (IsActiveEntity())
	{
		OutLocalCoordinates = InActiveEdgeCoordinate;
	}
 
	TSharedPtr<FTopologicalEdge> ActiveEdge = GetLinkActiveEntity();
	TArray<FPoint> ActiveEdgePoint;
	ActiveEdge->GetCurve()->Approximate3DPoints(InActiveEdgeCoordinate, ActiveEdgePoint);
	TArray<FPoint> ProjectedPoints;
	Curve->ProjectPoints(Boundary, ActiveEdgePoint, OutLocalCoordinates, ProjectedPoints);
}

void FTopologicalEdge::AddImposedCuttingPointU(const double ImposedCuttingPointU, int32 OppositeNodeIndex)
{
	if (!IsActiveEntity())
	{
		ensureCADKernel(false);
		FPoint Point = Curve->Approximate3DPoint(ImposedCuttingPointU);
		FPoint ProjectedPoint;
		double ActiveEdgeParamU = GetLinkActiveEntity()->ProjectPoint(Point, ProjectedPoint);
		return GetLinkActiveEntity()->AddImposedCuttingPointU(ActiveEdgeParamU, OppositeNodeIndex);
	}

	ImposedCuttingPointUs.Emplace(ImposedCuttingPointU, OppositeNodeIndex);
}

void FTopologicalEdge::ProjectTwinEdgePointsOn2DCurve(const TSharedRef<FTopologicalEdge>& InTwinEdge, const TArray<double>& InTwinEdgePointCoords, TArray<FPoint2D>& OutPoints2D)
{
	if (&InTwinEdge.Get() == this)
	{
		Curve->Approximate2DPoints(InTwinEdgePointCoords, OutPoints2D);
	}
	else
	{
		TArray<FPoint> Points3D;
		InTwinEdge->ApproximatePoints(InTwinEdgePointCoords, Points3D);

		bool bSameDirection = IsSameDirection(InTwinEdge);
		TArray<double> Coordinates;
		Curve->ProjectTwinCurvePoints(Points3D, bSameDirection, Coordinates);
		Curve->Approximate2DPoints(Coordinates, OutPoints2D);
	}
}

bool FTopologicalEdge::IsSameDirection(const TSharedPtr<FTopologicalEdge>& Edge) const
{
	if (TopologicalLink != Edge->GetLink())
	{
		return true;
	}

	if (Edge == AsShared())
	{
		return true;
	}

	TSharedPtr<const FVertexLink> vertex1Edge = GetStartVertex()->GetLink();
	TSharedPtr<const FVertexLink> vertex2Edge = GetEndVertex()->GetLink();

	if (vertex1Edge == vertex2Edge)
	{
		if (Edge->IsDegenerated())
		{
			return true;
		}

		// TODO
		ensureCADKernel(false);
	}

	return vertex1Edge == Edge->GetStartVertex()->GetLink();
}

TSharedPtr<FEntityGeom> FTopologicalEdge::ApplyMatrix(const FMatrixH& InMatrix) const
{
	TSharedPtr<FTopologicalVertex> v1Transformed = StaticCastSharedPtr<FTopologicalVertex>(StartVertex->ApplyMatrix(InMatrix));
	if (!v1Transformed.IsValid()) 
	{
		return TSharedPtr<FEntityGeom>();
	}

	TSharedPtr<FTopologicalVertex> v2Transformed = StaticCastSharedPtr<FTopologicalVertex>(EndVertex->ApplyMatrix(InMatrix));
	if (!v2Transformed.IsValid())
	{
		return TSharedPtr<FEntityGeom>();
	}

	TSharedPtr<FRestrictionCurve> TransformedCurve = StaticCastSharedPtr<FRestrictionCurve>(Curve->ApplyMatrix(InMatrix));
	if (!TransformedCurve.IsValid())
	{
		return TSharedPtr<FEntityGeom>();
	}

	return FTopologicalEdge::Make(TransformedCurve.ToSharedRef(), v1Transformed.ToSharedRef(), v2Transformed.ToSharedRef(), Boundary);
}

#ifdef CADKERNEL_DEV
FInfoEntity& FTopologicalEdge::GetInfo(FInfoEntity& Info) const
{
	return FTopologicalEntity::GetInfo(Info)
		.Add(TEXT("Link"), TopologicalLink)
		.Add(TEXT("Curve"), Curve)
		.Add(TEXT("Vertex1"), StartVertex)
		.Add(TEXT("Vertex2"), EndVertex)
		.Add(TEXT("Boundary"), Boundary)
		.Add(TEXT("Loop"), Loop)
		.Add(TEXT("Length"), Length())
		.Add(TEXT("Mesh"), Mesh);
}
#endif

TSharedRef<FEdgeMesh> FTopologicalEdge::GetOrCreateMesh(const TSharedRef<FModelMesh>& ShellMesh)
{
	if (!IsActiveEntity())
	{
		return GetLinkActiveEdge()->GetOrCreateMesh(ShellMesh);
	}

	if (!Mesh)
	{
		Mesh = FEntity::MakeShared<FEdgeMesh>(ShellMesh, StaticCastSharedRef<FTopologicalEdge>(AsShared()));
	}
	return Mesh.ToSharedRef();
}

void FTopologicalEdge::ChooseFinalDeltaUs()
{
	for (int32 Index = 0; Index < CrossingPointDeltaUMins.Num(); ++Index)
	{
		if (CrossingPointDeltaUMins[Index] > CrossingPointDeltaUMaxs[Index])
		{
			CrossingPointDeltaUMaxs[Index] = CrossingPointDeltaUMins[Index];
		}
	}
}

TSharedPtr<FTopologicalEdge> FTopologicalEdge::CreateEdgeByMergingEdges(TArray<FOrientedEdge>& Edges, const TSharedRef<FTopologicalVertex> StartVertex, const TSharedRef<FTopologicalVertex> EndVertex)
{
	// Make merged 2d Nurbs ===================================================

	const double Tolerance3D = Edges[0].Entity->GetTolerance3D();
	//const double Tolerance2D = Edges[0].Entity->GetCurve()->Get2DCurve()->GetDomainTolerance();
	TSharedRef<FSurface> CarrierSurface = Edges[0].Entity->GetCurve()->GetCarrierSurface();

	// check if all curve a 2D NURBS
	bool bAreNurbs = true;
	int32 NurbsMaxDegree = 0;

	// TODO, Check if some edges are small edges and could be replace by extending next or previous one
	double NewEdgeLength = 0;
	for (const FOrientedEdge& Edge : Edges)
	{
		NewEdgeLength += Edge.Entity->Length();
	}

	// MinLength = smaller than this size, the adgacent edge is extend to replace it  
	const double MinLength = FMath::Min(NewEdgeLength / (Edges.Num()+3.), FMath::Max(NewEdgeLength / 20., Tolerance3D * 5.)); // Tolerance3D = 0.01mm => MinLength = 0.25mm, 1/20 of the length is small enougth to be a detail.

	TArray<TSharedPtr<FNURBSCurve>> NurbsCurves;
	NurbsCurves.Reserve(Edges.Num());

	bool bCanRemove = true;
	for (FOrientedEdge& Edge : Edges)
	{
		if (Edge.Entity->GetCurve()->Get2DCurve()->GetCurveType() != ECurve::Nurbs)
		{
			return TSharedPtr<FTopologicalEdge>();
		}

		double EdgeLength = Edge.Entity->Length();
		if (bCanRemove && EdgeLength < MinLength)
		{
			NurbsCurves.Emplace(TSharedPtr<FNURBSCurve>());
			bCanRemove = false;
			continue; // the edge will be ignored 
		}
		bCanRemove = true;


		// Find the max degree of the nurbs
		TSharedPtr<FNURBSCurve> NURBS = NurbsCurves.Emplace_GetRef(StaticCastSharedRef<FNURBSCurve>(Edge.Entity->GetCurve()->Get2DCurve()));
		int32 NurbsDegree = NURBS->GetDegree();
		if (NurbsDegree > NurbsMaxDegree)
		{
			NurbsMaxDegree = NurbsDegree;
		}

		// Edge has restricted its curve ?
		FLinearBoundary EdgeBoundary = Edge.Entity->GetBoundary();
		FLinearBoundary CurveBoundary = NURBS->GetBoundary();

		double ParametricTolerance = NURBS->GetBoundary().ComputeMinimalTolerance();

		if (!FMath::IsNearlyEqual(EdgeBoundary.Min, CurveBoundary.Min, ParametricTolerance) ||
			!FMath::IsNearlyEqual(EdgeBoundary.Max, CurveBoundary.Max, ParametricTolerance))
		{
			// ToDO, check if the next edge is not the complementary of this

			// cancel
			return TSharedPtr<FTopologicalEdge>();
		}
	}

	bool bEdgeNeedToBeExtend = false;
	int32 PoleCount = 0;
	double LastCoordinate = 0;

	for (int32 Index = 0; Index < Edges.Num(); Index++)
	{
		TSharedPtr<FNURBSCurve>& NURBS = NurbsCurves[Index];
		if (!NURBS.IsValid())
		{
			bEdgeNeedToBeExtend = true;
			continue; // the edge will be ignored 
		}

		if (NURBS->GetDegree() < NurbsMaxDegree)
		{
			NURBS = BSpline::DuplicateNurbsCurveWithHigherDegree(NurbsMaxDegree, *NURBS);
		}
		else
		{
			NURBS = FEntity::MakeShared<FNURBSCurve>(NURBS.ToSharedRef());
		}

		if (Edges[Index].Direction == EOrientation::Back)
		{
			NURBS->Invert();
		}

		NURBS->SetStartNodalCoordinate(LastCoordinate);
		LastCoordinate = NURBS->GetBoundary().GetMax();

		PoleCount += NURBS->GetPoleCount();
	}

	if (bEdgeNeedToBeExtend)
	{
		for (int32 Index = 0; Index < Edges.Num(); Index++)
		{
			if (!NurbsCurves[Index].IsValid())
			{
				double PreviousLength = Index > 0 ? Edges[Index - 1].Entity->Length() : 0;
				double NextLength = Index < Edges.Num() - 1 ? Edges[Index + 1].Entity->Length() : 0;

				double TargetCoordinate = 0;
				EOrientation FrontOrientation = PreviousLength > NextLength ? EOrientation::Front : EOrientation::Back;
				TargetCoordinate = Edges[Index].Direction == FrontOrientation ? Edges[Index].Entity->GetBoundary().GetMax() : Edges[Index].Entity->GetBoundary().GetMin();

				if (PreviousLength > NextLength)
				{
					TargetCoordinate = Edges[Index].Direction == EOrientation::Front ? Edges[Index].Entity->GetBoundary().GetMax() : Edges[Index].Entity->GetBoundary().GetMin();
				}
				else
				{
					TargetCoordinate = Edges[Index].Direction == EOrientation::Front ? Edges[Index].Entity->GetBoundary().GetMin() : Edges[Index].Entity->GetBoundary().GetMax();
				}
				FPoint2D Target = Edges[Index].Entity->Approximate2DPoint(TargetCoordinate);

				int32 NeigborIndex = PreviousLength > NextLength ? Index - 1 : Index + 1;
				NurbsCurves[NeigborIndex]->ExtendTo(Target);
			}
		}
	}

	TArray<double> NewNodalVector;
	TArray<double> NewWeights;
	TArray<FPoint> NewPoles;
	NurbsMaxDegree++;
	NewNodalVector.Reserve(PoleCount + NurbsMaxDegree);
	NewWeights.Reserve(PoleCount + NurbsMaxDegree);
	NewPoles.Reserve(PoleCount + NurbsMaxDegree);

	for (const TSharedPtr<FNURBSCurve>& NurbsCurve : NurbsCurves)
	{
		if (!NurbsCurve.IsValid())
		{
			continue;
		}

		if (!NewPoles.IsEmpty())
		{
			NewPoles.Pop();
			NewWeights.Pop();
		}

		NewPoles.Append(NurbsCurve->GetPoles());
		NewWeights.Append(NurbsCurve->GetWeights());
	}

	for (const TSharedPtr<FNURBSCurve>& NurbsCurve : NurbsCurves)
	{
		if (!NurbsCurve.IsValid())
		{
			continue;
		}

		if (NewNodalVector.IsEmpty())
		{
			NewNodalVector.Append(NurbsCurve->GetNodalVector());
		}
		else
		{
			NewNodalVector.SetNum(NewNodalVector.Num() - 1);
			NewNodalVector.Append(NurbsCurve->GetNodalVector().GetData() + NurbsMaxDegree, NurbsCurve->GetNodalVector().Num() - NurbsMaxDegree);
		}
	}

	TSharedRef<FNURBSCurve> MergedNURBS = FEntity::MakeShared<FNURBSCurve>(NurbsMaxDegree - 1, NewNodalVector, NewPoles, NewWeights, 2);

	// Make new edge and delete the old ones ===================================================

	TSharedRef<FRestrictionCurve> RestrictionCurve = FEntity::MakeShared<FRestrictionCurve>(CarrierSurface, MergedNURBS);

	TSharedPtr<FTopologicalEdge> NewEdge = Make(RestrictionCurve, StartVertex, EndVertex);
	if (!NewEdge.IsValid())
	{
		printf("SSS");
	}


	TSharedPtr<FTopologicalLoop> Loop = Edges[0].Entity->GetLoop();
	Loop->ReplaceEdges(Edges, NewEdge);

	for (const FOrientedEdge& OrientedEdge : Edges)
	{
		OrientedEdge.Entity->Delete();
	}

	return NewEdge;
}

bool FTopologicalEdge::ExtendTo(bool bStartExtremity, const FPoint2D& NewExtremityCoordinate, TSharedRef<FTopologicalVertex> NewVertex)
{
	TFunction<void(TSharedPtr<FTopologicalVertex>&)> UpdateVertex = [&](TSharedPtr<FTopologicalVertex>& EdgeVertex)
	{
		TSharedRef<FTopologicalEdge> Edge = StaticCastSharedRef<FTopologicalEdge>(AsShared());
		EdgeVertex->RemoveConnectedEdge(Edge);
		if (EdgeVertex->GetDirectConnectedEdges().Num() == 0)
		{
			EdgeVertex->RemoveFromLink();
		}
		EdgeVertex = NewVertex;
		NewVertex->AddConnectedEdge(Edge);
		Length3D = -1.;
	};

	if (bStartExtremity ? FMath::IsNearlyEqual(Boundary.Min, Curve->GetBoundary().Min) : FMath::IsNearlyEqual(Boundary.Max, Curve->GetBoundary().Max))
	{
		Curve->ExtendTo(NewExtremityCoordinate);
	}
	else
	{
		FPoint ProjectedPoint;
		double UProjectedPoint = ProjectPoint(NewVertex->GetCoordinates(), ProjectedPoint);
		if (ProjectedPoint.Distance(NewVertex->GetCoordinates()) > GetTolerance3D())
		{
			return false;
		}

		if (bStartExtremity)
		{
			Boundary.Min = UProjectedPoint;
		}
		else
		{
			Boundary.Max = UProjectedPoint;
		}
	}

	if (bStartExtremity)
	{
		UpdateVertex(StartVertex);
	}
	else
	{
		UpdateVertex(EndVertex);
	}

	return true;
}

void FTopologicalEdge::ComputeEdge2DProperties(FEdge2DProperties& EdgeCharacteristics)
{
	const TArray<FPoint2D>& Polyline2D = Curve->Polyline.Get2DPoints();
	const TArray<FPoint>& Polyline3D = Curve->Polyline.GetPoints();
	const TArray<double>& Parameters = Curve->Polyline.GetCoordinates();

	FDichotomyFinder Finder(Curve->Polyline.GetCoordinates());
	int32 StartIndex = Finder.Find(Boundary.Min);
	int32 EndIndex = Finder.Find(Boundary.Max);

	for (int32 Index = StartIndex; Index <= EndIndex; ++Index)
	{
		double Slop = ComputeUnorientedSlope(Polyline2D[Index], Polyline2D[Index + 1], 0);
		if (Slop > 2.)
		{
			Slop = 4. - Slop;
		}
		EdgeCharacteristics.Add(Slop, Polyline3D[Index].Distance(Polyline3D[Index + 1]));
	}
}

FPoint FTopologicalEdge::GetTangentAt(const TSharedRef<FTopologicalVertex>& InVertex)
{
	if (InVertex->GetLink() == StartVertex->GetLink())
	{
		return Curve->GetTangentAt(Boundary.GetMin());
	}
	else if (InVertex->GetLink() == EndVertex->GetLink())
	{
		FPoint Tangent = Curve->GetTangentAt(Boundary.GetMax());
		Tangent *= -1.;
		return Tangent;
	}
	else
	{
		ensureCADKernel(false);
		return FPoint::ZeroPoint;
	}
}

FPoint2D FTopologicalEdge::GetTangent2DAt(const TSharedRef<FTopologicalVertex>& InVertex)
{
	if (InVertex->GetLink() == StartVertex->GetLink())
	{
		return Curve->GetTangent2DAt(Boundary.GetMin());
	}
	else if (InVertex->GetLink() == EndVertex->GetLink())
	{
		FPoint2D Tangent = Curve->GetTangent2DAt(Boundary.GetMax());
		Tangent *= -1.;
		return Tangent;
	}
	else
	{
		ensureCADKernel(false);
		return FPoint2D::ZeroPoint;
	}
}


void FTopologicalEdge::SpawnIdent(FDatabase& Database)
{
	if (!FEntity::SetId(Database))
	{
		return;
	}

	StartVertex->SpawnIdent(Database);
	EndVertex->SpawnIdent(Database);
	Curve->SpawnIdent(Database);

	if(TopologicalLink.IsValid())
	{
		TopologicalLink->SpawnIdent(Database);
	}
	if (Mesh.IsValid())
	{
		Mesh->SpawnIdent(Database);
	}
}

TSharedPtr<FTopologicalVertex> FTopologicalEdge::SplitAt(double SplittingCoordinate, const FPoint& NewVertexCoordinate, bool bKeepStartVertexConnectivity, TSharedPtr<FTopologicalEdge>& NewEdge)
{
	if (GetTwinsEntityCount() > 1)
	{
		return TSharedPtr<FTopologicalVertex>();
		ensureCADKernel(false);
		// TODO
	}

	TSharedRef<FTopologicalVertex> MiddelVertex = FEntity::MakeShared<FTopologicalVertex>(NewVertexCoordinate);

	if(bKeepStartVertexConnectivity)
	{
		FLinearBoundary NewEdgeBoundary(SplittingCoordinate, Boundary.Max);
		NewEdge = Make(Curve.ToSharedRef(), MiddelVertex, EndVertex.ToSharedRef(), NewEdgeBoundary);
	}
	else
	{
		FLinearBoundary NewEdgeBoundary(Boundary.Min, SplittingCoordinate);
		NewEdge = Make(Curve.ToSharedRef(), StartVertex.ToSharedRef(), MiddelVertex, NewEdgeBoundary);
	}
	if (!NewEdge.IsValid())
	{
		return TSharedPtr<FTopologicalVertex>();
	}

	TSharedRef<FTopologicalEdge> ThisEdge = StaticCastSharedRef<FTopologicalEdge>(AsShared());

	if(bKeepStartVertexConnectivity)
	{
		EndVertex->RemoveConnectedEdge(ThisEdge);
		EndVertex = MiddelVertex;
		Boundary.Max = SplittingCoordinate;
	}
	else
	{
		StartVertex->RemoveConnectedEdge(ThisEdge);
		StartVertex = MiddelVertex;
		Boundary.Min = SplittingCoordinate;
	}
	MiddelVertex->AddConnectedEdge(ThisEdge);
	Length3D = -1.;

	Loop.Pin()->SplitEdge(ThisEdge, NewEdge, bKeepStartVertexConnectivity);
	return MiddelVertex;
}



#ifdef CADKERNEL_DEV
FInfoEntity& FEdgeLink::GetInfo(FInfoEntity& Info) const
{
	return FEntity::GetInfo(Info)
		.Add(TEXT("active Entity"), ActiveEntity)
		.Add(TEXT("twin Entities"), TwinsEntities);
}
#endif


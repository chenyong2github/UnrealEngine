// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#include "Selection/GroupTopologySelector.h"
#include "MeshQueries.h"
#include "Drawing/ToolDataVisualizer.h"
#include "ToolSceneQueriesUtil.h"



void FGroupTopologySelection::Clear()
{
	SelectedGroupIDs.Reset();
	SelectedCornerIDs.Reset();
	SelectedEdgeIDs.Reset();
}



FGroupTopologySelector::FGroupTopologySelector()
{
	// initialize to sane values
	PointsWithinToleranceTest =
		[](const FVector3d& A, const FVector3d& B) { return A.Distance(B) < 1.0; };
	GetSpatial =
		[]() { return nullptr; };
}



void FGroupTopologySelector::Initialize(const FDynamicMesh3* MeshIn, const FGroupTopology* TopologyIn)
{
	Mesh = MeshIn;
	Topology = TopologyIn;
	bGeometryInitialized = false;
	bGeometryUpToDate = false;
}


void FGroupTopologySelector::Invalidate(bool bTopologyDeformed, bool bTopologyModified)
{
	if (bTopologyDeformed)
	{
		bGeometryUpToDate = false;
	}
	if (bTopologyModified)
	{
		bGeometryUpToDate = bGeometryInitialized = false;
	}
}


const FGeometrySet3& FGroupTopologySelector::GetGeometrySet()
{
	if (bGeometryInitialized == false)
	{
		GeometrySet.Reset();
		int NumCorners = Topology->Corners.Num();
		for (int CornerID = 0; CornerID < NumCorners; ++CornerID)
		{
			FVector3d Position = Mesh->GetVertex(Topology->Corners[CornerID].VertexID);
			GeometrySet.AddPoint(CornerID, Position);
		}
		int NumEdges = Topology->Edges.Num();
		for (int EdgeID = 0; EdgeID < NumEdges; ++EdgeID)
		{
			FPolyline3d Polyline;
			Topology->Edges[EdgeID].Span.GetPolyline(Polyline);
			GeometrySet.AddCurve(EdgeID, Polyline);
		}

		bGeometryInitialized = true;
		bGeometryUpToDate = true;
	}

	if (bGeometryUpToDate == false)
	{
		int NumCorners = Topology->Corners.Num();
		for (int CornerID = 0; CornerID < NumCorners; ++CornerID)
		{
			FVector3d Position = Mesh->GetVertex(Topology->Corners[CornerID].VertexID);
			GeometrySet.UpdatePoint(CornerID, Position);
		}
		int NumEdges = Topology->Edges.Num();
		for (int EdgeID = 0; EdgeID < NumEdges; ++EdgeID)
		{
			FPolyline3d Polyline;
			Topology->Edges[EdgeID].Span.GetPolyline(Polyline);
			GeometrySet.UpdateCurve(EdgeID, Polyline);
		}
		bGeometryUpToDate = true;
	}

	return GeometrySet;
}


void FGroupTopologySelector::UpdateEnableFlags(bool bFaceHits, bool bEdgeHits, bool bCornerHits)
{
	bEnableFaceHits = bFaceHits;
	bEnableEdgeHits = bEdgeHits;
	bEnableCornerHits = bCornerHits;
}


bool FGroupTopologySelector::FindSelectedElement(const FRay3d& Ray, FGroupTopologySelection& ResultOut,
	FVector3d& SelectedPositionOut, FVector3d& SelectedNormalOut)
{
	FDynamicMeshAABBTree3* Spatial = GetSpatial();
	const FGeometrySet3& TopoSpatial = GetGeometrySet();
	
	double TriangleHitRayParam = TNumericLimits<double>::Max();
	FVector3d TriangleHitPos;

	int HitTriangleID = (Spatial != nullptr) ? Spatial->FindNearestHitTriangle(Ray) : IndexConstants::InvalidID;
	if (HitTriangleID != IndexConstants::InvalidID)
	{
		FTriangle3d Triangle;
		Mesh->GetTriVertices(HitTriangleID, Triangle.V[0], Triangle.V[1], Triangle.V[2]);
		FIntrRay3Triangle3d Query(Ray, Triangle);
		Query.Find();

		TriangleHitRayParam = Query.RayParameter;
		TriangleHitPos = Ray.PointAt(Query.RayParameter);
	}
	bool bHitSurface = bEnableFaceHits && (HitTriangleID >= 0);
	bool bActuallyHitSurface = (HitTriangleID >= 0);


	int HitCornerID = -1;
	double HitCornerRayParam = TNumericLimits<double>::Max();
	FVector3d HitCornerPt, OnCornerPt;
	if (bEnableCornerHits)
	{
		FGeometrySet3::FNearest Nearest;
		bool bFound = TopoSpatial.FindNearestPointToRay(Ray, Nearest, PointsWithinToleranceTest);
		if (bFound)
		{
			HitCornerID = Nearest.ID;
			HitCornerRayParam = Nearest.RayParam;
			HitCornerPt = Nearest.NearestRayPoint;
			OnCornerPt = Nearest.NearestGeoPoint;
		}
	}
	bool bHitCorner = (HitCornerID >= 0);

	// @todo currently implicitly assuming that corner is better than edge, 
	// but we should be checking the ray-param...edge could be much closer!

	int HitEdgeID = -1;
	double HitEdgeRayParam = TNumericLimits<double>::Max();
	FVector3d HitEdgePt, OnEdgePt;
	if (bEnableEdgeHits && bHitCorner == false)		// EDGE HIT DISABLED IF CORNER HIT
	{
		FGeometrySet3::FNearest Nearest;
		bool bFound = TopoSpatial.FindNearestCurveToRay(Ray, Nearest, PointsWithinToleranceTest);
		if (bFound)
		{
			HitEdgeID = Nearest.ID;
			HitEdgeRayParam = Nearest.RayParam;
			HitEdgePt = Nearest.NearestRayPoint;
			OnEdgePt = Nearest.NearestGeoPoint;
		}
	}
	bool bHitEdge = (HitEdgeID >= 0);


	if (bHitCorner == false && bHitEdge == false && bHitSurface == false)
	{
		return false;
	}


	// can only have one hit
	if ((bHitCorner || bHitEdge) && bHitSurface)
	{
		// to determine if the edge/corner we "hit" is visible, we cast a ray back
		// towards the eye from that hit point. If it hits anything we can't see it
		// and so should take the face selection instead.

		FVector3d ElementHitPt = (bHitCorner) ? OnCornerPt : OnEdgePt;
		FRay3d ToEyeRay(ElementHitPt, (Ray.Origin - ElementHitPt).Normalized(), true);
		ToEyeRay.Origin += (double)(100*FMathf::ZeroTolerance) * ToEyeRay.Direction;
		int ToEyeHitTID = Spatial->FindNearestHitTriangle(ToEyeRay);
		if (ToEyeHitTID >= 0)
		{
			bHitCorner = bHitEdge = false;
		}
		else
		{
			bHitSurface = false;
		}
	}


	if (bHitCorner)
	{
		ResultOut.SelectedCornerIDs.Add(HitCornerID);
		SelectedPositionOut = HitCornerPt;
		SelectedNormalOut = (bActuallyHitSurface) ?
			Mesh->GetTriNormal(HitTriangleID) : FVector3d::UnitZ();
		return true;
	}
	else if (bHitEdge)
	{
		ResultOut.SelectedEdgeIDs.Add(HitEdgeID);
		SelectedPositionOut = HitEdgePt;
		SelectedNormalOut = (bActuallyHitSurface) ?
			Mesh->GetTriNormal(HitTriangleID) : FVector3d::UnitZ();
		return true;
	}
	else if (bHitSurface)
	{
		ResultOut.SelectedGroupIDs.Add(Mesh->GetTriangleGroup(HitTriangleID));
		SelectedPositionOut = TriangleHitPos;
		SelectedNormalOut = Mesh->GetTriNormal(HitTriangleID);
		return true;
	}

	return false;
}



void FGroupTopologySelector::DrawSelection(const FGroupTopologySelection& Selection, FToolDataVisualizer* Renderer, const FViewCameraState* CameraState)
{
	for (int CornerID : Selection.SelectedCornerIDs)
	{
		FLinearColor UseColor = Renderer->LineColor;
		float LineWidth = Renderer->LineThickness;

		int VertexID = Topology->GetCornerVertexID(CornerID);
		FVector Position = (FVector)Mesh->GetVertex(VertexID);
		FVector WorldPosition = Renderer->TransformP(Position);
		float Radius = (float)ToolSceneQueriesUtil::CalculateDimensionFromVisualAngleD(*CameraState, WorldPosition, 0.5);
		Renderer->DrawViewFacingCircle(Position, Radius, 16, UseColor, LineWidth, false);
	}

	for (int EdgeID : Selection.SelectedEdgeIDs)
	{
		FLinearColor UseColor = Renderer->LineColor;
		float LineWidth = Renderer->LineThickness;
		const TArray<int>& Vertices = Topology->GetGroupEdgeVertices(EdgeID);
		int NV = Vertices.Num() - 1;
		FVector A = (FVector)Mesh->GetVertex(Vertices[0]);
		for (int k = 0; k < NV; ++k)
		{
			FVector B = (FVector)Mesh->GetVertex(Vertices[k+1]);
			Renderer->DrawLine(A, B, UseColor, LineWidth, false);
			A = B;
		}
	}


}
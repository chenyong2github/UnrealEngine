// Copyright Epic Games, Inc. All Rights Reserved.


#include "Selection/GroupTopologySelector.h"
#include "MeshQueries.h"
#include "ToolDataVisualizer.h"
#include "ToolSceneQueriesUtil.h"

// Local utility function forward declarations
bool IsOccluded(const FGeometrySet3::FNearest& ClosestElement, const FVector3d& ViewOrigin, const FDynamicMeshAABBTree3* Spatial);
void AddNewEdgeLoopEdgesFromCorner(const FGroupTopology& Topology, int32 EdgeID, int32 CornerID, TSet<int32>& EdgeSet);
bool GetNextEdgeLoopEdge(const FGroupTopology& Topology, int32 IncomingEdgeID, int32 CornerID, int32& NextEdgeIDOut);
void AddNewEdgeRingEdges(const FGroupTopology& Topology, int32 StartEdgeID, int32 ForwardGroupID, TSet<int32>& EdgeSet);
bool GetQuadOppositeEdge(const FGroupTopology& Topology, int32 EdgeIDIn, int32 GroupID, int32& OppositeEdgeIDOut);

FGroupTopologySelector::FGroupTopologySelector()
{
	// initialize to sane values
	PointsWithinToleranceTest =
		[](const FVector3d& A, const FVector3d& B, double TolScale) { return A.Distance(B) < (TolScale*1.0); };
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

bool FGroupTopologySelector::FindSelectedElement(const FSelectionSettings& Settings, const FRay3d& Ray,
	FGroupTopologySelection& ResultOut, FVector3d& SelectedPositionOut, FVector3d& SelectedNormalOut, int32* EdgeSegmentIdOut)
{
	// These get used for finding intersections with triangles and corners/edges, repectively.
	FDynamicMeshAABBTree3* Spatial = GetSpatial();
	const FGeometrySet3& TopoSpatial = GetGeometrySet();

	// We start by intersecting with the mesh triangles because even when selecting corners or edges, we set
	// the normal based on the true triangle that we hit. If we end up with a simple face selection, we will
	// end up using this result.
	double RayParameter = -1;
	int HitTriangleID = IndexConstants::InvalidID;
	FVector3d TriangleHitPos;
	bool bActuallyHitSurface = (Spatial != nullptr) ? Spatial->FindNearestHitTriangle(Ray, RayParameter, HitTriangleID) : false;
	if (bActuallyHitSurface)
	{
		TriangleHitPos = Ray.PointAt(RayParameter);
		SelectedNormalOut = Mesh->GetTriNormal(HitTriangleID);
	}
	else
	{
		SelectedNormalOut = FVector3d::UnitZ();
	}
	bool bHaveFaceHit = (bActuallyHitSurface && Settings.bEnableFaceHits);
	
	// Deal with corner hits first (and edges that project to a corner)
	FGroupTopologySelection CornerResults;
	FVector3d CornerPosition;
	int32 CornerSegmentEdgeID = 0;
	bool bHaveCornerHit = false;
	double CornerAngle = TNumericLimits<double>::Max();
	if (Settings.bEnableCornerHits || (Settings.bEnableEdgeHits && Settings.bPreferProjectedElement))
	{
		if (DoCornerBasedSelection(Settings, Ray, Spatial, TopoSpatial, CornerResults, CornerPosition, &CornerSegmentEdgeID))
		{
			bHaveCornerHit = true;
			CornerAngle = Ray.Direction.AngleD((CornerPosition - Ray.Origin).Normalized());
		}
	}

	// If corner selection didn't yield results, try edge selection
	FGroupTopologySelection EdgeResults;
	FVector3d EdgePosition;
	int32 EdgeSegmentEdgeID = 0;
	bool bHaveEdgeHit = false;
	double EdgeAngle = TNumericLimits<double>::Max();
	if (Settings.bEnableEdgeHits || (Settings.bEnableFaceHits && Settings.bPreferProjectedElement))
	{
		if (DoEdgeBasedSelection(Settings, Ray, Spatial, TopoSpatial, EdgeResults, EdgePosition, &EdgeSegmentEdgeID))
		{
			bHaveEdgeHit = true;
			EdgeAngle = Ray.Direction.AngleD((EdgePosition - Ray.Origin).Normalized());
		}
	}

	// if we have both corner and edge, want to keep the one we are closer to
	if (bHaveCornerHit && bHaveEdgeHit)
	{
		if (PointsWithinToleranceTest(CornerPosition, Ray.NearestPoint(CornerPosition), 0.75))
		{
			bHaveEdgeHit = false;
		}
		else
		{
			bHaveCornerHit = false;
		}
	}

	// if we have a corner or edge hit, and a face hit, pick face unless we are really close to corner/edge
	if ((bHaveCornerHit || bHaveEdgeHit) && bHaveFaceHit)
	{
		FVector3d TestPos = (bHaveCornerHit) ? CornerPosition : EdgePosition;
		if (!PointsWithinToleranceTest(TestPos, Ray.NearestPoint(TestPos), 0.15))
		{
			bHaveEdgeHit = bHaveCornerHit = false;
		}
	}


	if (bHaveCornerHit)
	{
		ResultOut = CornerResults;
		SelectedPositionOut = CornerPosition;
		if (EdgeSegmentIdOut != nullptr)
		{
			*EdgeSegmentIdOut = CornerSegmentEdgeID;
		}
		return true;
	}
	else if (bHaveEdgeHit)
	{
		ResultOut = EdgeResults;
		SelectedPositionOut = EdgePosition;
		if (EdgeSegmentIdOut != nullptr)
		{
			*EdgeSegmentIdOut = EdgeSegmentEdgeID;
		}
		return true;
	}
	else if (bHaveFaceHit)
	{
		// If we still haven't found a selection, go ahead and select the face that we found earlier
		ResultOut.SelectedGroupIDs.Add(Topology->GetGroupID(HitTriangleID));
		SelectedPositionOut = TriangleHitPos;
		return true;
	}


	return false;
}

bool FGroupTopologySelector::DoCornerBasedSelection(const FSelectionSettings& Settings,
	const FRay3d& Ray, FDynamicMeshAABBTree3* Spatial, const FGeometrySet3& TopoSpatial,
	FGroupTopologySelection& ResultOut, FVector3d& SelectedPositionOut, int32 *EdgeSegmentIdOut) const
{
	// These will store our results, depending on whether we select all along the ray or not.
	FGeometrySet3::FNearest SingleElement;
	// ElementsWithinTolerance gives all nearby elements returned by the topology selector, whereas DownRayElements filters these to just
	// the closest element and those that project directly onto it (and it only stores the IDs, since we don't care about anything else).
	TArray<FGeometrySet3::FNearest> ElementsWithinTolerance;
	TArray<int32> DownRayElements;

	auto LocalTolTest = [this](const FVector3d& A, const FVector3d& B) { return PointsWithinToleranceTest(A, B, 1.0); };

	// Start by getting the closest element
	const FGeometrySet3::FNearest* ClosestElement = nullptr;
	if (!Settings.bSelectDownRay)
	{
		if (TopoSpatial.FindNearestPointToRay(Ray, SingleElement, LocalTolTest))
		{
			ClosestElement = &SingleElement;
		}
	}
	else
	{
		// We're collecting all corners within tolerance, but we still need the closest element
		if (TopoSpatial.CollectPointsNearRay(Ray, ElementsWithinTolerance, LocalTolTest))
		{
			double MinRayT = TNumericLimits<double>::Max();
			for (const FGeometrySet3::FNearest& Element : ElementsWithinTolerance)
			{
				if (Element.RayParam < MinRayT)
				{
					MinRayT = Element.RayParam;
					ClosestElement = &Element;
				}
			}
		}//end if found corners
	}//end if selecting down ray

	// Bail early if we haven't found a corner
	if (ClosestElement == nullptr)
	{
		return false;
	}

	// Also bail if the closest element is not visible.
	if (!Settings.bIgnoreOcclusion && IsOccluded(*ClosestElement, Ray.Origin, Spatial))
	{
		return false;
	}

	// The closest point is already found
	SelectedPositionOut = ClosestElement->NearestGeoPoint;

	// If we have other corners, we actually need to filter them to only those that lie in line with the closest element. Note that
	// this would be done differently depending on whether we're dealing with an orthographic or perspective view: in an
	// orthographic view, we need to see that they lie on a ray with view ray direction and closest corner origin, whereas in
	// perspective, they would need to lie on a ray from camer through closest corner (which will differ due to tolerance).
	// Because the "select down ray" behavior is only useful in orthographic viewports in the first place, we do it that way.
	if (Settings.bSelectDownRay)
	{
		DownRayElements.Add(ClosestElement->ID);
		for (const FGeometrySet3::FNearest& Element : ElementsWithinTolerance)
		{
			if (ClosestElement == &Element)
			{
				continue; // Already added
			}

			// Make sure that closest corner to current element is parallel with view ray
			FVector3d ClosestTowardElement = (Element.NearestGeoPoint - ClosestElement->NearestGeoPoint).Normalized();
			// There would usually be one more abs() in here, but we know that other elements are down ray direction
			if (abs(ClosestTowardElement.Dot(Ray.Direction) - 1.0) < KINDA_SMALL_NUMBER)
			{
				DownRayElements.Add(Element.ID);
			}
		}//end assembling aligned corners
	}

	// Try to select edges that project to corners.
	if (Settings.bPreferProjectedElement && Settings.bEnableEdgeHits)
	{
		TSet<int32> AddedTopologyEdges;

		// See if the closest vertex has an attached edge that is colinear with the view ray. Due to the fact that
		// topology "edges" are actually polylines, we could actually have more than one even for the closest corner
		// (if the "edge" towards us curves away). We'll only grab an edge down our view ray.
		int32 ClosestVid = Topology->GetCornerVertexID(ClosestElement->ID);
		for (int32 Eid : Mesh->VtxEdgesItr(ClosestVid))
		{
			FDynamicMesh3::FEdge Edge = Mesh->GetEdge(Eid);
			int32 OtherVid = (Edge.Vert.A == ClosestVid) ? Edge.Vert.B : Edge.Vert.A;
			FVector3d EdgeVector = (Mesh->GetVertex(OtherVid) - Mesh->GetVertex(ClosestVid)).Normalized();
			if (abs(EdgeVector.Dot(Ray.Direction) - 1.0) < KINDA_SMALL_NUMBER)
			{
				int TopologyEdgeIndex = Topology->FindGroupEdgeID(Eid);
				if (TopologyEdgeIndex >= 0)
				{
					ResultOut.SelectedEdgeIDs.Add(TopologyEdgeIndex);
					AddedTopologyEdges.Add(TopologyEdgeIndex);

					if (EdgeSegmentIdOut)
					{
						Topology->GetGroupEdgeEdges(TopologyEdgeIndex).Find(Eid, *EdgeSegmentIdOut);
					}

					break;
				}
			}
		}

		// If relevant, get all the other colinear edges
		if (Settings.bSelectDownRay && AddedTopologyEdges.Num() > 0)
		{
			for (int i = 1; i < DownRayElements.Num(); ++i) // skip 0 because it is closest and done
			{
				// Look though any attached edges.
				for (int32 Eid : Mesh->VtxEdgesItr(Topology->GetCornerVertexID(DownRayElements[i])))
				{
					FDynamicMesh3::FEdge Edge = Mesh->GetEdge(Eid);
					FVector3d EdgeVector = (Mesh->GetVertex(Edge.Vert.A) - Mesh->GetVertex(Edge.Vert.B)).Normalized();

					// Compare absolute value of dot product to 1. We already made sure that one of the vertices is in line
					// with the closest vertex earlier.
					if (abs(abs(EdgeVector.Dot(Ray.Direction)) - 1.0) < KINDA_SMALL_NUMBER)
					{
						int TopologyEdgeIndex = Topology->FindGroupEdgeID(Eid);
						if (TopologyEdgeIndex >= 0 && !AddedTopologyEdges.Contains(TopologyEdgeIndex))
						{
							ResultOut.SelectedEdgeIDs.Add(TopologyEdgeIndex);
							AddedTopologyEdges.Add(TopologyEdgeIndex);
							// Don't break here because we may have parallel edges in both directions, since we aren't
							// going through the vertices in a particular order.
						}
					}
				}//end checking edges
			}//end going through corners
		}

		// If we found edges, we're done
		if (AddedTopologyEdges.Num() > 0)
		{
			return true;
		}
	}//end selecting projected edges

	// If getting projected edges didn't work out, go ahead and add the corners.
	if (Settings.bEnableCornerHits)
	{
		if (Settings.bSelectDownRay)
		{
			for (int32 Id : DownRayElements)
			{
				ResultOut.SelectedCornerIDs.Add(Id);
			}
		}
		else
		{
			ResultOut.SelectedCornerIDs.Add(ClosestElement->ID);
		}
		return true;
	}

	return false;
}

bool FGroupTopologySelector::DoEdgeBasedSelection(const FSelectionSettings& Settings, const FRay3d& Ray,
	FDynamicMeshAABBTree3* Spatial, const FGeometrySet3& TopoSpatial,
	FGroupTopologySelection& ResultOut, FVector3d& SelectedPositionOut, int32* EdgeSegmentIdOut) const
{
	// These will store our results, depending on whether we select all along the ray or not.
	FGeometrySet3::FNearest SingleElement;
	// ElementsWithinTolerance gives all nearby elements returned by the topology selector, whereas DownRayElements filters these to just
	// the closest element and those that project directly onto it (it stores the polyline and segment IDs, which are all we care about).
	TArray<FGeometrySet3::FNearest> ElementsWithinTolerance;
	TArray<FIndex2i> DownRayElements;

	auto LocalTolTest = [this](const FVector3d& A, const FVector3d& B) { return PointsWithinToleranceTest(A, B, 1.0); };

	// Start by getting the closest element
	const FGeometrySet3::FNearest* ClosestElement = nullptr;
	if (!Settings.bSelectDownRay)
	{
		if (TopoSpatial.FindNearestCurveToRay(Ray, SingleElement, LocalTolTest))
		{
			ClosestElement = &SingleElement;
		}
	}
	else
	{
		// Need all curves within tolerance, but also need to know the closest.
		if (TopoSpatial.CollectCurvesNearRay(Ray, ElementsWithinTolerance, LocalTolTest))
		{
			double MinRayT = TNumericLimits<double>::Max();
			for (const FGeometrySet3::FNearest& Element : ElementsWithinTolerance)
			{
				if (Element.RayParam < MinRayT)
				{
					MinRayT = Element.RayParam;
					ClosestElement = &Element;
				}
			}
		}//end if found edges
	}//end if selecting down ray

	// Bail early if we haven't found at least one edge
	if (ClosestElement == nullptr)
	{
		return false;
	}

	// Also bail if the closest element is not visible.
	if (!Settings.bIgnoreOcclusion && IsOccluded(*ClosestElement, Ray.Origin, Spatial))
	{
		return false;
	}

	// The closest point is already found
	SelectedPositionOut = ClosestElement->NearestGeoPoint;

	// If we have other edges, we need to filter them to only those that project onto the closest element. This would be done
	// differently for perspective cameras vs orthographic projection, but since the behavior is only useful in ortho mode,
	// we do it that way.
	if (Settings.bSelectDownRay)
	{
		// Closest element is a given
		DownRayElements.Add(FIndex2i(ClosestElement->ID, ClosestElement->PolySegmentIdx));

		// We want edges that lie in a plane through the closest edge that is coplanar with the view direction. If we had wanted 
		// to do this for perspective mode, we would have picked a plane through the edge and the camera origin.
		int32 ClosestEid = Topology->GetGroupEdgeEdges(ClosestElement->ID)[ClosestElement->PolySegmentIdx];
		FDynamicMesh3::FEdge ClosestEdge = Mesh->GetEdge(ClosestEid);
		FPlane3d PlaneThroughClosestEdge(Mesh->GetVertex(ClosestEdge.Vert.A), Mesh->GetVertex(ClosestEdge.Vert.B), 
			Mesh->GetVertex(ClosestEdge.Vert.A) + Ray.Direction);

		for (const FGeometrySet3::FNearest& Element : ElementsWithinTolerance)
		{
			if (ClosestElement == &Element)
			{
				continue; // already added
			}

			// See if the edge endpoints lie in the plane
			int32 Eid = Topology->GetGroupEdgeEdges(Element.ID)[Element.PolySegmentIdx];
			FDynamicMesh3::FEdge Edge = Mesh->GetEdge(Eid);
			if (abs(PlaneThroughClosestEdge.DistanceTo(Mesh->GetVertex(Edge.Vert.A))) < KINDA_SMALL_NUMBER
				&& abs(PlaneThroughClosestEdge.DistanceTo(Mesh->GetVertex(Edge.Vert.B))) < KINDA_SMALL_NUMBER)
			{
				DownRayElements.Add(FIndex2i(Element.ID, Element.PolySegmentIdx));
			}
		}
	}

	// Try to select faces that project to the closest edge
	if (Settings.bPreferProjectedElement && Settings.bEnableFaceHits)
	{
		TSet<int32> AddedGroups;

		// Start with the closest edge
		int32 Eid = Topology->GetGroupEdgeEdges(ClosestElement->ID)[ClosestElement->PolySegmentIdx];
		FDynamicMesh3::FEdge Edge = Mesh->GetEdge(Eid);
		FVector3d VertA = Mesh->GetVertex(Edge.Vert.A);
		FVector3d VertB = Mesh->GetVertex(Edge.Vert.B);


		// Grab a plane through the two verts that contains the ray direction (again, this is assuming that we'd
		// only do this in ortho mode, otherwise the plane would go through the ray origin.
		FPlane3d PlaneThroughClosestEdge(VertA, VertB, VertA + Ray.Direction);

		// Checking that the face is coplanar simply entails checking that the opposite vert is in the plane. However,
		// it is possible even for the closest edge to have multiple coplanar faces if a group going towards
		// the camera curves away. We need to be able to select just one, and we want to select the one that
		// extends down the view ray. For that, we'll make sure that a vector to the opposite vertex lies on the same
		// side of the edge as the view ray.
		FVector3d EdgeVector = VertB - VertA;
		FVector3d EdgeVecCrossDirection = EdgeVector.Cross(Ray.Direction);
		FIndex2i OppositeVids = Mesh->GetEdgeOpposingV(Eid);

		FVector3d OppositeVert = Mesh->GetVertex(OppositeVids.A);
		if (abs(PlaneThroughClosestEdge.DistanceTo(OppositeVert)) < KINDA_SMALL_NUMBER
			&& EdgeVector.Cross(OppositeVert - VertA).Dot(EdgeVecCrossDirection) > 0)
		{
			int GroupId = Topology->GetGroupID(Edge.Tri.A);
			ResultOut.SelectedGroupIDs.Add(GroupId);
			AddedGroups.Add(GroupId);
		}
		else if (OppositeVids.B != FDynamicMesh3::InvalidID)
		{
			OppositeVert = Mesh->GetVertex(OppositeVids.B);

			if (abs(PlaneThroughClosestEdge.DistanceTo(OppositeVert)) < KINDA_SMALL_NUMBER
				&& EdgeVector.Cross(OppositeVert - VertA).Dot(EdgeVecCrossDirection) > 0)
			{
				int GroupId = Topology->GetGroupID(Edge.Tri.B);
				ResultOut.SelectedGroupIDs.Add(GroupId);
				AddedGroups.Add(GroupId);
			}
		}

		// If relevant, get all the other coplanar faces
		if (Settings.bSelectDownRay && AddedGroups.Num() > 0)
		{
			for (int i = 1; i < DownRayElements.Num(); ++i) // skip 0 because it is closest and done
			{
				// We already made sure that all these edges are coplanar, so we'll just be checking opposite verts.
				Eid = Topology->GetGroupEdgeEdges(DownRayElements[i].A)[DownRayElements[i].B];
				Edge = Mesh->GetEdge(Eid);
				OppositeVids = Mesh->GetEdgeOpposingV(Eid);

				// No need to check directionality of faces here, just need them to be coplanar.
				if (abs(PlaneThroughClosestEdge.DistanceTo(Mesh->GetVertex(OppositeVids.A))) < KINDA_SMALL_NUMBER)
				{
					int GroupId = Topology->GetGroupID(Edge.Tri.A);
					if (!AddedGroups.Contains(GroupId))
					{
						ResultOut.SelectedGroupIDs.Add(GroupId);
						AddedGroups.Add(GroupId);
					}
				}
				if (OppositeVids.B != FDynamicMesh3::InvalidID
					&& abs(PlaneThroughClosestEdge.DistanceTo(Mesh->GetVertex(OppositeVids.B))) < KINDA_SMALL_NUMBER)
				{
					int GroupId = Topology->GetGroupID(Edge.Tri.B);
					if (!AddedGroups.Contains(GroupId))
					{
						ResultOut.SelectedGroupIDs.Add(GroupId);
						AddedGroups.Add(GroupId);
					}
				}
			}//end going through other edges
		}

		// If we selected faces, we're done
		if (AddedGroups.Num() > 0)
		{
			return true;
		}
	}

	// If we didn't end up selecting projected faces, and we have edges to select, select them
	if (Settings.bEnableEdgeHits)
	{
		if (Settings.bSelectDownRay)
		{
			for (const FIndex2i ElementTuple : DownRayElements)
			{
				ResultOut.SelectedEdgeIDs.Add(ElementTuple.A);
			}
		}
		else
		{
			ResultOut.SelectedEdgeIDs.Add(ClosestElement->ID);
		}

		if (EdgeSegmentIdOut)
		{
			*EdgeSegmentIdOut = ClosestElement->PolySegmentIdx;
		}

		return true;
	}
	return false;
}

bool IsOccluded(const FGeometrySet3::FNearest& ClosestElement, const FVector3d& ViewOrigin, const FDynamicMeshAABBTree3* Spatial)
{
	// Shoot ray backwards to see if we hit something. 
	FRay3d ToEyeRay(ClosestElement.NearestGeoPoint, (ViewOrigin - ClosestElement.NearestGeoPoint).Normalized(), true);
	ToEyeRay.Origin += (double)(100 * FMathf::ZeroTolerance) * ToEyeRay.Direction;
	if (Spatial->FindNearestHitTriangle(ToEyeRay) >= 0)
	{
		return true;
	}
	return false;
}


bool FGroupTopologySelector::ExpandSelectionByEdgeLoops(FGroupTopologySelection& Selection)
{
	TSet<int32> EdgeSet(Selection.SelectedEdgeIDs);
	int32 OriginalNumEdges = Selection.SelectedEdgeIDs.Num();
	for (int32 Eid : Selection.SelectedEdgeIDs)
	{
		const FGroupTopology::FGroupEdge& Edge = Topology->Edges[Eid];
		if (Edge.EndpointCorners[0] == IndexConstants::InvalidID)
		{
			continue; // This FGroupEdge is a loop unto itself (and already in our selection, since we're looking at it).
		}

		// Go forward and backward adding edges
		AddNewEdgeLoopEdgesFromCorner(*Topology, Eid, Edge.EndpointCorners[0], EdgeSet);
		AddNewEdgeLoopEdgesFromCorner(*Topology, Eid, Edge.EndpointCorners[1], EdgeSet);
	}

	if (EdgeSet.Num() > OriginalNumEdges)
	{
		Selection.SelectedEdgeIDs.Append(EdgeSet);
		return true;
	}
	else
	{
		return false;
	}
}

void AddNewEdgeLoopEdgesFromCorner(const FGroupTopology& Topology, int32 EdgeID, int32 CornerID, TSet<int32>& EdgeSet)
{
	const FGroupTopology::FCorner& CurrentCorner = Topology.Corners[CornerID];

	int32 LastCornerID = CornerID;
	int32 LastEdgeID = EdgeID;
	while (true)
	{
		int32 NextEid;
		if (!GetNextEdgeLoopEdge(Topology, LastEdgeID, LastCornerID, NextEid))
		{
			break; // Probably not a valence 4 corner
		}
		if (EdgeSet.Contains(NextEid))
		{
			break; // Either we finished the loop, or we'll continue it from another selection
		}

		EdgeSet.Add(NextEid);

		LastEdgeID = NextEid;
		const FGroupTopology::FGroupEdge& LastEdge = Topology.Edges[LastEdgeID];
		LastCornerID = LastEdge.EndpointCorners[0] == LastCornerID ? LastEdge.EndpointCorners[1] : LastEdge.EndpointCorners[0];

		check(LastCornerID != IndexConstants::InvalidID);
	}
}

bool GetNextEdgeLoopEdge(const FGroupTopology& Topology, int32 IncomingEdgeID, int32 CornerID, int32& NextEdgeIDOut)
{
	// It's worth noting that the approach here breaks down in pathological cases where the same group is present
	// multiple times around a corner (i.e. the group is not contiguous, and separate islands share a corner).
	// It's not practical to worry about those cases.

	NextEdgeIDOut = IndexConstants::InvalidID;
	const FGroupTopology::FCorner& CurrentCorner = Topology.Corners[CornerID];

	if (CurrentCorner.NeighbourGroupIDs.Num() != 4)
	{
		return false; // Not a valence 4 corner
	}

	const FGroupTopology::FGroupEdge& IncomingEdge = Topology.Edges[IncomingEdgeID];

	// We want to find the edge that shares this corner but does not border either of the neighboring groups of
	// the incoming edge.

	for (int32 Gid : CurrentCorner.NeighbourGroupIDs)
	{
		if (Gid == IncomingEdge.Groups[0] || Gid == IncomingEdge.Groups[1])
		{
			continue; // This is one of the neighboring groups of the incoming edge
		}

		// Iterate through all edges of group
		const FGroupTopology::FGroup* Group = Topology.FindGroupByID(Gid);
		for (const FGroupTopology::FGroupBoundary& Boundary : Group->Boundaries)
		{
			for (int32 Eid : Boundary.GroupEdges)
			{
				const FGroupTopology::FGroupEdge& CandidateEdge = Topology.Edges[Eid];

				// Edge must share corner but not neighboring groups
				if ((CandidateEdge.EndpointCorners[0] == CornerID || CandidateEdge.EndpointCorners[1] == CornerID)
					&& CandidateEdge.Groups[0] != IncomingEdge.Groups[0] && CandidateEdge.Groups[0] != IncomingEdge.Groups[1]
					&& CandidateEdge.Groups[1] != IncomingEdge.Groups[0] && CandidateEdge.Groups[1] != IncomingEdge.Groups[1])
				{
					NextEdgeIDOut = Eid;
					return true;
				}
			}
		}
	}
	return false;
}

bool FGroupTopologySelector::ExpandSelectionByEdgeRings(FGroupTopologySelection& Selection)
{
	TSet<int32> EdgeSet(Selection.SelectedEdgeIDs);
	int32 OriginalNumEdges = Selection.SelectedEdgeIDs.Num();
	for (int32 Eid : Selection.SelectedEdgeIDs)
	{
		const FGroupTopology::FGroupEdge& Edge = Topology->Edges[Eid];

		// Go forward and backward adding edges
		if (Edge.Groups[0] != IndexConstants::InvalidID)
		{
			AddNewEdgeRingEdges(*Topology, Eid, Edge.Groups[0], EdgeSet);
		}
		if (Edge.Groups[0] != IndexConstants::InvalidID)
		{
			AddNewEdgeRingEdges(*Topology, Eid, Edge.Groups[1], EdgeSet);
		}
	}

	if (EdgeSet.Num() > OriginalNumEdges)
	{
		Selection.SelectedEdgeIDs.Append(EdgeSet);
		return true;
	}
	else
	{
		return false;
	}
}

void AddNewEdgeRingEdges(const FGroupTopology& Topology, int32 StartEdgeID, int32 ForwardGroupID, TSet<int32>& EdgeSet)
{
	int32 CurrentEdgeID = StartEdgeID;
	int32 CurrentForwardGroupID = ForwardGroupID;
	while (true)
	{
		if (ForwardGroupID == IndexConstants::InvalidID)
		{
			break; // Reached a boundary
		}

		int32 NextEdgeID;
		if (!GetQuadOppositeEdge(Topology, CurrentEdgeID, CurrentForwardGroupID, NextEdgeID))
		{
			break; // Probably not a quad
		}
		if (EdgeSet.Contains(NextEdgeID))
		{
			break; // Either we finished the loop, or we'll continue it from another selection
		}

		EdgeSet.Add(NextEdgeID);

		CurrentEdgeID = NextEdgeID;
		const FGroupTopology::FGroupEdge& Edge = Topology.Edges[CurrentEdgeID];
		CurrentForwardGroupID = (Edge.Groups[0] == CurrentForwardGroupID) ? Edge.Groups[1] : Edge.Groups[0];
	}
}

bool GetQuadOppositeEdge(const FGroupTopology& Topology, int32 EdgeIDIn, int32 GroupID, int32& OppositeEdgeIDOut)
{
	const FGroupTopology::FGroup* Group = Topology.FindGroupByID(GroupID);
	check(Group);

	// Find the boundary that contains this edge
	for (int32 i = 0; i < Group->Boundaries.Num(); ++i)
	{
		const FGroupTopology::FGroupBoundary& Boundary = Group->Boundaries[i];
		int32 EdgeIndex = Boundary.GroupEdges.IndexOfByKey(EdgeIDIn);
		if (EdgeIndex != INDEX_NONE)
		{
			if (Boundary.GroupEdges.Num() != 4)
			{
				return false;
			}

			OppositeEdgeIDOut = Boundary.GroupEdges[(EdgeIndex + 2) % 4];
			return true;
		}
	}
	check(false); // No boundary of the given group contained the given edge
	return false;
}


void FGroupTopologySelector::DrawSelection(const FGroupTopologySelection& Selection, FToolDataVisualizer* Renderer, const FViewCameraState* CameraState)
{
	FLinearColor UseColor = Renderer->LineColor;
	float LineWidth = Renderer->LineThickness;

	for (int CornerID : Selection.SelectedCornerIDs)
	{
		int VertexID = Topology->GetCornerVertexID(CornerID);
		FVector Position = (FVector)Mesh->GetVertex(VertexID);
		FVector WorldPosition = Renderer->TransformP(Position);
		
		// Depending on whether we're in an orthographic view or not, we set the radius based on visual angle or based on ortho 
		// viewport width (divided into 90 segments like the FOV is divided into 90 degrees).
		float Radius = (CameraState->bIsOrthographic) ? CameraState->OrthoWorldCoordinateWidth * 0.5 / 90.0
			: (float)ToolSceneQueriesUtil::CalculateDimensionFromVisualAngleD(*CameraState, WorldPosition, 0.5);
		Renderer->DrawViewFacingCircle(Position, Radius, 16, UseColor, LineWidth, false);
	}

	for (int EdgeID : Selection.SelectedEdgeIDs)
	{
		const TArray<int>& Vertices = Topology->GetGroupEdgeVertices(EdgeID);
		int NV = Vertices.Num() - 1;

		// Draw the edge, but also draw the endpoints in ortho mode (to make projected edges visible)
		FVector A = (FVector)Mesh->GetVertex(Vertices[0]);
		if (CameraState->bIsOrthographic)
		{
			Renderer->DrawPoint(A, UseColor, 10, false);
		}
		for (int k = 0; k < NV; ++k)
		{
			FVector B = (FVector)Mesh->GetVertex(Vertices[k+1]);
			Renderer->DrawLine(A, B, UseColor, LineWidth, false);
			A = B;
		}
		if (CameraState->bIsOrthographic)
		{
			Renderer->DrawPoint(A, UseColor, LineWidth, false);
		}
	}

	// We are not responsible for drawing the faces, but do draw the sides of the faces in ortho mode to make them visible
	// when they project to an edge.
	if (CameraState->bIsOrthographic && Selection.SelectedGroupIDs.Num() > 0)
	{
		Topology->ForGroupSetEdges(Selection.SelectedGroupIDs,
			[&UseColor, LineWidth, Renderer, this] (FGroupTopology::FGroupEdge Edge, int EdgeID) 
		{
			Topology->GetGroupEdgeVertices(EdgeID);
			const TArray<int>& Vertices = Topology->GetGroupEdgeVertices(EdgeID);
			int NV = Vertices.Num() - 1;
			FVector A = (FVector)Mesh->GetVertex(Vertices[0]);
			for (int k = 0; k < NV; ++k)
			{
				FVector B = (FVector)Mesh->GetVertex(Vertices[k + 1]);
				Renderer->DrawLine(A, B, UseColor, LineWidth, false);
				A = B;
			}
		});
	}
}
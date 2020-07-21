// Copyright Epic Games, Inc. All Rights Reserved.

#include "CleaningOps/HoleFillOp.h"
#include "Operations/SimpleHoleFiller.h"
#include "Operations/PlanarHoleFiller.h"
#include "Operations/MinimalHoleFiller.h"
#include "Operations/SmoothHoleFiller.h"
#include "ConstrainedDelaunay2.h"
#include "CompGeom/PolygonTriangulation.h"
#include "MeshQueries.h"

namespace 
{
	bool LoopIsValid(const FDynamicMesh3& Mesh, const FEdgeLoop& Loop)
	{
		if (Loop.Edges.Num() == 0) 
		{ 
			return false; 
		}

		for (int EdgeID : Loop.Edges)
		{
			if (!Mesh.IsBoundaryEdge(EdgeID))
			{
				return false;
			}
		}

		FEdgeLoop CheckLoop(&Mesh, Loop.Vertices, Loop.Edges);
		if (!CheckLoop.CheckValidity(EValidityCheckFailMode::ReturnOnly))
		{
			return false;
		}

		for (int VertexID : Loop.Vertices)
		{
			if (!Mesh.IsBoundaryVertex(VertexID))
			{
				// Not sure how this can happen if the edges are all valid...
				check(false);
			}
		}

		return true;
	}

	/// Look for a loop around a single isolated triangle. A triangle is isolated if its vertices are only
	/// incident on that triangle (i.e. triangles connected by a bowtie connection are not considered isolated.)
	bool LoopIsAnIsolatedTriangle(const FDynamicMesh3& Mesh, const FEdgeLoop& Loop, int& TriangleID )
	{
		if (Loop.Edges.Num() != 3)
		{
			return false;
		}

		check(Mesh.IsBoundaryEdge(Loop.Edges[0]));
		check(Mesh.IsBoundaryEdge(Loop.Edges[1]));
		check(Mesh.IsBoundaryEdge(Loop.Edges[2]));

		FDynamicMesh3::FEdge Edge0 = Mesh.GetEdge(Loop.Edges[0]);
		FDynamicMesh3::FEdge Edge1 = Mesh.GetEdge(Loop.Edges[1]);
		FDynamicMesh3::FEdge Edge2 = Mesh.GetEdge(Loop.Edges[2]);

		// Return true if all edges are incident on the same triangle...
		TriangleID = Edge0.Tri[0];
		if (TriangleID != Edge1.Tri[0]) { return false; }
		if (TriangleID != Edge2.Tri[0]) { return false; }

		// ...and the triangle's vertices are only connected to one triangle.
		const FIndex3i& Verts = Mesh.GetTriangle(TriangleID);
		return ((Mesh.GetVtxTriangleCount(Verts[0]) == 1) && 
				(Mesh.GetVtxTriangleCount(Verts[1]) == 1) && 
				(Mesh.GetVtxTriangleCount(Verts[2]) == 1));
	}

	TUniquePtr<FSmoothHoleFiller> MakeSmoothHoleFiller(FDynamicMesh3& Mesh, FEdgeLoop& Loop, const FSmoothFillOptions& Options)
	{
		TUniquePtr<FSmoothHoleFiller> Filler = MakeUnique<FSmoothHoleFiller>(Mesh, Loop);
		Filler->FillOptions = Options;
		return Filler;
	}

}

void FHoleFillOp::CalculateResult(FProgressCancel* Progress)
{
	NumFailedLoops = 0;

	if (Progress->Cancelled())
	{
		return;
	}

	ResultMesh->Copy(*OriginalMesh, true, true, true, true);

	if (Loops.Num() == 0)
	{
		return;
	}

	if (Progress->Cancelled())
	{
		return;
	}

	TSet<int32> NewGroupIDs;

	for (FEdgeLoop& Loop : Loops)
	{
		if (!LoopIsValid(*ResultMesh, Loop))
		{ 
			++NumFailedLoops;
			continue;
		}

		if (FillOptions.bRemoveIsolatedTriangles)
		{
			int IsolatedTriangleID;
			if (LoopIsAnIsolatedTriangle(*ResultMesh, Loop, IsolatedTriangleID))
			{
				ResultMesh->RemoveTriangle(IsolatedTriangleID);
				continue;
			}
		}

		int32 NewGroupID = ResultMesh->AllocateTriangleGroup();
		NewGroupIDs.Add(NewGroupID);

		// Compute a best-fit plane of the boundary vertices
		TArray<FVector3d> VertexPositions;
		Loop.GetVertices(VertexPositions);
		FVector3d PlaneOrigin;
		FVector3d PlaneNormal;
		PolygonTriangulation::ComputePolygonPlane<double>(VertexPositions, PlaneNormal, PlaneOrigin);
		PlaneNormal *= -1.0;	// Previous function seems to orient the normal opposite to what's expected elsewhere

		// Now fill using the appropriate algorithm
		TUniquePtr<IHoleFiller> Filler;
		TArray<TArray<int>> VertexLoops;

		switch (FillType)
		{
		case EHoleFillOpFillType::TriangleFan:
			Filler = MakeUnique<FSimpleHoleFiller>(ResultMesh.Get(), Loop, FSimpleHoleFiller::EFillType::TriangleFan);
			break;
		case EHoleFillOpFillType::PolygonEarClipping:
			Filler = MakeUnique<FSimpleHoleFiller>(ResultMesh.Get(), Loop, FSimpleHoleFiller::EFillType::PolygonEarClipping);
			break;
		case EHoleFillOpFillType::Planar:
			VertexLoops.Add(Loop.Vertices);
			Filler = MakeUnique<FPlanarHoleFiller>(ResultMesh.Get(),
				&VertexLoops,
				ConstrainedDelaunayTriangulate<double>,
				PlaneOrigin,
				PlaneNormal);
			break;
		case EHoleFillOpFillType::Minimal:
			Filler = MakeUnique<FMinimalHoleFiller>(ResultMesh.Get(), Loop);
			break;
		case EHoleFillOpFillType::Smooth:
			Filler = MakeSmoothHoleFiller(*ResultMesh, Loop, SmoothFillOptions);			
			break;
		default:
			check(false);
		}

		check(Filler);
		bool bFilled = Filler->Fill(NewGroupID);

		if (!bFilled)
		{
			++NumFailedLoops;
			continue;
		}

		// Compute normals and UVs
		if (ResultMesh->HasAttributes())
		{
			FDynamicMeshEditor Editor(ResultMesh.Get());
			FFrame3d ProjectionFrame(PlaneOrigin, PlaneNormal);
			Editor.SetTriangleNormals(Filler->NewTriangles, (FVector3f)PlaneNormal);
			Editor.SetTriangleUVsFromProjection(Filler->NewTriangles, ProjectionFrame, MeshUVScaleFactor);
		}

		if (Progress->Cancelled())
		{
			return;
		}

	}	// for Loops

	// NewGroupIDs are assigned to triangles in the fill regions, which are the ones we want to select/highlight by
	// adding them to NewTriangles
	NewTriangles.Empty();
	for (int TriangleID : ResultMesh->TriangleIndicesItr())
	{
		int GroupID = ResultMesh->GetTriangleGroup(TriangleID);
		if (NewGroupIDs.Contains(GroupID))
		{
			NewTriangles.Emplace(TriangleID);
		}
	}
	
}


// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConversionUtils/VolumeToDynamicMesh.h"

#include "CompGeom/PolygonTriangulation.h"
#include "DynamicMesh3.h"
#include "GameFramework/Volume.h"
#include "MeshBoundaryLoops.h"
#include "MeshQueries.h"
#include "MeshRegionBoundaryLoops.h"
#include "Model.h"
#include "Operations/MergeCoincidentMeshEdges.h"
#include "Operations/MinimalHoleFiller.h"
#include "Operations/PlanarFlipsOptimization.h"

namespace UE {
namespace Conversion {

void VolumeToDynamicMesh(AVolume* Volume, FDynamicMesh3& Mesh, 
	const FVolumeToMeshOptions& Options)
{
	Mesh.DiscardAttributes();
	if (Options.bSetGroups)
	{
		Mesh.EnableTriangleGroups();
	}

	UModel* Model = Volume->Brush;
	FTransform3d XForm = (Options.bInWorldSpace) ? FTransform3d(Volume->GetTransform()) : FTransform3d::Identity();

	// Each "BspNode" is a planar polygon, triangulate each polygon and accumulate in a mesh.
	// Note that this does not make any attempt to weld vertices/edges
	for (const FBspNode& Node : Model->Nodes)
	{
		FVector3d Normal = (FVector3d)Node.Plane;
		FFrame3d Plane(Node.Plane.W * Normal, Normal);

		int32 NumVerts = (Node.NodeFlags & PF_TwoSided) ? Node.NumVertices / 2 : Node.NumVertices;  // ??
		if (NumVerts > 0)
		{
			TArray<int32> VertIndices;
			TArray<FVector2d> VertPositions2d;
			VertIndices.SetNum(NumVerts);
			VertPositions2d.SetNum(NumVerts);
			for (int32 VertexIndex = 0; VertexIndex < NumVerts; ++VertexIndex)
			{
				const FVert& Vert = Model->Verts[Node.iVertPool + VertexIndex];
				FVector3d Point = (FVector3d)Model->Points[Vert.pVertex];
				Point = XForm.TransformPosition(Point);
				VertIndices[VertexIndex] = Mesh.AppendVertex(Point);
				VertPositions2d[VertexIndex] = Plane.ToPlaneUV(Point, 2);
			}

			TArray<FIndex3i> PolyTriangles;
			PolygonTriangulation::TriangulateSimplePolygon(VertPositions2d, PolyTriangles);

			int32 GroupID = FDynamicMesh3::InvalidID;
			if (Options.bSetGroups)
			{
				GroupID = Mesh.AllocateTriangleGroup();
			}

			for (FIndex3i Tri : PolyTriangles)
			{
				// flip orientation here...
				Mesh.AppendTriangle(VertIndices[Tri.A], VertIndices[Tri.C], VertIndices[Tri.B], GroupID);
			}
		}
	}

	if (Options.bMergeVertices)
	{
		// Merge the mesh edges to create a closed solid
		double MinLen, MaxLen, AvgLen;
		TMeshQueries<FDynamicMesh3>::EdgeLengthStats(Mesh, MinLen, MaxLen, AvgLen);
		FMergeCoincidentMeshEdges Merge(&Mesh);
		Merge.MergeVertexTolerance = FMathd::Max(Merge.MergeVertexTolerance, MinLen * 0.1);
		Merge.Apply();

		// If the mesh is not closed, the merge failed or the volume had cracks/holes. 
		// Do trivial hole fills to ensure the output is solid   (really want autorepair here)
		if (Mesh.IsClosed() == false && Options.bAutoRepairMesh)
		{
			FMeshBoundaryLoops BoundaryLoops(&Mesh, true);
			for (FEdgeLoop& Loop : BoundaryLoops.Loops)
			{
				FMinimalHoleFiller Filler(&Mesh, Loop);
				Filler.Fill();
			}
		}


		// try to flip towards better triangles in planar areas, should reduce/remove degenerate geo
		if (Options.bOptimizeMesh)
		{
			FPlanarFlipsOptimization(&Mesh, 5).Apply(); // Do five passes
		}
	}
}

}}//end namespace UE::Conversion
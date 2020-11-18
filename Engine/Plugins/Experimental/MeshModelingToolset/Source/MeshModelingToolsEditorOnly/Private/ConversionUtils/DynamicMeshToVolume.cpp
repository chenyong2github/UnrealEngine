// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConversionUtils/DynamicMeshToVolume.h"

#include "BSPOps.h"
#include "DynamicMesh3.h"
#include "Engine/Classes/Engine/Polys.h"
#include "GameFramework/Volume.h"
#include "MeshNormals.h"
#include "MeshRegionBoundaryLoops.h"
#include "Model.h"
#include "Selections/MeshConnectedComponents.h"


namespace UE {
namespace Conversion {

void DynamicMeshToVolume(const FDynamicMesh3& InputMesh, AVolume* TargetVolume)
{
	TArray<FDynamicMeshFace> Faces;
	GetPolygonFaces(InputMesh, Faces);
	DynamicMeshToVolume(InputMesh, Faces, TargetVolume);
}

void DynamicMeshToVolume(const FDynamicMesh3& InputMesh, TArray<FDynamicMeshFace>& Faces, AVolume* TargetVolume)
{
	check(TargetVolume->Brush);

	UModel* Model = TargetVolume->Brush;

	Model->Modify();

	Model->Initialize(TargetVolume);
	UPolys* Polys = Model->Polys;

	for (FDynamicMeshFace& Face : Faces)
	{
		int32 NumVertices = Face.BoundaryLoop.Num();
		FVector Normal = (FVector)Face.Plane.Z();
		FVector U = (FVector)Face.Plane.X();
		FVector V = (FVector)Face.Plane.Y();
		FPlane FacePlane = Face.Plane.ToFPlane();

		// create FPoly. This is Editor-only and I'm not entirely sure we need it?
		int32 PolyIndex = Polys->Element.Num();
		FPoly NewPoly;
		NewPoly.Base = (FVector)Face.BoundaryLoop[0];
		NewPoly.Normal = Normal;
		NewPoly.TextureU = U;
		NewPoly.TextureV = V;
		NewPoly.Vertices.SetNum(NumVertices);
		for (int32 k = 0; k < NumVertices; ++k)
		{
			NewPoly.Vertices[k] = (FVector)Face.BoundaryLoop[k];
		}
		NewPoly.PolyFlags = 0;
		NewPoly.iLink = NewPoly.iLinkSurf = NewPoly.iBrushPoly = -1;
		NewPoly.SmoothingMask = 0;
		Polys->Element.Add(NewPoly);

		/*

				// create points for this face in UModel::Points
				// TODO: can we share points between faces?
				TArray<int32> PointIndices;
				PointIndices.SetNum(NumVertices);
				for (int32 k = 0; k < NumVertices; ++k)
				{
					int32 NewIdx = Model->Points.Num();
					Model->Points.Add((FVector)Face.BoundaryLoop[k]);
					PointIndices[k] = NewIdx;
				}
				int32 BasePointIndex = PointIndices[0];

				// create normal for this face in UModel::Vectors along with U and V direction vectors
				int32 NormalIdx = Model->Vectors.Num();
				Model->Vectors.Add(Normal);
				int32 TextureUIdx = Model->Vectors.Num();
				Model->Vectors.Add(U);
				int32 TextureVIdx = Model->Vectors.Num();
				Model->Vectors.Add(V);

				// create FVerts for this face in UModel::Verts
				int32 iVertPoolStart = Model->Verts.Num();
				for (int32 k = 0; k < NumVertices; ++k)
				{
					FVert NewVert;
					NewVert.pVertex = PointIndices[k];		// Index of vertex point.
					NewVert.iSide = INDEX_NONE;				// If shared, index of unique side. Otherwise INDEX_NONE.
					NewVert.ShadowTexCoord = FVector2D::ZeroVector;			// The vertex's shadow map coordinate.
					NewVert.BackfaceShadowTexCoord = FVector2D::ZeroVector;	// The vertex's shadow map coordinate for the backface of the node.
					Model->Verts.Add(NewVert);
				}

				// create Surf

				int32 SurfIndex = Model->Surfs.Num();
				FBspSurf NewSurf;
				NewSurf.Material = nullptr;			// 4 Material.
				NewSurf.PolyFlags = 0;				// 4 Polygon flags.
				NewSurf.pBase = BasePointIndex;		// 4 Polygon & texture base point index (where U,V==0,0).
				NewSurf.vNormal = NormalIdx;		// 4 Index to polygon normal.
				NewSurf.vTextureU = TextureUIdx;	// 4 Texture U-vector index.
				NewSurf.vTextureV = TextureVIdx;	// 4 Texture V-vector index.
				//NewSurf.iBrushPoly = PolyIndex;		// 4 Editor brush polygon index.
				NewSurf.iBrushPoly = -1;
				//NewSurf.Actor = NewVolume;			// 4 Brush actor owning this Bsp surface.
				NewSurf.Actor = nullptr;
				NewSurf.Plane = FacePlane;			// 16 The plane this surface lies on.
				Model->Surfs.Add(NewSurf);


				// create nodes for this face in UModel::Nodes

				FBspNode NewNode;
				NewNode.Plane = FacePlane;					// 16 Plane the node falls into (X, Y, Z, W).
				NewNode.iVertPool = iVertPoolStart;			// 4  Index of first vertex in vertex pool, =iTerrain if NumVertices==0 and NF_TerrainFront.
				NewNode.iSurf = SurfIndex;					// 4  Index to surface information.
				NewNode.iVertexIndex = INDEX_NONE;			// The index of the node's first vertex in the UModel's vertex buffer.
															// This is initialized by UModel::UpdateVertices()
				NewNode.NumVertices = NumVertices;			// 1  Number of vertices in node.
				NewNode.NodeFlags = 0;						// 1  Node flags.
				Model->Nodes.Add(NewNode);
		*/
	}

	// requires ed
	FBSPOps::csgPrepMovingBrush(TargetVolume);

	// do we need to do this?
	TargetVolume->MarkPackageDirty();
}


void GetPolygonFaces(const FDynamicMesh3& InputMesh, TArray<FDynamicMeshFace>& Faces)
{
	Faces.SetNum(0);

	FMeshNormals Normals(&InputMesh);
	Normals.ComputeTriangleNormals();

	double PlanarTolerance = FMathf::ZeroTolerance;

	FMeshConnectedComponents Components(&InputMesh);
	Components.FindConnectedTriangles([&](int32 Triangle0, int32 Triangle1)
		{
			FVector3d Origin = InputMesh.GetTriCentroid(Triangle0);
			FVector3d Normal = Normals[Triangle0];

			FVector3d A, B, C;
			InputMesh.GetTriVertices(Triangle1, A, B, C);;
			double DistA = (A - Origin).Dot(Normal);
			double DistB = (B - Origin).Dot(Normal);
			double DistC = (C - Origin).Dot(Normal);
			double MaxDist = FMathd::Max3(FMathd::Abs(DistA), FMathd::Abs(DistB), FMathd::Abs(DistC));

			return MaxDist < PlanarTolerance;
		});

	for (const FMeshConnectedComponents::FComponent& Component : Components)
	{
		FVector3d FaceNormal = Normals[Component.Indices[0]];

		FMeshRegionBoundaryLoops Loops(&InputMesh, Component.Indices);
		for (const FEdgeLoop& Loop : Loops.Loops)
		{
			FDynamicMeshFace Face;

			FVector3d AvgPos(0, 0, 0);
			for (int32 vid : Loop.Vertices)
			{
				FVector3d Position = InputMesh.GetVertex(vid);
				Face.BoundaryLoop.Add(Position);
				AvgPos += Position;
			}
			AvgPos /= (double)Loop.Vertices.Num();
			Algo::Reverse(Face.BoundaryLoop);

			Face.Plane = FFrame3d(AvgPos, FaceNormal);

			Faces.Add(Face);
		}
	}
}

void GetTriangleFaces(const FDynamicMesh3& InputMesh, TArray<FDynamicMeshFace>& Faces)
{
	Faces.SetNum(0);

	for (int32 tid : InputMesh.TriangleIndicesItr())
	{
		FVector3d A, B, C;
		InputMesh.GetTriVertices(tid, A, B, C);
		FVector3d Centroid, Normal; double Area;
		InputMesh.GetTriInfo(tid, Normal, Area, Centroid);

		FDynamicMeshFace Face;
		Face.Plane = FFrame3d(Centroid, Normal);
		Face.BoundaryLoop.Add(A);
		Face.BoundaryLoop.Add(C);
		Face.BoundaryLoop.Add(B);

		Faces.Add(Face);
	}
}

}//end namespace UE::Conversion
}//end namespace UE
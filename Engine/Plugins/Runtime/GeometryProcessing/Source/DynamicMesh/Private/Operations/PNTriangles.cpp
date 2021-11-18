// Copyright Epic Games, Inc. All Rights Reserved.

#include "Operations/PNTriangles.h"
#include "IndexTypes.h"
#include "VectorTypes.h"
#include "DynamicMesh/MeshNormals.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Async/ParallelFor.h"
#include "Util/ProgressCancel.h"

using namespace UE::Geometry;

namespace FPNTrianglesLocals
{ 
	/**
	 * PN Triangle is represented by a control points cage. Each edge has 2 control points and 
	 * 1 control point is inside the triangle. Additionally, each edge contains one control point 
	 * of the normal component. We separate control points into 2 categories to avoid repeating 
	 * computations since control points on edges are shared.
	 */
		
	using FTriangleControlPoint = FVector3d;

	struct FEdgeControlPoints
	{	
		FVector3d Point1;
		FVector3d Point2;
		FVector3d NormalTriA;
		FVector3d NormalTriB;
	}; 
	
	struct FControlPoints 
	{
		TArray<FEdgeControlPoints> OnEdges; // Map edge ID to the edge control points
		TArray<FTriangleControlPoint> OnTriangles; // Map triangle ID to the triangle center control point
	};

	// Used to save just the connectivity information of a mesh
	struct FMeshConnectivity
	{
		FMeshConnectivity(FDynamicMesh3& Mesh) 
		{
			Triangles = Mesh.GetTrianglesBuffer();
			Edges = Mesh.GetEdgesBuffer();
			TriangleEdges = Mesh.GetTriangleEdges();
		}
		
		TDynamicVector<FIndex3i> Triangles;
		TDynamicVector<FDynamicMesh3::FEdge> Edges;
		TDynamicVector<FIndex3i> TriangleEdges;
	};

	/**
	 *  Compute PN Triangle control points and optionally their normal component.
	 * 
	 *  @param Mesh The mesh used to compute the per-triangle control points.
	 *  @param UseNormals If normals are disabled for the Mesh then pass manually computed normals.
	 *  @param bComputePNNormals Compute control points for the normal component for calculating quadratically varying normals.
	 *  @param ProgressCancel Set this to be able to cancel running operation.
	 *  @param OutControlPoints Contains all the control points for the Mesh.
	 * 
	 *  @return true if the operation succeeded, false if it failed or was canceled by the user.
	 */
	bool ComputeControlPoints(FControlPoints& OutControlPoints,
							  const FDynamicMesh3& Mesh, 
							  const FMeshNormals* UseNormals, 
							  const bool bComputePNNormals, 
							  FProgressCancel* ProgressCancel) 
	{
		if (!Mesh.HasAttributes() && !Mesh.HasVertexNormals() && UseNormals == nullptr)
		{
			return false; // Mesh must have normals
		}

		// We know ahead of time the total number of control points:
		// 2 control points per edge and one in the middle of each triangle.
		OutControlPoints.OnEdges.SetNum(Mesh.MaxEdgeID());
		OutControlPoints.OnTriangles.SetNum(Mesh.MaxTriangleID());

		auto ComputeControlPoint = [](const FVector3d& Vertex1, const FVector3d& Vertex2, const FVector3f& Normal)
		{ 
			FVector3d Edge12 = Vertex2 - Vertex1;
			double Weight12 = Edge12.Dot(Normal);
			FVector3d Point = (2.0*Vertex1 + Vertex2 - Weight12*Normal)/3.0;  
			return Point;
		 };

		auto ComputeControlNormal = [](const FVector3d& Vertex1, const FVector3d& Vertex2, const FVector3f& Normal1, const FVector3f& Normal2)
		{ 	
			FVector3d Edge12 = Vertex2 - Vertex1;
			FVector3d Normal21 = Normal2 + Normal1;

			double Divisor = Edge12.Dot(Edge12);
			
			FVector3f Normal;
			if (FMath::IsNearlyZero(Divisor)) 
			{
				Normal = Normal21 / 2.0; // If edge is degenerate then simply interpolate between the normals
			} 
			else
			{ 
				double NWeight12 = 2.0*Edge12.Dot(Normal21)/Divisor;
				Normal = Normalized(Normal21 - NWeight12*Edge12);
			}
			
			return Normal;
		 };

		// First compute only the control points on the edges
		ParallelFor(Mesh.MaxEdgeID(), [&](int32 EID)
		{
			if (ProgressCancel && ProgressCancel->Cancelled()) 
			{
				return;  
			}

			if (Mesh.IsEdge(EID)) 
			{
				FIndex2i EdgeV = Mesh.GetEdgeV(EID);
				
				FVector3d Vertex1 = Mesh.GetVertex(EdgeV.A);
				FVector3d Vertex2 = Mesh.GetVertex(EdgeV.B);

				FIndex2i EdgeTri = Mesh.GetEdgeT(EID);

				FVector3f Normal1, Normal2;
				if (Mesh.HasAttributes()) 
				{
					const FDynamicMeshNormalOverlay* NormalOverlay = Mesh.Attributes()->PrimaryNormals();
					NormalOverlay->GetElementAtVertex(EdgeTri.A, EdgeV.A, Normal1);
					NormalOverlay->GetElementAtVertex(EdgeTri.A, EdgeV.B, Normal2);
				}
				else 
				{
					Normal1 = (UseNormals != nullptr) ? (FVector3f)(*UseNormals)[EdgeV.A] : Mesh.GetVertexNormal(EdgeV.A);
					Normal2 = (UseNormals != nullptr) ? (FVector3f)(*UseNormals)[EdgeV.B] : Mesh.GetVertexNormal(EdgeV.B);
				}

				FEdgeControlPoints& ControlPnts = OutControlPoints.OnEdges[EID];

				if (bComputePNNormals)
				{
					ControlPnts.NormalTriA = ComputeControlNormal(Vertex1, Vertex2, Normal1, Normal2);
				}

				FVector3d Point1ForTriA = ComputeControlPoint(Vertex1, Vertex2, Normal1);
				FVector3d Point2ForTriA = ComputeControlPoint(Vertex2, Vertex1, Normal2);

				if (Mesh.HasAttributes()) 
				{
					const FDynamicMeshNormalOverlay* NormalOverlay = Mesh.Attributes()->PrimaryNormals();
					if (EdgeTri.B != FDynamicMesh3::InvalidID && NormalOverlay->IsSeamEdge(EID)) 
					{	
						NormalOverlay->GetElementAtVertex(EdgeTri.B, EdgeV.A, Normal1);
						NormalOverlay->GetElementAtVertex(EdgeTri.B, EdgeV.B, Normal2); 
						FVector3d Point1ForTriB = ComputeControlPoint(Vertex1, Vertex2, Normal1);
						FVector3d Point2ForTriB = ComputeControlPoint(Vertex2, Vertex1, Normal2);

						// Following the core idea of Crack-free PN Triangles we average the control point we'd like 
						// with the control point our neighbor triangle would like. 
						Point1ForTriA = (Point1ForTriA + Point1ForTriB) / 2.0;
						Point2ForTriA = (Point2ForTriA + Point2ForTriB) / 2.0;

						if (bComputePNNormals)
						{
							ControlPnts.NormalTriB = ComputeControlNormal(Vertex1, Vertex2, Normal1, Normal2);
						}
					}
				}

				ControlPnts.Point1 = Point1ForTriA;
				ControlPnts.Point2 = Point2ForTriA;
			}
		});

		if (ProgressCancel && ProgressCancel->Cancelled())         
		{
			return false; 
		}

		// Compute the center control point for each triangle
		ParallelFor(Mesh.MaxTriangleID(), [&](int32 TID)
		{
			if (ProgressCancel && ProgressCancel->Cancelled()) 
			{
				return;  
			}

			if (Mesh.IsTriangle(TID)) 
			{
				const FIndex3i& TriEdgeID = Mesh.GetTriEdgesRef(TID);
				FVector3d ControlMidpoint = FVector3d::Zero();
				for (int EIdx = 0; EIdx < 3; ++EIdx) 
				{
					const FEdgeControlPoints& ControlPnts = OutControlPoints.OnEdges[TriEdgeID[EIdx]];
					ControlMidpoint += ControlPnts.Point1 + ControlPnts.Point2;
				}
				ControlMidpoint /= 6.0;

				const FIndex3i& TriVertID = Mesh.GetTriangleRef(TID);
				FVector3d VertexMidpoint = (Mesh.GetVertex(TriVertID.A) + 
											Mesh.GetVertex(TriVertID.B) + 
											Mesh.GetVertex(TriVertID.C))/3.0;

				OutControlPoints.OnTriangles[TID] = ControlMidpoint + (ControlMidpoint - VertexMidpoint)/2.0;	
			}
		});

		if (ProgressCancel && ProgressCancel->Cancelled())         
		{
			return false; 
		}

		return true;
	}

	/** 
	 * Tesselate the mesh by recursively subdividing it using loop-style subdivision. Keep track of the new vertices 
	 * added and which original triangles (before tesselation) they belong to. New vertices on original non-boundary 
	 * edges can belong to either of the original triangles that share the edge.
	 * 
	 * @param Mesh The mesh we are tesselating.
	 * @param Level How many times we are recursively subdividing the Mesh.
	 * @param ProgressCancel Set this to be able to cancel running operation.
	 * @param OutNewVertices Array of tuples of the new vertex ID and the original triangle ID the vertex belongs to.
	 * 
	 * @return true if the operation succeeded, false if it failed or was canceled by the user.
	 */
	bool TesselateMesh(FDynamicMesh3& Mesh, 
					   TArray<FIndex2i>& OutNewVertices,
					   const int Level, 
					   FProgressCancel* ProgressCancel) 
	{
		check(Level >= 0);

		if (Level < 0) 
		{
			return false;
		}
		else if (Level == 0) 
		{
			return true; // nothing to do
		}

		// Compute the number of new vertices we will generate with tesselation
		int NumEdgeSegments = 1 << Level; // Number of the edges each original edge is split into
		int NumEdgeVert = NumEdgeSegments + 1; // Number of the vertices along the edge after the tesselation
		int NumNewEdgeVert = NumEdgeSegments - 1; // Number of the new vertices added per edge
		// Triangular number series (n(n+1)/2) minus the 3 original vertices and new edge vertices
		int NumNewTriangleVert = NumEdgeVert*(NumEdgeVert + 1)/2 - 3 - 3*NumNewEdgeVert;
		int NumNewVertices = NumNewEdgeVert * Mesh.EdgeCount() + NumNewTriangleVert* Mesh.TriangleCount(); 
		OutNewVertices.Reserve(NumNewVertices);

		// The number of the new triangle IDs created with Level number of subdivisions 
		int NumNewTrianglesIDs = (NumEdgeSegments * NumEdgeSegments - 1) * Mesh.TriangleCount();
		
		// When we perform edge splits, we want to keep track of the original triangles that 
		// the new added triangles belong to (i.e parent triangle). 
		TArray<int> ParentTriangles;
		ParentTriangles.Init(FDynamicMesh3::InvalidID, Mesh.MaxTriangleID() + NumNewTrianglesIDs);
		for (int TID : Mesh.TriangleIndicesItr()) 
		{ 
			ParentTriangles[TID] = TID; // Original triangles are parents of themselves.
		}

		// Recursively subdivide the mesh Level number of times, while keeping track of new 
		// vertices added and which original triangles they belong to.
		for (int CurLevel = 0; CurLevel < Level; ++CurLevel) 
		{
			TArray<int> EdgesToProcess;
			EdgesToProcess.Reserve(Mesh.EdgeCount());
			for (int EID : Mesh.EdgeIndicesItr())
			{
				EdgesToProcess.Add(EID);
			} 

			int MaxTriangleID = Mesh.MaxTriangleID();

			TArray<int> EdgesToFlip;
			EdgesToFlip.Init(FDynamicMesh3::InvalidID, MaxTriangleID);

			for (int EID : EdgesToProcess)
			{
				FIndex2i EdgeTris = Mesh.GetEdgeT(EID);

				FDynamicMesh3::FEdgeSplitInfo SplitInfo;
				EMeshResult Result = Mesh.SplitEdge(EID, SplitInfo);

				if (Result == EMeshResult::Ok) 
				{	  
					ParentTriangles[SplitInfo.NewTriangles.A] = ParentTriangles[SplitInfo.OriginalTriangles.A]; 
					if (SplitInfo.OriginalTriangles.B != FDynamicMesh3::InvalidID) 
					{
						ParentTriangles[SplitInfo.NewTriangles.B] = ParentTriangles[SplitInfo.OriginalTriangles.B]; 
					}

					OutNewVertices.Add(FIndex2i(SplitInfo.NewVertex, ParentTriangles[SplitInfo.OriginalTriangles.A]));

					if (EdgeTris.A < MaxTriangleID && EdgesToFlip[EdgeTris.A] == FDynamicMesh3::InvalidID)
					{
						EdgesToFlip[EdgeTris.A] = SplitInfo.NewEdges.B;
					}
					if (EdgeTris.B != FDynamicMesh3::InvalidID)
					{
						if (EdgeTris.B < MaxTriangleID && EdgesToFlip[EdgeTris.B] == FDynamicMesh3::InvalidID)
						{
							EdgesToFlip[EdgeTris.B] = SplitInfo.NewEdges.C;
						}
					}
				}   

			  	if (ProgressCancel && ProgressCancel->Cancelled())    
				{
					return false;
				}
			}

			for (int EID : EdgesToFlip)
			{
				if (EID != FDynamicMesh3::InvalidID)
				{
					FDynamicMesh3::FEdgeFlipInfo FlipInfo;
					Mesh.FlipEdge(EID, FlipInfo);

					if (ProgressCancel && ProgressCancel->Cancelled()) 
					{
						return false;
					}
				}
			}
		}

		checkSlow(OutNewVertices.Num() == NumNewVertices);
		checkSlow(ParentTriangles.Num() == Mesh.MaxTriangleID());

		return true;
	}

	/** 
	 * Displace the vertices created from the tesselation using the cubic patch formula based on their barycentric 
	 * coordinates.
	 * 
	 * @param Mesh The tessellated mesh whose vertices we are displacing.
	 * @param FControlPoints PN Triangle control points.
	 * @param VerticesToDisplace Array of vertices into the Mesh that we are displacing.
	 * @param bComputePNNormals Should we be computing quadratically varying normals using control points.
	 * @param OriginalConnectivity Connectivity of the original mesh (before tesselation).
	 * @param ProgressCancel Set this to be able to cancel running operation.
	 * 
	 * @return true if the operation succeeded, false if it failed or was canceled by the user.
	 */

	bool DisplaceAndSetNormals(FDynamicMesh3& Mesh,
					           const FControlPoints& FControlPoints,
					           const TArray<FIndex2i>& VerticesToDisplace,
					           const bool bComputePNNormals,
					           const FMeshConnectivity& OriginalConnectivity,
					           FProgressCancel* ProgressCancel) 
	{
		bool bHasVertexNormals = Mesh.HasVertexNormals();
		bool bHasAttributes = Mesh.HasAttributes();
		
		// Iterate over every new vertex and compute its displacement and optionally a normal
		ParallelFor(VerticesToDisplace.Num(), [&](int32 IDX)
		{	
			if (ProgressCancel && ProgressCancel->Cancelled()) 
			{
				return; 
			}
			
			FIndex2i NewVtx = VerticesToDisplace[IDX];
			int VertexID = NewVtx[0]; // ID of the new vertex added with tesselation
			int OriginalTriangleID = NewVtx[1]; // ID of the original triangle new vertex belongs to
			
			// Get the topology information of the original triangle
			FIndex3i TriVertex = OriginalConnectivity.Triangles[OriginalTriangleID];
			FIndex3i TriEdges = OriginalConnectivity.TriangleEdges[OriginalTriangleID];

			FVector3d Bary = VectorUtil::BarycentricCoords(Mesh.GetVertexRef(VertexID), Mesh.GetVertexRef(TriVertex.A), 
														   Mesh.GetVertexRef(TriVertex.B), Mesh.GetVertexRef(TriVertex.C));
														   
			FVector3d BarySquared = Bary*Bary;

			// Displaced vertex. First compute contribution of the original control points at the original vertices
			FVector3d NewPos = Bary[0]*BarySquared[0]*Mesh.GetVertexRef(TriVertex.A) + 
							   Bary[1]*BarySquared[1]*Mesh.GetVertexRef(TriVertex.B) + 
							   Bary[2]*BarySquared[2]*Mesh.GetVertexRef(TriVertex.C);
			
			// Compute contribution of the control points at the edges
			for (int EIDX = 0; EIDX < 3; ++EIDX) 
			{   
				int EID = TriEdges[EIDX];
				FIndex2i EdgeV = OriginalConnectivity.Edges[EID].Vert;
				const FEdgeControlPoints& ControlPnts = FControlPoints.OnEdges[EID];

				// Check that the orientation of the edge is consistent so we use the correct barycentric values
				int EdgeVtx1 = EdgeV[0];
				int EdgeVtx2 = EdgeV[1];
				IndexUtil::OrientTriEdgeAndFindOtherVtx(EdgeVtx1, EdgeVtx2, TriVertex);
				
				int BaryIdx1 = EdgeV[0] == EdgeVtx1 ? EIDX : (EIDX + 1) % 3;
				int BaryIdx2 = EdgeV[0] == EdgeVtx1 ? (EIDX + 1) % 3 : EIDX;
				
				NewPos += (3.0*BarySquared[BaryIdx1]*Bary[BaryIdx2])*ControlPnts.Point1 + 
						  (3.0*BarySquared[BaryIdx2]*Bary[BaryIdx1])*ControlPnts.Point2;
			}

			// Finally compute contribution of the single control point inside the triangle
			NewPos += (6.0*Bary[0]*Bary[1]*Bary[2])*FControlPoints.OnTriangles[OriginalTriangleID];
			
			Mesh.SetVertex(VertexID, NewPos); 

			if (bComputePNNormals) 
			{
				if (bHasVertexNormals) 
				{
					// Compute contribution of the normal control points at the original vertices
					FVector3d NewNormal = BarySquared[0]*Mesh.GetVertexNormal(TriVertex.A) + 
										  BarySquared[1]*Mesh.GetVertexNormal(TriVertex.B) +
										  BarySquared[2]*Mesh.GetVertexNormal(TriVertex.C);
					
					// Compute contribution of the normal control points at the edges
					for (int EIDX = 0; EIDX < 3; ++EIDX) 
					{   
						int EID = TriEdges[EIDX];
						const FEdgeControlPoints& ControlPnts = FControlPoints.OnEdges[EID];
						NewNormal += Bary[EIDX]*Bary[(EIDX + 1) % 3]*ControlPnts.NormalTriA;
					}

					Normalize(NewNormal);

					Mesh.SetVertexNormal(VertexID, NewNormal); 
				} 
				
				if (bHasAttributes) 
				{
					//TODO: Compute per-element quadractic PN normals
				}
			}
		});

		if (ProgressCancel && ProgressCancel->Cancelled()) 
		{
			return false; 
		}
		
		return true;
	}
}

bool FPNTriangles::Compute()
{
	using namespace FPNTrianglesLocals;

	check(TesselationLevel >= 0);
	check(Mesh != nullptr);

	if (TesselationLevel < 0 || Mesh == nullptr) 
	{
		return false;
	}

	if (TesselationLevel == 0) 
	{
		return true; // nothing to do
	}

	// Compute per-vertex normals if no normals exist
	FMeshNormals Normals;
	bool bHasNormals = Mesh->HasVertexNormals() || Mesh->HasAttributes();
	if (!bHasNormals)
	{
		Normals = FMeshNormals(Mesh);
		Normals.ComputeVertexNormals();
	}
	FMeshNormals* UseNormals = (bHasNormals) ? nullptr : &Normals;
	
	// Compute PN triangle control points for each flat triangle of the original mesh
	FControlPoints FControlPoints;
	bool bOk = ComputeControlPoints(FControlPoints, *Mesh, UseNormals, bComputePNNormals, Progress);
	if (bOk == false) 
	{
		return false;
	}

	// Save the topology information of the original mesh before we change it with tesselation
	FMeshConnectivity OriginalConnectivity(*Mesh);
	
	// Tesselate the original mesh
	TArray<FIndex2i> NewVertices;
	bOk = TesselateMesh(*Mesh, NewVertices, TesselationLevel, Progress);
	if (bOk == false) 
	{
		return false;
	}

	// Compute displacement and normals
	bOk = DisplaceAndSetNormals(*Mesh, FControlPoints, NewVertices, bComputePNNormals, OriginalConnectivity, Progress);
	if (bOk == false) 
	{
		return false;
	}

	return true;
}
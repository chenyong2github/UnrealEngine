// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

// Port of geometry3Sharp Arrangement2d

#include "Arrangement2d.h"
#include "ThirdParty/GTEngine/Mathematics/GteConstrainedDelaunay2.h"
#include "ThirdParty/GTEngine/Mathematics/GteBSNumber.h"
#include "ThirdParty/GTEngine/Mathematics/GteUIntegerFP32.h"
#include <vector>



namespace
{
//#define DEBUG_FILE_DUMPING 1
#ifndef DEBUG_FILE_DUMPING
	void DumpGraphForDebug(const FDynamicGraph2d& Graph, const FString& PathBase)
	{
	}
	void DumpGraphForDebugAsOBJ(const FDynamicGraph2d& Graph, const FString& PathBase)
	{
	}
	void DumpTriangulationForDebug(const FDynamicGraph2d& Graph, const TArray<FIntVector>& Triangles, const FString& PathBase)
	{
	}
#else
#include <fstream>
	static int num = 0;
	void DumpGraphForDebug(const FDynamicGraph2d& Graph, const FString& PathBase)
	{
		num++;
		FString Path = PathBase + FString::FromInt(num) + ".txt";
		std::ofstream f(*Path);

		for (int32 VertexIdx = 0; VertexIdx < Graph.MaxVertexID(); VertexIdx++)
		{
			const FVector2d& Vertex = Graph.GetVertex(VertexIdx);
			f << "v " << Vertex.X << " " << Vertex.Y << " " << std::endl;
		}
		for (const FDynamicGraph::FEdge& Edge : Graph.Edges())
		{
			f << "e " << Edge.A << " " << Edge.B << std::endl;
		}
		f.close();
	}
	void DumpGraphForDebugAsOBJ(const FDynamicGraph2d& Graph, const FString& PathBase)
	{
		num++;
		FString Path = PathBase + FString::FromInt(num) + ".obj";
		std::ofstream f(*Path);

		for (int32 VertexIdx = 0; VertexIdx < Graph.MaxVertexID(); VertexIdx++)
		{
			const FVector2d& Vertex = Graph.GetVertex(VertexIdx);
			f << "v " << Vertex.X << " " << Vertex.Y << " 0" << std::endl;
		}
		for (int32 VertexIdx = 0; VertexIdx < Graph.MaxVertexID(); VertexIdx++)
		{
			const FVector2d& Vertex = Graph.GetVertex(VertexIdx);
			f << "v " << Vertex.X << " " << Vertex.Y << " .5" << std::endl;
		}
		for (const FDynamicGraph::FEdge& Edge : Graph.Edges())
		{
			f << "f " << Edge.A + 1 << " " << Edge.B + 1 << " " << 1 + Edge.A + Graph.MaxVertexID() << std::endl;
		}
		f.close();
	}
	void DumpTriangulationForDebug(const FDynamicGraph2d& Graph, const TArray<FIntVector>& Triangles, const FString& PathBase)
	{
		num++;
		FString Path = PathBase + FString::FromInt(num) + ".obj";
		std::ofstream f(*Path);
		for (int32 VertexIdx = 0; VertexIdx < Graph.MaxVertexID(); VertexIdx++)
		{
			const FVector2d& Vertex = Graph.GetVertex(VertexIdx);
			f << "v " << Vertex.X << " " << Vertex.Y << " 0" << std::endl;
		}
		for (const FIntVector& Tri : Triangles)
		{
			f << "f " << 1 + Tri.X << " " << 1 + Tri.Y << " " << 1 + Tri.Z << std::endl;
		}
		f.close();
	}
#endif
}

bool FArrangement2d::AttemptTriangulate(TArray<FIntVector> &Triangles, TArray<int32> &SkippedEdges, int32 BoundaryEdgeGroupID)
{
	Triangles.Empty();

	// Use this declaration for the non-robust version of the algorithm; this is very non-robust, and not recommended!
	//gte::ConstrainedDelaunay2<double, double> Delaunay;

	// Here are the types you could use for robust math (in theory makes the algorithm overall robust; in practice I am not sure)
	gte::ConstrainedDelaunay2<double, gte::BSNumber<gte::UIntegerFP32<263>>> Delaunay; // Value of 263 is from comment in GTEngine/Mathematics/GteDelaunay2.h
	//gte::ConstrainedDelaunay2<double, gte::BSNumber<gte::UIntegerAP32>> Delaunay; // Full arbitrary precision (slowest method, creates a std::vector per number)
	
	std::vector<gte::Vector2<double>> InputVertices;

	// If there are unused vertices, define a vertex index remapping so the delaunay code won't have to understand that
	bool bNeedsRemap = Graph.MaxVertexID() != Graph.VertexCount();
	TArray<int> InputIndices, OutputIndices;
	if (bNeedsRemap)
	{
		InputIndices.SetNumZeroed(Graph.MaxVertexID());
		OutputIndices.SetNumZeroed(Graph.VertexCount());
		int TotalOffset = 0;
		for (int i = 0; i < Graph.MaxVertexID(); i++)
		{
			if (Graph.IsVertex(i))
			{
				OutputIndices[i - TotalOffset] = i;
				InputIndices[i] = i - TotalOffset;
				FVector2d Vertex = Graph.GetVertex(i);
				InputVertices.push_back(gte::Vector2<double> {{Vertex.X, Vertex.Y}});
			}
			else
			{
				InputIndices[i] = -1;
				TotalOffset++;
			}
		}
	}
	else
	{	// no index remapping needed; just copy the vertices
		for (int i = 0; i < Graph.MaxVertexID(); i++)
		{
			FVector2d Vertex = Graph.GetVertex(i);
			InputVertices.push_back(gte::Vector2<double> {{Vertex.X, Vertex.Y}});
		}
	}
	if (!Delaunay(Graph.VertexCount(), &InputVertices[0], 0.001f))
	{
		return false;
	}
	std::vector<int> OutEdges;
	bool InsertConstraintFailure = false;
	TSet<TPair<int, int>> BoundarySet; // tracks all the boundary edges as they are added, so we can later eat what's outside them
	bool bBoundaryTrackingFailure = false;

	for (int EdgeIdx : Graph.EdgeIndices())
	{
		FDynamicGraph::FEdge Edge = Graph.GetEdge(EdgeIdx);
		if (bNeedsRemap)
		{
			Edge.A = InputIndices[Edge.A];
			Edge.B = InputIndices[Edge.B];
		}
		if (!Delaunay.Insert({{Edge.A, Edge.B}}, OutEdges))
		{
			// Note the failed edge; we will try to proceed anyway, just without this edge.  Hopefully the CDT is robust and this never happens!
			ensureMsgf(false, TEXT("CDT edge insertion failed"));
			InsertConstraintFailure = true;
			SkippedEdges.Add(EdgeIdx);
			// specially note if we failed to add a boundary edge; in this case removing the outside-boundary triangles would likely delete all triangles from the output
			// in this case I choose (below) to just add all triangles to the output, ignoring boundaries, as this may create less noticeable artifacts in the typical cases
			if (Edge.Group == BoundaryEdgeGroupID)
			{
				bBoundaryTrackingFailure = true;
			}
		}
		else
		{
			if (Edge.Group == BoundaryEdgeGroupID)
			{
				for (size_t i = 0; i + 1 < OutEdges.size(); i++)
				{
					int MinV = FMath::Min(OutEdges[i], OutEdges[i + 1]);
					int MaxV = FMath::Max(OutEdges[i], OutEdges[i + 1]);
					BoundarySet.Add(TPair<int, int>(MinV, MaxV));
				}
			}
		}
	}
	const std::vector<int>& Indices = Delaunay.GetIndices();
	if (!bBoundaryTrackingFailure && BoundarySet.Num() > 0) {
		auto IsBoundaryEdge = [&BoundarySet](int V0, int V1)
		{
			int MinV = FMath::Min(V0, V1);
			int MaxV = FMath::Max(V0, V1);
			return BoundarySet.Contains(TPair<int, int>(MinV, MaxV));
		};

		// eat hull to boundary
		const std::vector<int>& Adj = Delaunay.GetAdjacencies();
		int TriNum = int(Adj.size() / 3);
		TArray<bool> Eaten;
		Eaten.SetNumZeroed(TriNum);

		TArray<int> ToEatQ;
		// seed the feed queue with any triangles that are on hull but w/ an edge that is not on intended boundary
		for (int TriIdx = 0; TriIdx < TriNum; TriIdx++)
		{
			int BaseIdx = TriIdx * 3;
			bool bEatIt = false;
			for (int SubIdx = 0, NextIdx = 2; !bEatIt && SubIdx < 3; NextIdx = SubIdx++)
			{
				// eat the triangle if it has a hull edge that is not a boundary edge
				bEatIt = Adj[BaseIdx + NextIdx] == -1 && !IsBoundaryEdge(Indices[BaseIdx + SubIdx], Indices[BaseIdx + NextIdx]);
			}
			if (bEatIt)
			{
				ToEatQ.Add(TriIdx);
				Eaten[TriIdx] = true;
			}
		}

		// eat any triangle that we can get to by crossing a non-boundary edge from an already-been-eaten triangle
		while (ToEatQ.Num())
		{
			int EatTri = ToEatQ.Pop();
			int BaseIdx = EatTri * 3;
			for (int SubIdx = 0, NextIdx = 2; SubIdx < 3; NextIdx = SubIdx++)
			{
				int AdjTri = Adj[BaseIdx + NextIdx];
				if (AdjTri != -1 && !Eaten[AdjTri] && !IsBoundaryEdge(Indices[BaseIdx + SubIdx], Indices[BaseIdx + NextIdx]))
				{
					ToEatQ.Add(AdjTri);
					Eaten[AdjTri] = true;
				}
			}
		}

		// copy uneaten triangles
		for (int i = 0; i < TriNum; i++)
		{
			if (!Eaten[i])
			{
				Triangles.Add(FIntVector(Indices[i * 3], Indices[i * 3 + 1], Indices[i * 3 + 2]));
			}
		}
		ensure(Triangles.Num()); // technically it could be valid to not have any triangles after eating the outside-boundary ones, but it would be unusual and could also be a bug
	}
	else
	{
		// copy all triangles over
		for (size_t i = 0, n = Indices.size() / 3; i < n; i++)
		{
			Triangles.Add(FIntVector(Indices[i * 3], Indices[i * 3 + 1], Indices[i * 3 + 2]));
		}
	}
	if (bNeedsRemap)
	{
		for (FIntVector& Face : Triangles)
		{
			Face.X = OutputIndices[Face.X];
			Face.Y = OutputIndices[Face.Y];
			Face.Z = OutputIndices[Face.Z];
		}
	}
	return !InsertConstraintFailure;
}


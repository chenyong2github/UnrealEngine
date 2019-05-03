// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

// Port of geometry3Sharp Arrangement2d

#include "Arrangement2d.h"
#include "ThirdParty/GTEngine/Mathematics/GteConstrainedDelaunay2.h"
#include "ThirdParty/GTEngine/Mathematics/GteBSNumber.h"
#include "ThirdParty/GTEngine/Mathematics/GteUIntegerFP32.h"
#include <vector>

bool FArrangement2d::AttemptTriangulate(TArray<FIntVector> &Triangles, TArray<int32> &SkippedEdges)
{
	Triangles.Empty();

	// non-robust version is very non-robust
	//gte::ConstrainedDelaunay2<double, double> Delaunay;

	// Here are the types you could use for robust math (doesn't make the algorithm 100% robust, but improves it a lot)
	gte::ConstrainedDelaunay2<double, gte::BSNumber<gte::UIntegerFP32<263>>> Delaunay; // Value of 263 is from comment in GTEngine/Mathematics/GteDelaunay2.h
	//gte::ConstrainedDelaunay2<double, gte::BSNumber<gte::UIntegerAP32>> Delaunay; // Full arbitrary precision (slowest method, creates a std::vector per number)
	
	// Copy vertices to a compact buffer, and create a mapping in and out of the compact space (TODO: maybe extract this for general use?  or is it already somewhere else?)
	std::vector<gte::Vector2<double>> InputVertices;
	TArray<int> InputIndices; InputIndices.SetNumZeroed(Graph.MaxVertexID());
	TArray<int> OutputIndices; OutputIndices.SetNumZeroed(Graph.VertexCount());
	int TotalOffset = 0;
	for (int i = 0; i < Graph.MaxVertexID(); i++)
	{
		if (Graph.IsVertex(i))
		{
			OutputIndices[i - TotalOffset] = i;
			InputIndices[i] = i - TotalOffset;
			FVector2d Vertex = Graph.GetVertex(i);
			InputVertices.push_back(gte::Vector2<double> {Vertex.X, Vertex.Y});
		}
		else
		{
			InputIndices[i] = -1;
			TotalOffset++;
		}
	}
	if (!Delaunay(Graph.VertexCount(), &InputVertices[0], 0.001f))
	{
		return false;
	}
	std::vector<int> OutEdges; // (needed for Delaunay.Insert call, but otherwise unused)
	bool InsertConstraintFailure = false;
	for (int EdgeIdx : Graph.EdgeIndices())
	{
		FDynamicGraph::FEdge Edge = Graph.GetEdge(EdgeIdx);
		if (!Delaunay.Insert({ InputIndices[Edge.A], InputIndices[Edge.B] }, OutEdges))
		{
			// Note the failed edge; we will try to proceed anyway, just without this edge
			ensureMsgf(false, TEXT("CDT edge insertion failed -- possibly bad data?"));
			InsertConstraintFailure = true;
			SkippedEdges.Add(EdgeIdx);
		}
	}
	
	std::vector<int> const& Indices = Delaunay.GetIndices();
	for (size_t i = 0, n = Indices.size() / 3; i < n; i++)
	{
		Triangles.Add(FIntVector(OutputIndices[Indices[i * 3]], OutputIndices[Indices[i * 3 + 1]], OutputIndices[Indices[i * 3 + 2]]));
	}
	return !InsertConstraintFailure;
}


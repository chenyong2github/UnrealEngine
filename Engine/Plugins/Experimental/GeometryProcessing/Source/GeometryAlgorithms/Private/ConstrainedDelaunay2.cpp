// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConstrainedDelaunay2.h"
#include "ThirdParty/GTEngine/Mathematics/GteConstrainedDelaunay2.h"
#include "ThirdParty/GTEngine/Mathematics/GteBSNumber.h"
#include "ThirdParty/GTEngine/Mathematics/GteUIntegerFP32.h"
#include <vector>

//namespace
//{
//#define DEBUG_FILE_DUMPING 1
//#ifndef DEBUG_FILE_DUMPING
//	void DumpDelaunayInputForDebugAsOBJ(const FConstrainedDelaunay2d& Delaunay, const FString& PathBase)
//	{
//	}
//	void DumpDelaunayTriangulationForDebug(const FConstrainedDelaunay2d& Delaunay, const FString& PathBase)
//	{
//	}
//#else
//#include <fstream>
//	static int num = 0;
//	template <typename RealType>
//	void DumpDelaunayInputForDebugAsOBJ(const TConstrainedDelaunay2<RealType>& Delaunay, const FString& PathBase)
//	//void DumpGraphForDebugAsOBJ(const FDynamicGraph2d& Graph, const FString& PathBase)
//	{
//		num++;
//		FString Path = PathBase + FString::FromInt(num) + ".obj";
//		std::ofstream f(*Path);
//
//		for (int32 VertexIdx = 0; VertexIdx < Delaunay.Vertices.Num(); VertexIdx++)
//		{
//			const FVector2<RealType>& Vertex = Delaunay.Vertices[VertexIdx];
//			f << "v " << Vertex.X << " " << Vertex.Y << " 0" << std::endl;
//		}
//		for (int32 VertexIdx = 0; VertexIdx < Delaunay.Vertices.Num(); VertexIdx++)
//		{
//			const FVector2<RealType>& Vertex = Delaunay.Vertices[VertexIdx];
//			f << "v " << Vertex.X << " " << Vertex.Y << " .5" << std::endl;
//		}
//		for (const FIndex2i& Edge : Delaunay.Edges)
//		{
//			f << "f " << Edge.A + 1 << " " << Edge.B + 1 << " " << 1 + Edge.A + Delaunay.Vertices.Num() << std::endl;
//		}
//		f.close();
//	}
//	//void DumpTriangulationForDebug(const FDynamicGraph2d& Graph, const TArray<FIntVector>& Triangles, const FString& PathBase)
//	template <typename RealType>
//	void DumpDelaunayTriangulationForDebug(const TConstrainedDelaunay2<RealType>& Delaunay, const FString& PathBase)
//	{
//		num++;
//		FString Path = PathBase + FString::FromInt(num) + ".obj";
//		std::ofstream f(*Path);
//		for (int32 VertexIdx = 0; VertexIdx < Delaunay.Vertices.Num(); VertexIdx++)
//		{
//			const FVector2<RealType>& Vertex = Delaunay.Vertices[VertexIdx];
//			f << "v " << Vertex.X << " " << Vertex.Y << " 0" << std::endl;
//		}
//		for (const FIndex3i& Tri : Delaunay.Triangles)
//		{
//			f << "f " << 1 + Tri.A << " " << 1 + Tri.B << " " << 1 + Tri.C << std::endl;
//		}
//		f.close();
//	}
//#endif
//}

template<class RealType>
template<class InputRealType>
void TConstrainedDelaunay2<RealType>::Add(const FDynamicGraph2<InputRealType>& Graph)
{
	// TODO: implement
	check(false);
}

template<class RealType>
template<class InputRealType>
void TConstrainedDelaunay2<RealType>::Add(const TPolygon2<InputRealType>& Polygon, bool bIsHole)
{
	int32 VertexStart = Vertices.Num();
	int32 VertexEnd = VertexStart + Polygon.VertexCount();
	for (const FVector2<InputRealType> &Vertex : Polygon.GetVertices())
	{
		Vertices.Add((FVector2<RealType>)Vertex);
	}

	TArray<FIndex2i>* EdgeArr;
	if (bIsHole)
	{
		EdgeArr = &HoleEdges;
	}
	else
	{
		EdgeArr = &Edges;
	}
	for (int32 A = VertexEnd - 1, B = VertexStart; B < VertexEnd; A = B++)
	{
		EdgeArr->Add(FIndex2i(A, B));
	}
}

template<class RealType>
template<class InputRealType>
void TConstrainedDelaunay2<RealType>::Add(const TGeneralPolygon2<InputRealType>& GPolygon)
{
	Add(GPolygon.GetOuter(), false);
	const TArray<TPolygon2<InputRealType>>& Holes = GPolygon.GetHoles();
	for (int HoleIdx = 0, HolesNum = Holes.Num(); HoleIdx < HolesNum; HoleIdx++)
	{
		Add(Holes[HoleIdx], true);
	}
}

void AddOrderedEdge(TMap<TPair<int, int>, bool>& EdgeMap, int VertA, int VertB)
{
	bool bReversed = VertA > VertB;
	if (bReversed)
	{
		Swap(VertA, VertB);
	}
	EdgeMap.Add(TPair<int, int>(VertA, VertB), bReversed);
}

/**
 * Compute the change in winding number from crossing an oriented edge connecting VertA to VertB
 *
 * @param EdgeMap Map of known edges & orientations
 * @param VertA First vertex on edge
 * @param VertB Second vertex on edge
 * @return  -1 if reverse edge (B-A) found, 1 if forward edge (A-B) found, 0 otherwise
 */
int WindingAcross(const TMap<TPair<int, int>, bool>& EdgeMap, int VertA, int VertB)
{
	bool bReversed = VertA > VertB;
	if (bReversed)
	{
		Swap(VertA, VertB);
	}
	TPair<int, int> EdgeKey(VertA, VertB);
	const bool *bFoundReversed = EdgeMap.Find(EdgeKey);
	if (!bFoundReversed)
	{
		return 0;
	}
	bool bSameDir = bReversed == *bFoundReversed;
	return bSameDir ? 1 : -1;
}

/**
 * Check if any edge in edge map connects VertA to VertB (or VertB to VertA)
 * 
 * @param EdgeMap Map of known edges & orientations
 * @param VertA First vertex on edge
 * @param VertB Second vertex on edge
 * @return  true if VertA and VertB are connected by an edge (in either direction)
 */
bool HasUnorderedEdge(const TMap<TPair<int, int>, bool>& EdgeMap, int VertA, int VertB)
{
	return EdgeMap.Contains(TPair<int, int>(FMath::Min(VertA, VertB), FMath::Max(VertA, VertB)));
}

template<class RealType>
bool TConstrainedDelaunay2<RealType>::Triangulate()
{
	Triangles.Empty();

	check(FillRule <= EFillRule::Odd || bOrientedEdges);

	gte::ConstrainedDelaunay2<double, gte::BSNumber<gte::UIntegerFP32<263>>> Delaunay; // Value of 263 is from comment in GTEngine/Mathematics/GteDelaunay2.h

	std::vector<gte::Vector2<double>> InputVertices;
	for (int i = 0; i < Vertices.Num(); i++)
	{
		InputVertices.push_back(gte::Vector2<double> {{Vertices[i].X, Vertices[i].Y}});
	}

	if (!Delaunay(Vertices.Num(), &InputVertices[0], 0))
	{
		return false;
	}

	std::vector<int> OutEdges;
	bool InsertConstraintFailure = false;
	TMap<TPair<int, int>, bool> BoundaryMap, HoleMap; // tracks all the boundary edges as they are added, so we can later flood fill across them for inside/outside decisions
	TMap<TPair<int, int>, bool>* EdgeAndHoleMaps[2] = { &BoundaryMap, &HoleMap };
	bool bBoundaryTrackingFailure = false;

	TArray<FIndex2i>* InputEdgesAndHoles[2] = { &Edges, &HoleEdges };
	for (int EdgeOrHole = 0; EdgeOrHole < 2; EdgeOrHole++)
	{
		TArray<FIndex2i>& Input = *InputEdgesAndHoles[EdgeOrHole];
		TMap<TPair<int, int>, bool>& InputMap = *EdgeAndHoleMaps[EdgeOrHole];
		for (const FIndex2i& Edge : Input)
		{
			if (!Delaunay.Insert({{Edge.A, Edge.B}}, OutEdges))
			{
				// Note the failed edge; we will try to proceed anyway, just without this edge.  Hopefully the CDT is robust and this never happens!
				ensureMsgf(false, TEXT("CDT edge insertion failed"));
				InsertConstraintFailure = true;
				bBoundaryTrackingFailure = true;
			}
			else
			{
				for (size_t i = 0; i + 1 < OutEdges.size(); i++)
				{
					AddOrderedEdge(InputMap, OutEdges[i], OutEdges[i + 1]);
				}
			}
		}
	}

	
	const std::vector<int>& Indices = Delaunay.GetIndices();
	
	const std::vector<int>& Adj = Delaunay.GetAdjacencies();
	int TriNum = int(Adj.size() / 3);
	TArray<int8> Keep;  // values: 0->unprocessed (delete), 1->yes keep, -1->processed, delete
	Keep.SetNumZeroed(TriNum);

	TArray<TPair<int, int>> ToWalkQ; // Pair of tri index, winding number
	// seed the queue with all triangles that are on the boundary of the convex hull
	// note: need *all* not just *one* because of the strategy of refusing to cross hole edges; if using pure winding number classification would just need one boundary triangle to start
	for (int TriIdx = 0; TriIdx < TriNum; TriIdx++)
	{
		int BaseIdx = TriIdx * 3;
		for (int SubIdx = 0, NextIdx = 2; SubIdx < 3; NextIdx = SubIdx++)
		{
			if (Adj[BaseIdx + NextIdx] < 0) // on hull
			{
				int VertA = Indices[BaseIdx + SubIdx], VertB = Indices[BaseIdx + NextIdx];
				if (HasUnorderedEdge(HoleMap, VertA, VertB))
				{
					continue; // cannot cross hole edges
				}
				// note we negate the winding across for these hull triangles because we're not actually crossing the edge; we're already on the 'inside' of the hull edge
				int Winding = -WindingAcross(BoundaryMap, VertA, VertB);
				ToWalkQ.Add(TPair<int, int>(TriIdx, Winding));
				Keep[TriIdx] = ClassifyFromRule(Winding) ? 1 : -1;
				break; // don't check any more edges once in queue
			}
		}
	}

	int SelIdx = 0; // Index of item to Pop next; used to make the traversal less depth-first in shape, so a little more robust to bad data
	while (ToWalkQ.Num())
	{
		SelIdx = (SelIdx + 1) % ToWalkQ.Num();
		TPair<int, int> TriWithWinding = ToWalkQ[SelIdx];
		ToWalkQ.RemoveAtSwap(SelIdx);
		int BaseIdx = TriWithWinding.Key * 3;
		int LastWinding = TriWithWinding.Value;
		for (int SubIdx = 0, NextIdx = 2; SubIdx < 3; NextIdx = SubIdx++)
		{
			int VertA = Indices[BaseIdx + SubIdx], VertB = Indices[BaseIdx + NextIdx];
			if (HasUnorderedEdge(HoleMap, VertA, VertB))
			{
				continue; // cannot cross hole edges
			}
			int AdjTri = Adj[BaseIdx + NextIdx];
			if (AdjTri >= 0 && Keep[AdjTri] == 0)
			{
				int WindingChange = WindingAcross(BoundaryMap, VertA, VertB);
				int Winding = LastWinding + WindingChange;
				ToWalkQ.Add(TPair<int, int>(AdjTri, Winding));
				Keep[AdjTri] = ClassifyFromRule(Winding) ? 1 : -1;
			}
		}
	}

	for (int i = 0; i < TriNum; i++)
	{
		if (Keep[i] > 0)
		{
			FIndex3i& Tri = Triangles.Emplace_GetRef(Indices[i * 3], Indices[i * 3 + 1], Indices[i * 3 + 2]);
			if (!bOutputCCW)
			{
				Swap(Tri.B, Tri.C);
			}
		}
	}
	
	return !bBoundaryTrackingFailure;
}
//
template<typename RealType>
TArray<FIndex3i> GEOMETRYALGORITHMS_API ConstrainedDelaunayTriangulate(const TGeneralPolygon2<RealType>& GeneralPolygon)
{
	TConstrainedDelaunay2<RealType> Triangulation;
	Triangulation.FillRule = TConstrainedDelaunay2<RealType>::EFillRule::Positive;
	Triangulation.Add(GeneralPolygon);
	Triangulation.Triangulate();
	return Triangulation.Triangles;
}


template TArray<FIndex3i> GEOMETRYALGORITHMS_API ConstrainedDelaunayTriangulate(const TGeneralPolygon2<double>& GeneralPolygon);
template TArray<FIndex3i> GEOMETRYALGORITHMS_API ConstrainedDelaunayTriangulate(const TGeneralPolygon2<float>& GeneralPolygon);

template void TConstrainedDelaunay2<double>::Add(const FDynamicGraph2<double>& Graph);
template void TConstrainedDelaunay2<double>::Add(const TGeneralPolygon2<double>& Polygon);
template void TConstrainedDelaunay2<double>::Add(const TGeneralPolygon2<float>& Polygon);
template void TConstrainedDelaunay2<double>::Add(const TPolygon2<double>& Polygon, bool bIsHole);
template void TConstrainedDelaunay2<double>::Add(const TPolygon2<float>& Polygon, bool bIsHole);

template void TConstrainedDelaunay2<float>::Add(const FDynamicGraph2<double>& Graph);
template void TConstrainedDelaunay2<float>::Add(const TGeneralPolygon2<double>& Polygon);
template void TConstrainedDelaunay2<float>::Add(const TGeneralPolygon2<float>& Polygon);
template void TConstrainedDelaunay2<float>::Add(const TPolygon2<double>& Polygon, bool bIsHole);
template void TConstrainedDelaunay2<float>::Add(const TPolygon2<float>& Polygon, bool bIsHole);

template struct TConstrainedDelaunay2<float>;
template struct TConstrainedDelaunay2<double>;

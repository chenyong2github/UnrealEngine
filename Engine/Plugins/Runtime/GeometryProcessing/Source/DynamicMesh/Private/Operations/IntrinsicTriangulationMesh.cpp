// Copyright Epic Games, Inc. All Rights Reserved.

#include "Operations/IntrinsicTriangulationMesh.h"
#include "Operations/MeshGeodesicSurfaceTracer.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Util/IndexUtil.h"
#include "Algo/Reverse.h"
#include "Containers/BitArray.h"
#include "Containers/Queue.h"
#include "MathUtil.h"
#include "Async/ParallelFor.h"

using namespace UE::Geometry;


/**
 *  Helper functions that use triangle edge lengths instead of vertex positions to do some euclidean triangle-based calculations.
 *  Many of these are based on the "law of cosines" 
 */
namespace
{
	/**
	* Given a flat triangle with edges L0, L1, L2 in CCW order, compute the angle between
	* the L0 and L2 edge in radians [0, Pi] range.
	* 
	* used by Sharp, Soliman and Crane [2019, ACM Transactions on Graphics] section 3.2
	*
	* but can be understood as trivial application of "the law of cosines"
	*/
	double InteriorAngle(const double L0, const double L1, const double L2)
	{
		// V1 = V2 - V0.   L1^2 = ||V1||^2 = ||V2||^2 + ||V0||^2 - 2 Dot(V2, V0)  =  L2^2 + L0^2 - 2 L2 L0 CosTheta
		// CosTheta = (L0^2 + L1^2 - L2^2) / (2 L0 L1)
		double CosTheta = (L0 * L0 + L2 * L2 - L1 * L1) / (2. * L0 * L2);

		// account for roundoff, theoretically this would already be in the [-1,1] range
		CosTheta = FMath::Clamp(CosTheta, -1., 1.);

		return FMath::Acos(CosTheta);
	}

	/**
	* For a triangle, use the law of cos to compute the third edge length
	*  - Given interior angle in radians AngleR, and adjacent edge lengths L0 and L1, this computes the opposing edge length. 
	*/
	double LawOfCosLength(const double AngleR, const double L0, const double L1)
	{
		double L2sqr = L0 * L0 + L1 * L1 - 2.*L0*L1*FMath::Cos(AngleR);
		// theoretically this is always positive, but protect round-off.   Alternately, could compute (L0-L1)^2 + 4 L0 L1 Sin^2 ( AngleR/2)  
		L2sqr = FMath::Max(L2sqr, 0.);
		return FMath::Sqrt(L2sqr);
	}

	/**
	* Compute the unsigned distance between two points, described as barycentric coordinates
	* relative to a triangle with side lengths L0, L1, L2 is CCW order.
	* @params L0, L1, L2 - triangle side lengths in CCW order
	* @param BCPoint0  - Barycentric coordinates for the first point
	* @param BCPoint1  - Barycentric coordinates for the second point
	*
	* NB: it is assumed that the points are within the triangle, and the triangle is valid (i.e. lengths satisfy triangle inequality)
	*/
	double DistanceBetweenBarycentricPoints(const double L0, const double L1, const double L2, const FVector3d& BCPoint0, const FVector3d& BCPoint1)
	{
		// see Sharp, Soliman and Crane [2019, ACM Transactions on Graphics] or  Schindler and Chen [2012, Section 3.2]
		using Vector3Type = FVector3d;
		using ScalarType = double;
		const ScalarType Zero(0);

		const Vector3Type u = BCPoint1 - BCPoint0;
		const ScalarType dsqr = -(L0 * L0 * u[0] * u[1] + L1 * L1 * u[1] * u[2] + L2 * L2 * u[2] * u[0]);
		const ScalarType d = (dsqr < Zero) ? Zero : sqrt(dsqr);
		return d;
	}


	/**
	* Compute a vector (in polar form) from triangle corner to a point within the triangle.
	* @params L0, L1, L2 - triangle side lengths in CCW order
	* @param BarycetricPoint - interior point in triangle
	*
	* @return, a polar vector from the vertex shared by L0, L2 to the BarycentricPoint in the form
	*          Vector[0] = radial distance
	*          Vector[1] = angle measured from the L0 side of the triangle
	*/
	FVector2d VectorToPoint(const double L0, const double L1, const double L2, const FVector3d& BarycentricPoint)
	{
		FVector3d BCpi(1., 0., 0.);
		FVector3d BCpj(0., 1., 0.);
		double rpi = DistanceBetweenBarycentricPoints(L0, L1, L2, BCpi, BarycentricPoint);
		double rpj = DistanceBetweenBarycentricPoints(L0, L1, L2, BCpj, BarycentricPoint);
		double angle = InteriorAngle(L0, rpj, rpi);
		return FVector2d(rpi, angle);
	}

	/**
	* Heron's formula for computing the area of a triangle given the lengths of all three sides.  
	*/
	double TriangleArea(const double L0, const double L1, const double L2)
	{
		// note: since this is a triangle, the lengths should satisfy triangle inequality, meaning this should be positive
		double AreaSqrTimes16 = FMath::Max(0., (L0 + L1 + L2) * (-L0 + L1 + L2 ) * (L0 -L1 + L2) * (L0 + L1 -L2));
		return FMath::Sqrt(AreaSqrTimes16) * 0.25;
	}
	double TriangleArea(const FVector3d& Ls)
	{
		return TriangleArea(Ls[0], Ls[1], Ls[2]);
	}
	/**
	* Compute the cotangent of the interior angle opposite the edge L0, give a triangle with edges {L0, L1, L2} in CCW order
	*/
	double ComputeCotangent(const double L0, const double L1, const double L2)
	{
		const double Area = TriangleArea(L0, L1, L2); 
		const double CT = 0.25 * (-L0 * L0 + L1 * L1 + L2 * L2) / Area;
		return CT;
	}

	/**
	* Return the location of third vertex in a triangle in R2 with prescribed lengths
	* such that the resulting triangle could be constructed as { (0,0),  (L1, 0), Result}.
	* 
	* NB: this assumes, but does not check, that the lengths satisfy the triangle inequality
	*/
	FVector2d ComputeOpposingVert2d(const double L0, const double L1, const double L2)
	{
		const double Area = TriangleArea(L0, L1, L2);
		const double Y = 2. * Area / L0;
		double X = FMath::Sqrt(FMath::Max(0., L2 * L2 - Y * Y));
		FVector2d ResultPos(X, Y);
		// angle between L0 and L2 edges can be computed form 2 * L0 * L2 * Cos(theta) = L0^2 + L2^2 - L1^2
		if (L0 * L0 + L2 * L2 < L1 * L1)
		{
			ResultPos.X = -X; // angle > 90-degrees.
		}
		return ResultPos;
	}

	/**
	* Given three side lengths that satisfy the triangle inequality,
	* this updates the 2d positions to be the vertices of a triangle, 
	* the edge lengths of which ( when in CCW order)  match the prescribed lengths.  
	* with vertices { (0,0),  (L1, 0), (X, Y)} where Y > 0
	*/
	void TriangleFromLengths(const double L0, const double L1, const double L2, FVector2d& p0, FVector2d& p1, FVector2d& p3)
	{
		p0 = FVector2d(0., 0.);
		p1 = FVector2d(L1, 0.);
		p3 = ComputeOpposingVert2d(L0, L1, L2);
	}

	
};


/**
 *  Utilities and generic implementation for flipping a mesh to Delaunay  
 */
namespace
{
	/**
	* LIFO for unique DynamicMesh IDs, can be constructed from any FRefCountVector::IndexEnumerable
	* and supports a filtering function on construction.  If the filtering function is absent, all
	* unique elements will be added, otherwise only those selected by the filter.
	*    
	*	for example: 
	* 
	*	 auto AddToQueueFilter = [](int ID)->bool{...};
	* 
	*    FIndexLIFO EdgeQueueLIFO(Mesh.EdgeIndicesItr(), AddToQueueFilter);
	*/
	class FIndexLIFO
	{
	public:
		// constructor that adds all unique IDs from IndexEnumerable
		FIndexLIFO(FRefCountVector::IndexEnumerable IDEnumerable)
			: IsEnqueued(false, (int32)IDEnumerable.Vector->GetMaxIndex())
		{
			const int32 NumIDs = (int32)IDEnumerable.Vector->GetCount();
			IDs.Reserve(NumIDs);
			for (int32 ID : IDEnumerable)
			{
				Enqueue(ID);
			}
		}
		// filtering constructor that only adds IDs for which Filter(ID) is true
		FIndexLIFO(FRefCountVector::IndexEnumerable IDEnumerable, const TFunctionRef<bool(int32)>& Filter)
			: IsEnqueued(false, (int32)IDEnumerable.Vector->GetMaxIndex())
		{
			const int32 NumIDs = (int32)IDEnumerable.Vector->GetCount();
			IDs.Reserve(NumIDs);
			for (int32 ID : IDEnumerable)
			{
				if (Filter(ID))
				{
					Enqueue(ID);
				}
			}
		}
		bool Dequeue(int32& IDOut)
		{
			if (IDs.Num())
			{
				IDOut = IDs.Pop(false);
				IsEnqueued[IDOut] = false;
				return true;
			}
			IDOut = -1;
			return false;
		}
		void Enqueue(int32 ID)
		{
			if (!IsEnqueued[ID])
			{
				IDs.Add(ID);
				IsEnqueued[ID] = true;
			}
		}
	private:
		FIndexLIFO();
		TBitArray<FDefaultBitArrayAllocator> IsEnqueued;
		TArray<int32> IDs;
	};

	/**
	* FIFO for unique Dynamic Mesh IDs - can be constructed from any FRefCountVector::IndexEnumerable
	* and supports a filtering function on construction.  If the filtering function is absent, all
	* unique elements will be added, otherwise only those selected by the filter.
	*
	*	for example:
	*
	*	 auto AddToQueueFilter = [](int ID)->bool{...};
	*
	*    FIndexFIFO EdgeQueueFIFO(Mesh.EdgeIndicesItr(), AddToQueueFilter);
	*/
	class FIndexFIFO
	{
	public:
		// constructor that adds all unique IDs from IndexEnumerable
		FIndexFIFO(FRefCountVector::IndexEnumerable IDEnumerable)
			: IsEnqueued(false, (int32)IDEnumerable.Vector->GetMaxIndex())
		{
			const int32 NumEdges = (int32)IDEnumerable.Vector->GetMaxIndex();
			for (int32 ID : IDEnumerable)
			{
				Enqueue(ID);
			}
		}

		// filtering constructor that only adds IDs for which Filter(ID) is true  Note: Filter is called in parallel.
		FIndexFIFO(FRefCountVector::IndexEnumerable IDEnumerable, const TFunctionRef<bool(int32)>& Filter)
			: IsEnqueued(false, (int32)IDEnumerable.Vector->GetMaxIndex())
		{
			// parallel evaluation that assumes Filter is expensive
			
			const int32 MaxID = (int32)IDEnumerable.Vector->GetMaxIndex();
			TArray<int32> ToInclude;
			ToInclude.AddZeroed(MaxID);
			ParallelFor(MaxID, [&ToInclude, &Filter](int32 ID)
			{
				if (Filter(ID))
				{
					ToInclude[ID] = 1;
				}
			});
			for (int32 ID = 0; ID < MaxID; ++ID)
			{
				if (ToInclude[ID] == 1)
				{
					Enqueue(ID);
				}
			}
		}

		bool Dequeue(int32& IDOut)
		{
			if (IDs.Dequeue(IDOut))
			{
				IsEnqueued[IDOut] = false;
				return true;
			}
			IDOut = -1;
			return false;
		}
		void Enqueue(int32 ID)
		{
			if (!IsEnqueued[ID])
			{
				IDs.Enqueue(ID);
				IsEnqueued[ID] = true;
			}
		}
	private:
		FIndexFIFO();
		TBitArray<FDefaultBitArrayAllocator> IsEnqueued;
		TQueue<int32> IDs;
	};


	/**
	* Carry out edge flips on intrinsic mesh until either the MaxFlipCount is reached or the mesh is fully Delaunay and returns
	* the number of flips.
	* Note: There can be cases where an edge can not flip (e.g. if the resulting edge already exists)
	*       - for this reason, there may be some "uncorrected" edges in the resulting mesh.
	* 
	* @param IntrinsicMesh - The mesh to be operated on.
	* @param Uncorrected   - contains on return, all the edges that could not be flipped.
	* @param Threshold     - edges with cotan weight less than threshold should be flipped.
	*/
	template <typename MeshType>
	int32 FlipToDelaunayImpl(MeshType& Mesh, TSet<int32>& Uncorrected, const int32 MaxFlipCount, double Threshold = -TMathUtilConstants<double>::ZeroTolerance)
	{
	Uncorrected.Empty();

	// returns true if an edge should be flipped.
	auto EdgeShouldFlipFilter = [&Mesh, Threshold](int32 EID)->bool
	{
		if (!Mesh.IsEdge(EID) || Mesh.IsBoundaryEdge(EID))
		{
			return false;
		}
	
		const double CotanWeightValue = Mesh.EdgeCotanWeight(EID);
		
		// Delaunay test
		return CotanWeightValue < Threshold;
	};

	// Enqueue the "bad" edges.
	FIndexFIFO EdgeQueue(Mesh.EdgeIndicesItr(), EdgeShouldFlipFilter);

	// flip away the bad edges
	int32 FlipCount = 0;
	int32 EID;
	while (EdgeQueue.Dequeue(EID) && FlipCount < MaxFlipCount)
	{
		if (EdgeShouldFlipFilter(EID))
		{
			FIntrinsicTriangulation::FEdgeFlipInfo EdgeFlipInfo;
			const EMeshResult Result = Mesh.FlipEdge(EID, EdgeFlipInfo);

			if (Result == EMeshResult::Ok)
			{
				FlipCount++;
				for (int32 t = 0; t < 2; ++t)
				{
					const FIndex3i TriEIDs = Mesh.GetTriEdges(EdgeFlipInfo.Triangles[t]);
					int32 IndexOf = TriEIDs.IndexOf(EID);
					EdgeQueue.Enqueue(TriEIDs[(IndexOf + 1) % 3]);
					EdgeQueue.Enqueue(TriEIDs[(IndexOf + 2) % 3]);
				}
			}
			else
			{
				Uncorrected.Add(EID);
			}
		}
	}

	return FlipCount;
	}

};


int32 UE::Geometry::FlipToDelaunay(FIntrinsicTriangulation& IntrinsicMesh, TSet<int32>& Uncorrected, const int32 MaxFlipCount)
{
	return FlipToDelaunayImpl(IntrinsicMesh, Uncorrected, MaxFlipCount);
}

int32 UE::Geometry::FlipToDelaunay(FIntrinsicEdgeFlipMesh& IntrinsicMesh, TSet<int32>& Uncorrected, const int32 MaxFlipCount)
{
	return FlipToDelaunayImpl(IntrinsicMesh, Uncorrected, MaxFlipCount);
}




/**------------------------------------------------------------------------------
* FIntrinsicEdgeFlipMesh Methods
*------------------------------------------------------------------------------ */

FIntrinsicEdgeFlipMesh::FIntrinsicEdgeFlipMesh(const FDynamicMesh3& SrcMesh) :
	Vertices(SrcMesh.GetVerticesBuffer()),
	VertexRefCounts{ SrcMesh.GetVerticesRefCounts() },
	VertexEdgeLists{ SrcMesh.GetVertexEdges() },

	Triangles{ SrcMesh.GetTrianglesBuffer() },
	TriangleRefCounts{ SrcMesh.GetTrianglesRefCounts() },
	TriangleEdges{ SrcMesh.GetTriangleEdges() },

	Edges{ SrcMesh.GetEdgesBuffer() },
	EdgeRefCounts{ SrcMesh.GetEdgesRefCounts() }
{
	
	const int32 MaxEID = MaxEdgeID();
	EdgeLengths.SetNum(MaxEID);
	for (int32 EID = 0; EID < MaxEID; ++EID)
	{
		if (!IsEdge(EID))
		{
			continue;
		}
		const FIndex2i EdgeV = GetEdgeV(EID);
		const FVector3d Pos[2] = { GetVertex(EdgeV.A), GetVertex(EdgeV.B) };
		EdgeLengths[EID] = (Pos[1] - Pos[0]).Length();
	}

	const int32 MaxTriID = MaxTriangleID();
	InternalAngles.SetNum(MaxTriID);
	for (int32 TID = 0; TID < MaxTriID; ++TID)
	{
		if (!IsTriangle(TID))
		{
			continue;
		}
		// angles at v0, v1, v2, in that order
		InternalAngles[TID] = ComputeTriInternalAnglesR(TID);
	}
}


FIndex2i FIntrinsicEdgeFlipMesh::GetEdgeOpposingV(int32 EID) const
{
	const FEdge& Edge = Edges[EID];
	FIndex2i Result(InvalidID, InvalidID);

	for (int32 i = 0; i < 2; ++i)
	{
		int32 TriID = Edge.Tri[i];
		if (TriID == InvalidID) continue;

		const FIndex3i TriEIDs = GetTriEdges(TriID);
		const int32 IndexOf = TriEIDs.IndexOf(EID);
		const FIndex3i TriVIDs = GetTriangle(TriID);
		Result[i] = TriVIDs[AddTwoModThree[IndexOf]];
	}

	return Result;
}


FIndex2i FIntrinsicEdgeFlipMesh::GetOrientedEdgeV(int32 EID, int32 TID) const
{
	int32 IndexOf = GetTriEdges(TID).IndexOf(EID);
	checkSlow(IndexOf != InvalidID);
	FIndex3i TriVIDs = GetTriangle(TID);
	return FIndex2i(TriVIDs[IndexOf], TriVIDs[AddOneModThree[IndexOf]]);
}


int32 FIntrinsicEdgeFlipMesh::ReplaceEdgeTriangle(int32 eID, int32 tOld, int32 tNew)
{
	FIndex2i& Tris = Edges[eID].Tri;
	int32 a = Tris[0], b = Tris[1];
	if (a == tOld) {
		if (tNew == InvalidID)
		{
			Tris[0] = b;
			Tris[1] = InvalidID;
		}
		else
		{
			Tris[0] = tNew;
		}
		return 0;
	}
	else if (b == tOld)
	{
		Tris[1] = tNew;
		return 1;
	}
	else
	{
		return -1;
	}
}


EMeshResult FIntrinsicEdgeFlipMesh::FlipEdgeTopology(int32 eab, FEdgeFlipInfo& FlipInfo)
{
	if (!IsEdge(eab))
	{
		return EMeshResult::Failed_NotAnEdge;
	}
	if (IsBoundaryEdge(eab))
	{
		return EMeshResult::Failed_IsBoundaryEdge;
	}

	// find oriented edge [a,b], tris t0,t1, and other verts c in t0, d in t1
	const FEdge Edge = Edges[eab];
	int32 t0 = Edge.Tri[0], t1 = Edge.Tri[1];

	FIndex2i oppV = GetEdgeOpposingV(eab);
	FIndex2i orientedV = GetOrientedEdgeV(eab, t0);
	int32 a = orientedV.A, b = orientedV.B;

	if (oppV[0] == InvalidID || oppV[1] == InvalidID)
	{
		return EMeshResult::Failed_BrokenTopology;
	}
	int32 c = oppV[0];
	int32 d = oppV[1];

	const FIndex3i T0te = GetTriEdges(t0);
	const FIndex3i T1te = GetTriEdges(t1);

	const int32 T0IndexOf = T0te.IndexOf(eab);
	const int32 T1IndexOf = T1te.IndexOf(eab);
	// find edges bc, ca, ad, db
	const int32 ebc = T0te[AddOneModThree[T0IndexOf]];
	const int32 eca = T0te[AddTwoModThree[T0IndexOf]];

	const int32 ead = T1te[AddOneModThree[T1IndexOf]];
	const int32 edb = T1te[AddTwoModThree[T1IndexOf]];

	// update triangles
	Triangles[t0] = FIndex3i(c, d, b);
	Triangles[t1] = FIndex3i(d, c, a);

	// update edge AB, which becomes flipped edge CD
	SetEdgeVerticesInternal(eab, c, d);
	SetEdgeTrianglesInternal(eab, t0, t1);
	const int32 ecd = eab;

	// update the two other edges whose triangle nbrs have changed
	if (ReplaceEdgeTriangle(eca, t0, t1) == -1)
	{
		checkfSlow(false, TEXT("FIntrinsicEdgeFlipMesh.FlipEdge: first ReplaceEdgeTriangle failed"));
		return EMeshResult::Failed_UnrecoverableError;
	}
	if (ReplaceEdgeTriangle(edb, t1, t0) == -1)
	{
		checkfSlow(false, TEXT("FIntrinsicEdgeFlipMesh.FlipEdge: second ReplaceEdgeTriangle failed"));
		return EMeshResult::Failed_UnrecoverableError;
	}

	// update triangle nbr lists (these are edges)
	TriangleEdges[t0] = FIndex3i(ecd, edb, ebc);
	TriangleEdges[t1] = FIndex3i(ecd, eca, ead);

	// remove old eab from verts a and b, and Decrement ref counts
	if (VertexEdgeLists.Remove(a, eab) == false)
	{
		checkfSlow(false, TEXT("FIntrinsicEdgeFlipMesh.FlipEdge: first edge list remove failed"));
		return EMeshResult::Failed_UnrecoverableError;
	}
	VertexRefCounts.Decrement(a);
	if (a != b)
	{
		if (VertexEdgeLists.Remove(b, eab) == false)
		{
			checkfSlow(false, TEXT("FIntrinsicEdgeFlipMesh.FlipEdge: second edge list remove failed"));
			return EMeshResult::Failed_UnrecoverableError;
		}
		VertexRefCounts.Decrement(b);
	}
	if (IsVertex(a) == false || IsVertex(b) == false)
	{
		checkfSlow(false, TEXT("FIntrinsicEdgeFlipMesh.FlipEdge: either a or b is not a vertex?"));
		return EMeshResult::Failed_UnrecoverableError;
	}

	// add edge ecd to verts c and d, and increment ref counts
	VertexEdgeLists.Insert(c, ecd);
	VertexRefCounts.Increment(c);
	if (c != d)
	{
		VertexEdgeLists.Insert(d, ecd);
		VertexRefCounts.Increment(d);
	}

	// success! collect up results
	FlipInfo.EdgeID = eab;
	FlipInfo.OriginalVerts = FIndex2i(a, b);
	FlipInfo.OpposingVerts = FIndex2i(c, d);
	FlipInfo.Triangles = FIndex2i(t0, t1);
	return EMeshResult::Ok;
}


FVector3d FIntrinsicEdgeFlipMesh::ComputeTriInternalAnglesR(const int32 TID) const
{
	const FVector3d Lengths = GetTriEdgeLengths(TID);
	FVector3d Angles;
	for (int32 v = 0; v < 3; ++v)
	{
		Angles[v] = InteriorAngle(Lengths[v], Lengths[AddOneModThree[v]], Lengths[AddTwoModThree[v]]);
	}

	return Angles;
}


double FIntrinsicEdgeFlipMesh::GetOpposingVerticesDistance(int32 EID) const
{
	const double OrgLength = GetEdgeLength(EID);
	const FIndex2i EdgeTris = GetEdgeT(EID);
	checkSlow(EdgeTris[1] != FDynamicMesh3::InvalidID);

	// compute 2d locations of the verts opposite the EID edge.
	FVector2d Opp2dVerts[2];
	for (int32 i = 0; i < 2; ++i)
	{
		const FIndex3i TriEIDs = GetTriEdges(EdgeTris[i]);
		const FVector3d Ls = GetEdgeLengthTriple(TriEIDs);
		const int32 IndexOf = TriEIDs.IndexOf(EID);
		const FVector3d PermutedLs = Permute(IndexOf, Ls);
		Opp2dVerts[i] = ComputeOpposingVert2d(PermutedLs[0], PermutedLs[1], PermutedLs[2]);
	}
	// rotate second tri so the shared edge aligns
	Opp2dVerts[1].Y = -Opp2dVerts[1].Y;
	Opp2dVerts[1].X = OrgLength - Opp2dVerts[1].X;

	return (Opp2dVerts[0] - Opp2dVerts[1]).Length();
}


EMeshResult FIntrinsicEdgeFlipMesh::FlipEdge(int32 EID, FEdgeFlipInfo& EdgeFlipInfo)
{
	if (!IsEdge(EID))
	{
		return EMeshResult::Failed_NotAnEdge;
	}

	if (IsBoundaryEdge(EID))
	{
		return EMeshResult::Failed_IsBoundaryEdge;
	}

	// prohibit case where the edge is shared by a non-convex pair of triangles.
	{
		// original triangles
		FDynamicMesh3::FEdge OrgEdge = GetEdge(EID);

		// Assumes both triangles have same orientation
		double TotalAngleAtOrgVert[2] = { 0., 0. };
		for (int32 i = 0; i < 2; ++i)
		{	
			int32 TriID = OrgEdge.Tri[i];
			int32 IndexOf = GetTriEdges(TriID).IndexOf(EID);
			TotalAngleAtOrgVert[i] += InternalAngles[TriID][IndexOf];
			TotalAngleAtOrgVert[(i+1) % 2] += InternalAngles[TriID][AddOneModThree[IndexOf]];
		}

		if (TotalAngleAtOrgVert[0] > TMathUtilConstants<double>::Pi - TMathUtilConstants<double>::ZeroTolerance || TotalAngleAtOrgVert[1] > TMathUtilConstants<double>::Pi - TMathUtilConstants<double>::ZeroTolerance)
		{
			return EMeshResult::Failed_Unsupported;
		}
	}

	// compute the length of edge after flip
	const double PostFlipLength = GetOpposingVerticesDistance(EID);

	// flip edge in the underlying mesh
	const EMeshResult MeshFlipResult = FlipEdgeTopology(EID, EdgeFlipInfo);

	if (MeshFlipResult != EMeshResult::Ok)
	{
		// could fail for many reasons, e.g. a boundary edge or the new edge already exists.
		return MeshFlipResult;
	}

	// update the intrinsic edge length
	EdgeLengths[EID] = PostFlipLength;

	// update interior angles for the tris
	for (int32 t = 0; t < 2; ++t)
	{
		const int32 TID = EdgeFlipInfo.Triangles[t];
		InternalAngles[TID] = ComputeTriInternalAnglesR(TID);	
	}

	return MeshFlipResult;
}


FVector2d FIntrinsicEdgeFlipMesh::EdgeOpposingAngles(int32 EID) const
{
	FVector2d Result;
	FDynamicMesh3::FEdge Edge = GetEdge(EID);
	{
		const int32 IndexOf = GetTriEdges(Edge.Tri.A).IndexOf(EID);
		const int32 IndexOfOpp = AddTwoModThree[IndexOf];
		const double Angle = GetTriInternalAngleR(Edge.Tri.A, IndexOfOpp);
		Result[0] = Angle;
	}
	if (Edge.Tri.B != FDynamicMesh3::InvalidID)
	{
		const int32 IndexOf = GetTriEdges(Edge.Tri.B).IndexOf(EID);
		const int32 IndexOfOpp = AddTwoModThree[IndexOf];
		const double Angle = GetTriInternalAngleR(Edge.Tri.B, IndexOfOpp);
		Result[1] = Angle;
	}
	else
	{
		Result[1] = -TMathUtilConstants<double>::MaxReal;
	}

	return Result;
}


double FIntrinsicEdgeFlipMesh::EdgeCotanWeight(int32 EID) const
{
	auto ComputeCotanOppAngle = [this](int32 EID, int32 TID)->double
	{
		const FIndex3i TriEIDs = GetTriEdges(TID);
		const int32 IOf = TriEIDs.IndexOf(EID);
		const FVector3d TriLs = GetEdgeLengthTriple(TriEIDs);

		const FVector3d Ls(TriLs[IOf], TriLs[AddOneModThree[IOf]], TriLs[AddTwoModThree[IOf]]);
		return ComputeCotangent(Ls[0], Ls[1], Ls[2]);
	};

	FDynamicMesh3::FEdge Edge = GetEdge(EID);
	double Result = ComputeCotanOppAngle(EID, Edge.Tri.A);
	if (Edge.Tri.B != FDynamicMesh3::InvalidID)
	{
		Result += ComputeCotanOppAngle(EID, Edge.Tri.B);
		Result /= 2;
	}

	return Result;
}



/**------------------------------------------------------------------------------
*  FIntrinsicTriangulation Methods
*-------------------------------------------------------------------------------*/

FIntrinsicTriangulation::FSurfacePoint::FSurfacePoint()
{
	PositionType = EPositionType::Vertex;
	Position.VertexPosition.VID = FDynamicMesh3::InvalidID;
}

FIntrinsicTriangulation::FSurfacePoint::FSurfacePoint(int32 VID)
{
	PositionType = EPositionType::Vertex;
	Position.VertexPosition.VID = VID;
}

FIntrinsicTriangulation::FSurfacePoint::FSurfacePoint(int32 EdgeID, double Alpha)
{
	PositionType = EPositionType::Edge;
	Position.EdgePosition.EdgeID = EdgeID;
	Position.EdgePosition.Alpha = Alpha;
}

FIntrinsicTriangulation::FSurfacePoint::FSurfacePoint(int32 TriID, const FVector3d& BaryCentrics)
{
	PositionType = EPositionType::Triangle;
	Position.TriPosition.TriID = TriID;
	Position.TriPosition.BarycentricCoords[0] = BaryCentrics[0];
	Position.TriPosition.BarycentricCoords[1] = BaryCentrics[1];
	Position.TriPosition.BarycentricCoords[2] = BaryCentrics[2];
}

FVector3d FIntrinsicTriangulation::FSurfacePoint::FSurfacePoint::AsR3Position(const FDynamicMesh3& Mesh, bool& bIsValid) const
{
	FVector3d Result(0., 0., 0.);
	switch (PositionType)
	{
		case FSurfacePoint::EPositionType::Vertex:
		{
			const int32 SurfaceVID = Position.VertexPosition.VID;
			bIsValid = Mesh.IsVertex(SurfaceVID);
			if (bIsValid)
			{
				Result = Mesh.GetVertex(SurfaceVID);
			}
		}
		break;
		case FSurfacePoint::EPositionType::Edge:
		{
			const int32 SurfaceEID = Position.EdgePosition.EdgeID;
			double Alpha = Position.EdgePosition.Alpha;
			bIsValid = Mesh.IsEdge(SurfaceEID);
			if (bIsValid)
			{
				const FIndex2i SurfaceEdgeV = Mesh.GetEdgeV(SurfaceEID);
				Result = Alpha * (Mesh.GetVertex(SurfaceEdgeV.A)) + (1. - Alpha) * (Mesh.GetVertex(SurfaceEdgeV.B));
			}
		}
		break;
		case FSurfacePoint::EPositionType::Triangle:
		{
			const int32 SurfaceTID = Position.TriPosition.TriID;
			bIsValid = Mesh.IsTriangle(SurfaceTID);

			if (bIsValid)
			{
				Result = Mesh.GetTriBaryPoint(SurfaceTID,
					Position.TriPosition.BarycentricCoords[0],
					Position.TriPosition.BarycentricCoords[1],
					Position.TriPosition.BarycentricCoords[2]);
			}

		}
		break;
		default:
		{
			bIsValid = false;
			// shouldn't be able to reach this point
			check(0);
		}
	}
	return Result;
}

UE::Geometry::FIntrinsicTriangulation::FIntrinsicTriangulation(const FDynamicMesh3& SrcMesh)
	: MyBase(SrcMesh)
	, ExtrinsicMesh(&SrcMesh)
{
	// During construction the mesh is a deep copy of the SrcMesh ( i.e. has the same IDs)

	// pick local reference directions for each vertex and triangle.
	const int32 MaxVertexID   = ExtrinsicMesh->MaxVertexID();
	const int32 MaxTriangleID = ExtrinsicMesh->MaxTriangleID();

	{
		VIDToReferenceEID.AddUninitialized(MaxVertexID);
		TIDToReferenceEID.AddUninitialized(MaxTriangleID);

		for (int32 VID = 0; VID < MaxVertexID; ++VID)
		{
			if (ExtrinsicMesh->IsVertex(VID))
			{
				int32 RefEID = FDynamicMesh3::InvalidID;
				if (ExtrinsicMesh->IsBoundaryVertex(VID) && !ExtrinsicMesh->IsBowtieVertex(VID))
				{
					// vertex is on the mesh boundary, 
					// choose the boundary edge such that traveling CCW (about the vertex) moves into the mesh
					for (int32 EID : ExtrinsicMesh->VtxEdgesItr(VID))
					{
						if (ExtrinsicMesh->IsBoundaryEdge(EID))
						{
							const int32 TID = ExtrinsicMesh->GetEdgeT(EID).A;
							const int32 IndexOf = ExtrinsicMesh->GetTriEdges(TID).IndexOf(EID);
							if (ExtrinsicMesh->GetTriangle(TID)[IndexOf] == VID)
							{
								RefEID = EID;
								break;
							}
						}
					}
				}
				else
				{
					// vertex isn't on a mesh boundary, just pick the first adj edge to be the reference edge
					for (int32 EID : ExtrinsicMesh->VtxEdgesItr(VID))
					{
						RefEID = EID;
						break;
					}
				}
				VIDToReferenceEID[VID] = RefEID;
			}
		}
		for (int32 TID = 0; TID < MaxTriangleID; ++TID)
		{
			if (ExtrinsicMesh->IsTriangle(TID))
			{
				TIDToReferenceEID[TID] = ExtrinsicMesh->GetTriEdge(TID, 0);
			}
		}
	}

	// add surface position.
	{
		IntrinsicVertexPositions.SetNum(MaxVertexID);

		// initialize: identify with vertex in Extrinsic Mesh
		for (int32 VID = 0; VID < MaxVertexID; ++VID)
		{
			if (IsVertex(VID))
			{
				FSurfacePoint VertexSurfacePoint(VID);
				IntrinsicVertexPositions[VID] = VertexSurfacePoint;
			}
		}
	}

	// add edge directions
	{
		const int32 MaxExtEdgeID = ExtrinsicMesh->MaxEdgeID();
		IntrinsicEdgeAngles.SetNum(MaxTriangleID);

		GeometricVertexInfo.SetNum(MaxVertexID);
		IntrinsicVertexPositions.SetNum(MaxVertexID);

		ParallelFor(MaxVertexID, [this, &SrcMesh](int32 VID)
		{
			if (!ExtrinsicMesh->IsVertex(VID))
			{
				return;
			}

			const int32 PlusTwoModThree[3] = { 2, 0, 1 }; // PlusTwoModThree[i] = (i + 2)%3

			// initialize by computing the angles
			TArray<int32> Triangles;
			TArray<int32> ContiguousGroupLengths;
			TArray<bool> GroupIsLoop;

			{

				FGeometricInfo GeometricInfo;
				// note: since the structure and IDs are a deep copy of the source mesh, we can use it to find the contiguous triangles
				const EMeshResult Result = SrcMesh.GetVtxContiguousTriangles(VID, Triangles, ContiguousGroupLengths, GroupIsLoop);
				if (Result == EMeshResult::Ok && GroupIsLoop.Num() == 1)
				{
					GeometricInfo.bIsInterior = (GroupIsLoop[0] == true);
					// Contiguous tris could be cw or ccw?  Need them to  be ccw
					if (Triangles.Num() > 1)
					{
						const int32 TID0 = Triangles[0];
						FIndex3i TriVIDs = GetTriangle(TID0);
						FIndex3i TriEIDs = GetTriEdges(TID0);
						int32 VertSubIdx = TriVIDs.IndexOf(VID);
						const int32 EID = TriEIDs[VertSubIdx];
						const int32 NextEID = TriEIDs[PlusTwoModThree[VertSubIdx]];
						const int32 TID1 = Triangles[1];
						TriVIDs = GetTriangle(TID1);
						TriEIDs = GetTriEdges(TID1);
						VertSubIdx = TriVIDs.IndexOf(VID);

						if (TriEIDs[VertSubIdx] != NextEID)
						{
							// need to reverse order to have CCW
							Algo::Reverse(Triangles);
						}
					}

					const int32 NumEdges = ContiguousGroupLengths[0];

					double AngleOffset = 0;
					double WalkedAngle = 0;
					const int32 ReferenceEID = VIDToReferenceEID[VID];
					checkSlow(ReferenceEID != FDynamicMesh3::InvalidID)

					TMap<int32, int32> VisitedTriSubID;
					for (int32 TID : Triangles)
					{
						const FIndex3i TriVIDs = GetTriangle(TID);
						const FIndex3i TriEIDs = GetTriEdges(TID);
						const int32 VertSubIdx = TriVIDs.IndexOf(VID);
						IntrinsicEdgeAngles[TID][VertSubIdx] = WalkedAngle;

						VisitedTriSubID.Add(TID, VertSubIdx);
						const int32 EID = TriEIDs[VertSubIdx];
						if (EID == ReferenceEID)
						{
							AngleOffset = WalkedAngle;
						}
						
						WalkedAngle += GetTriInternalAngleR(TID, VertSubIdx);
					}
					// need to include last edge if the one ring of triangles isn't a closed loop, it could be the reference edge.
					if (GroupIsLoop[0] == false)
					{
						const int32 TID = Triangles.Last();
						const FIndex3i TriVIDs = GetTriangle(TID);
						const FIndex3i TriEIDs = GetTriEdges(TID);
						const int32 VertSubIdx = TriVIDs.IndexOf(VID);
						const int32 EID = TriEIDs[PlusTwoModThree[VertSubIdx]];
						if (EID == ReferenceEID)
						{
							AngleOffset = WalkedAngle;
						}
					}

					// orient with the reference edge at zero (or pi) angle  and rescale to radians and use 0,2pi to define polar angle.
					const double ToRadians = TMathUtilConstants<double>::TwoPi / WalkedAngle;
					GeometricInfo.ToRadians = ToRadians;
					double RefAngle = (ExtrinsicMesh->GetEdgeV(ReferenceEID).B == VID) ? TMathUtilConstants<double>::Pi : 0.;
					for (TPair<int32, int32>& TriSubIDPair : VisitedTriSubID)
					{
						int32 TID = TriSubIDPair.Key;
						int32 SubID = TriSubIDPair.Value;
						double& Angle = IntrinsicEdgeAngles[TID][SubID];
						Angle = ToRadians * (Angle - AngleOffset) + RefAngle;
						Angle = AsZeroToTwoPi(Angle);
					}
				}
				else // bow-tie or boundary
				{
					GeometricInfo.bIsInterior = false;
				}

				GeometricVertexInfo[VID] = GeometricInfo;
			}
		}, false /* force single thread*/);
	}

}

TArray<FIntrinsicTriangulation::FSurfacePoint> UE::Geometry::FIntrinsicTriangulation::TraceEdge(int32 EID, double CoalesceThreshold, bool bReverse) const
{
	TArray<FIntrinsicTriangulation::FSurfacePoint> SurfacePoints;
	if (!IsEdge(EID))
	{
		return SurfacePoints;
	}


	const FDynamicMesh3* SurfaceMesh = GetExtrinsicMesh();
	const double IntrinsicEdgeLength = GetEdgeLength(EID);
	const FIndex2i EdgeV = GetEdgeV(EID);
	int32 StartVID = EdgeV.A;
	int32 EndVID = EdgeV.B;
	if (bReverse)
	{
		StartVID = EdgeV.B;
		EndVID = EdgeV.A;
	}


	FIndex2i AdjTIDs = GetEdgeT(EID);

	// look-up the polar angle of this edge as it leaves StartVID ( this angle is relative to a specified reference mesh edge )
	const double PolarAngle = 
		[&]{
			const int32 AdjTID = AdjTIDs.A;
			const int32 IndexOf = GetTriEdges(AdjTID).IndexOf(EID);
			checkSlow(IndexOf > -1);
			if (GetTriangle(AdjTID)[IndexOf] == StartVID)
			{
				// IntrinsicEdgeAngles hold the local angle of each out-going edge relative to the vertex the edge exits
				return IntrinsicEdgeAngles[AdjTID][IndexOf];
			}
			else
			{
				const double ToRadians = GeometricVertexInfo[StartVID].ToRadians;
				// polar angle of prev edge just before (clockwise) the edge we want
				const double PrevPolarAngle = IntrinsicEdgeAngles[AdjTID][(IndexOf + 1) % 3];
				// angle between previous edge and this one
				const double InternalAngle = InternalAngles[AdjTID][(IndexOf + 1) % 3];
				// add the internal angle to rotate from Prev Edge to this edge
				return ToRadians * InternalAngle + PrevPolarAngle;
			}
		}();

	// Trace the surface mesh from the intrinsic StartVID, in the PolarAngle direction, a distance of IntrinsicEdgeLength.
	// 
	// Note: this intrisic vertex may or may not correspond to a vertex in the surface mesh if vertices were added to the intrinsic mesh
	// by doing an edge split or a triangle poke.
	FMeshGeodesicSurfaceTracer SurfaceTracer(*SurfaceMesh);
	{
		const FSurfacePoint& StartSurfacePoint = GetVertexSurfacePoint(StartVID);
		const FSurfacePoint::FSurfacePositionUnion& SurfacePosition = StartSurfacePoint.Position;
		switch (StartSurfacePoint.PositionType)
		{
			case FSurfacePoint::EPositionType::Vertex:
			{
				const int32 SurfaceVID = SurfacePosition.VertexPosition.VID;
				const int32 RefSurfaceEID = VIDToReferenceEID[SurfaceVID];

				FMeshGeodesicSurfaceTracer::FMeshTangentDirection TangentAtVertex = { SurfaceVID, RefSurfaceEID, PolarAngle };
				SurfaceTracer.TraceMeshFromVertex(TangentAtVertex, IntrinsicEdgeLength);
			}
			break;
			case FSurfacePoint::EPositionType::Edge:
			{
				const int32 RefSurfaceEID = SurfacePosition.EdgePosition.EdgeID;
				double Alpha = SurfacePosition.EdgePosition.Alpha;
				// convert to BC
				const int32 SurfaceTID = SurfaceMesh->GetEdgeT(RefSurfaceEID).A;
				const FIndex2i SurfaceEdgeV = SurfaceMesh->GetEdgeV(RefSurfaceEID);
				const int32 IndexOf = SurfaceMesh->GetTriEdges(SurfaceTID).IndexOf(RefSurfaceEID);
				const bool bSameOrientation = (SurfaceMesh->GetTriangle(SurfaceTID)[IndexOf] == SurfaceEdgeV.A);
				FVector3d BaryPoint(0., 0., 0.);
				if (!bSameOrientation)
				{
					Alpha = 1. - Alpha;
				}
				BaryPoint[IndexOf] = Alpha;
				BaryPoint[(IndexOf + 1) % 3] = 1. - Alpha;

				FMeshGeodesicSurfaceTracer::FMeshSurfaceDirection DirectionOnSurface(RefSurfaceEID, PolarAngle);
				SurfaceTracer.TraceMeshFromBaryPoint(SurfaceTID, BaryPoint, DirectionOnSurface, IntrinsicEdgeLength);
			}
			break;
			case FSurfacePoint::EPositionType::Triangle:
			{
				const int32 SurfaceTID = SurfacePosition.TriPosition.TriID;

				const FVector3d BaryPoint(SurfacePosition.TriPosition.BarycentricCoords[0],
					SurfacePosition.TriPosition.BarycentricCoords[1],
					SurfacePosition.TriPosition.BarycentricCoords[2]);
				const int32 RefSurfaceEID = TIDToReferenceEID[SurfaceTID];

				FMeshGeodesicSurfaceTracer::FMeshSurfaceDirection DirectionOnSurface(RefSurfaceEID, PolarAngle);
				SurfaceTracer.TraceMeshFromBaryPoint(SurfaceTID, BaryPoint, DirectionOnSurface, IntrinsicEdgeLength);
			}
			break;
			default:
			{
				// shouldn't be able to reach this point
				check(0);
			}
		}
	}

	// util to convert the trace result to a surface point, potentially snapping to a vertex if within the coalesce threshold 
	auto TraceResultToSurfacePoint = [CoalesceThreshold, SurfaceMesh](const FMeshGeodesicSurfaceTracer::FTraceResult& TraceResult)->FSurfacePoint
	{

	
		if (TraceResult.bIsEdgePoint)
		{
			// TraceResult alpha is defined as EdgeV.A * (1-Alpha) + EdgeV.B Alpha;  This is the complement of what we want
			const double Alpha = FMath::Clamp((1. - TraceResult.EdgeAlpha), 0., 1.);
			const int32 EID = TraceResult.EdgeID;

			if (Alpha <= 0.5 && Alpha < CoalesceThreshold)
			{
				return FSurfacePoint(SurfaceMesh->GetEdgeV(EID).B);
			}
			else if (Alpha > 0.5 && (1. - Alpha) < CoalesceThreshold)
			{
				return FSurfacePoint(SurfaceMesh->GetEdgeV(EID).A);
			}

			return FSurfacePoint(EID, Alpha);
		}
		else
		{
			// TODO - should snap to vertex / edge if close?
			const int32 TID = TraceResult.TriID;
			const FVector3d BC = TraceResult.Barycentric;
			return FSurfacePoint(TID, BC);
		}
	};

	// Add surface point to the outgoing array, but don't allow for duplicate vertex points
	auto AddSurfacePoint = [&SurfacePoints](const FSurfacePoint& PointA)
	{
		if (SurfacePoints.Num() == 0)
		{
			SurfacePoints.Add(PointA);
		}
		else
		{
			const FSurfacePoint& PointB = SurfacePoints.Last();
			const bool bAreSameVertexPoint = ( PointA.PositionType == FSurfacePoint::EPositionType::Vertex &&
				                               PointB.PositionType == FSurfacePoint::EPositionType::Vertex &&
				                               PointA.Position.VertexPosition.VID == PointB.Position.VertexPosition.VID );
			if (!bAreSameVertexPoint)
			{
				SurfacePoints.Add(PointA);
			}
		}
	};

	// package the surface trace results as series of surface points.  Note, because this is a trace along an intrinsic edge
	// the first and last result will be an intrinsic vertex (StartVID and EndVID)
	TArray<FMeshGeodesicSurfaceTracer::FTraceResult>& TraceResults = SurfaceTracer.GetTraceResults();
	{
		int32 NumTraceResults = TraceResults.Num();
		AddSurfacePoint(GetVertexSurfacePoint(StartVID));
		for (int32 i = 1; i < NumTraceResults - 1; ++i)
		{
			FMeshGeodesicSurfaceTracer::FTraceResult& TraceResult = TraceResults[i];
			FSurfacePoint SurfacePoint = TraceResultToSurfacePoint(TraceResult);
			AddSurfacePoint(SurfacePoint);
		}
		AddSurfacePoint(GetVertexSurfacePoint(EndVID));
	}


	return SurfacePoints;
}

EMeshResult UE::Geometry::FIntrinsicTriangulation::FlipEdge(int32 EID, FEdgeFlipInfo& EdgeFlipInfo)
{

	if (IsBoundaryEdge(EID))
	{
		return EMeshResult::Failed_IsBoundaryEdge;
	}
	// capture the state of the triangles before the flip.
	
	FIndex2i Tris = GetEdgeT(EID);
	const int32 PreFlipIndexOf[2] = {GetTriEdges(Tris[0]).IndexOf(EID), GetTriEdges(Tris[1]).IndexOf(EID)};

	// edge polar directions before the flip
	const FVector3d PreFlipDirections[2] = { IntrinsicEdgeAngles[Tris[0]], IntrinsicEdgeAngles[Tris[1]] };
	const FVector3d PreFlipInteriorAngles[2] = { InternalAngles[Tris[0]], InternalAngles[Tris[1]] };

	// polar directions for the four edges that don't change.
	const double bcDir = PreFlipDirections[0][AddOneModThree[PreFlipIndexOf[0]]];
	const double caDir = PreFlipDirections[0][AddTwoModThree[PreFlipIndexOf[0]]];

	const double adDir = PreFlipDirections[1][AddOneModThree[PreFlipIndexOf[1]]];
	const double dbDir = PreFlipDirections[1][AddTwoModThree[PreFlipIndexOf[1]]];

	// flip edge in the underlying mesh
	// this updates the edge lengths and the interior angles.
	const EMeshResult MeshFlipResult = MyBase::FlipEdge(EID, EdgeFlipInfo);

	if (MeshFlipResult != EMeshResult::Ok)
	{
		// could fail for many reasons, e.g. a boundary edge 
		return MeshFlipResult;
	}

	// compute the polar edge directions for the new edge (ie. dcDir and cdDir)
	// dcDir 
	const double newAngleAtD = InternalAngles[Tris[0]][1];
	const double ToPolarAtD = GeometricVertexInfo[EdgeFlipInfo.OpposingVerts[1]].ToRadians;
	const double dcDir = AsZeroToTwoPi(dbDir + ToPolarAtD * newAngleAtD);

	// cdDir 
	const double newAngleAtC = InternalAngles[Tris[1]][1];
	const double ToPolarAtC =  GeometricVertexInfo[EdgeFlipInfo.OpposingVerts[0]].ToRadians;
	const double cdDir = AsZeroToTwoPi(caDir + ToPolarAtC * newAngleAtC);

	IntrinsicEdgeAngles[Tris[0]] = FVector3d(cdDir, dbDir, bcDir); // edges leaving c, d, b
	IntrinsicEdgeAngles[Tris[1]] = FVector3d(dcDir, caDir, adDir); // edges leaving d, c, a
	
	return EMeshResult::Ok;
}


double UE::Geometry::FIntrinsicTriangulation::UpdateVertexByEdgeTrace(const int32 NewVID, const int32 TraceStartVID, const double TracePolarAngle, const double TraceDist)
{
	const FSurfacePoint& StartSurfacePoint = IntrinsicVertexPositions[TraceStartVID];

	double ActualDist = 0;
	FMeshGeodesicSurfaceTracer SurfaceTracer(*ExtrinsicMesh);
	switch (StartSurfacePoint.PositionType)
	{
	case FSurfacePoint::EPositionType::Vertex:
	{
		// convert vertex location
		const int32 ExtrinsicVID = StartSurfacePoint.Position.VertexPosition.VID;
		const int32 ExtrinsicRefEID = VIDToReferenceEID[ExtrinsicVID];

		const FMeshGeodesicSurfaceTracer::FMeshTangentDirection TangentDirection = { ExtrinsicVID, ExtrinsicRefEID, TracePolarAngle };
		ActualDist = SurfaceTracer.TraceMeshFromVertex(TangentDirection, TraceDist);

	}
	break;
	case FSurfacePoint::EPositionType::Edge:
	{
		// convert edge location -  Not used, and might have bugs
		check(0);

		const int32 ExtrinsicRefEID = StartSurfacePoint.Position.EdgePosition.EdgeID;
		const double Alpha = StartSurfacePoint.Position.EdgePosition.Alpha;
		const FDynamicMesh3::FEdge ExtEdge = ExtrinsicMesh->GetEdge(ExtrinsicRefEID);
		const int32 StartExtrinsicTID = ExtEdge.Tri.A; // Q? should I inspect the angle to determine which triangle we are entering?
		FIndex3i ExtrinsicTriVIDs = ExtrinsicMesh->GetTriangle(StartExtrinsicTID);
		FVector3d StartBarycentricCoords(0.);
		StartBarycentricCoords[ExtrinsicTriVIDs.IndexOf(ExtEdge.Vert.A)] = Alpha;
		StartBarycentricCoords[ExtrinsicTriVIDs.IndexOf(ExtEdge.Vert.B)] = 1. - Alpha;

		FMeshGeodesicSurfaceTracer::FMeshSurfaceDirection SurfaceDirection(ExtrinsicRefEID, TracePolarAngle);
	}
	break;
	case FSurfacePoint::EPositionType::Triangle:
	{
		// trace from BC point in triangle
		const int32 StartExtrinsicTID = StartSurfacePoint.Position.TriPosition.TriID;
		const int32 ExtrinsicRefEID = TIDToReferenceEID[StartExtrinsicTID];

		FVector3d StartBarycentricCoords(0.);
		for (int32 i = 0; i < 3; ++i)
		{
			StartBarycentricCoords[i] = StartSurfacePoint.Position.TriPosition.BarycentricCoords[i];
		}

		const FMeshGeodesicSurfaceTracer::FMeshSurfaceDirection SurfaceDirection(ExtrinsicRefEID, TracePolarAngle);
		ActualDist = SurfaceTracer.TraceMeshFromBaryPoint(StartExtrinsicTID, StartBarycentricCoords, SurfaceDirection, TraceDist);
	}
	break;
	default:
	{
		// not possible
		check(0);
	}
	}

	const FMeshGeodesicSurfaceTracer::FTraceResult& TraceResult = SurfaceTracer.GetTraceResults().Last();
	// update new vertex position in two ways: the r3 position and the intrinsic position 
	// 1) the r3 position
	FVector3d TraceResultPos = ExtrinsicMesh->GetTriBaryPoint(TraceResult.TriID, TraceResult.Barycentric.X, TraceResult.Barycentric.Y, TraceResult.Barycentric.Z);
	Vertices[NewVID] = TraceResultPos;
	// 2) the intrinsic position (i.e. relative to the surface)  
	//  Use "Triangle" type position for result ( Q: should we classify as "Edge" position if very close to edge ?)
	FSurfacePoint TraceResultPosition(TraceResult.TriID, TraceResult.Barycentric);
	IntrinsicVertexPositions.InsertAt(TraceResultPosition, NewVID);


	// at the new vertex location: update the polar angle for each new edge relative to the reference direction in this extrinsic triangle. 



	// fix directions relative to local reference edge on the extrinsic mesh 
	// by finding direction of the TraceEID edge indecent on NewVID
	double AngleOffset = 0;
	{
		FVector2d Dir = TraceResult.SurfaceDirection.Dir;

		// translate to Dir about the reference edge for this triangle
		int32 EndRefEID = TIDToReferenceEID[TraceResult.TriID];

		const FMeshGeodesicSurfaceTracer::FTangentTri2& TangentTri2 = SurfaceTracer.GetLastTri();
		// convert to local basis for first edge of tri2
		if (TangentTri2.EdgeOrientationSign[0] == -1)
		{
			Dir = -Dir;
		}
		const int32 IndexOfEndRefEID = TangentTri2.PermutedTriEIDs.IndexOf(EndRefEID);
		FVector2d DirRelToRefEID = TangentTri2.ChangeBasis(Dir, IndexOfEndRefEID);
		if (TangentTri2.EdgeOrientationSign[IndexOfEndRefEID] == -1)
		{
			DirRelToRefEID = -DirRelToRefEID;
		}
		// angle of (new edge) path to NewVert relative to Ref edge
		const double AngleToNewVert = FMath::Atan2(DirRelToRefEID.Y, DirRelToRefEID.X);
		// reverse direction of path because we NewVert is the local origin for polar angles around new vert
		const double AngleFromNewVert = AngleToNewVert + TMathUtilConstants<double>::Pi;
		AngleOffset = AsZeroToTwoPi(AngleFromNewVert);
	}

	return AngleOffset;
}


UE::Geometry::EMeshResult UE::Geometry::FIntrinsicTriangulation::PokeTriangleTopology(int32 TriangleID, FPokeTriangleInfo& PokeResult)
{
	PokeResult = FPokeTriangleInfo();

	if (!IsTriangle(TriangleID))
	{
		return EMeshResult::Failed_NotATriangle;
	}

	FIndex3i tv = GetTriangle(TriangleID);
	FIndex3i te = GetTriEdges(TriangleID);

	// create vertex with averaged position.. 
	FVector3d vPos = (1./3.)*(GetVertex(tv[0]) + GetVertex(tv[1]) + GetVertex(tv[2]));

	int32 newVertID = AppendVertex(vPos);


	// add in edges to center vtx, do not connect to triangles yet
	int32 eAN = AddEdgeInternal(tv[0], newVertID, -1, -1);
	int32 eBN = AddEdgeInternal(tv[1], newVertID, -1, -1);
	int32 eCN = AddEdgeInternal(tv[2], newVertID, -1, -1);
	VertexRefCounts.Increment(tv[0]);
	VertexRefCounts.Increment(tv[1]);
	VertexRefCounts.Increment(tv[2]);
	VertexRefCounts.Increment(newVertID, 3);

	// old triangle becomes tri along first edge
	Triangles[TriangleID] = FIndex3i(tv[0], tv[1], newVertID);
	TriangleEdges[TriangleID] = FIndex3i(te[0], eBN, eAN);

	// add two triangles
	int32 t1 = AddTriangleInternal(tv[1], tv[2], newVertID, te[1], eCN, eBN);
	int32 t2 = AddTriangleInternal(tv[2], tv[0], newVertID, te[2], eAN, eCN);

	// second and third edges of original tri have neighbors
	ReplaceEdgeTriangle(te[1], TriangleID, t1);
	ReplaceEdgeTriangle(te[2], TriangleID, t2);

	// set the triangles for the edges we created above
	SetEdgeTrianglesInternal(eAN, TriangleID, t2);
	SetEdgeTrianglesInternal(eBN, TriangleID, t1);
	SetEdgeTrianglesInternal(eCN, t1, t2);


	PokeResult.OriginalTriangle = TriangleID;
	PokeResult.TriVertices = tv;
	PokeResult.NewVertex = newVertID;
	PokeResult.NewTriangles = FIndex2i(t1, t2);
	PokeResult.NewEdges = FIndex3i(eAN, eBN, eCN);
	PokeResult.BaryCoords = FVector3d(1./3., 1./3., 1./3.);
	
	return EMeshResult::Ok;
}

UE::Geometry::EMeshResult UE::Geometry::FIntrinsicTriangulation::PokeTriangle(int32 TID, const FVector3d& BaryCoordinates, FPokeTriangleInfo& PokeInfo)
{
	if (!IsTriangle(TID))
	{
		return EMeshResult::Failed_NotATriangle;
	}

	// state before the poke
	const FIndex3i OriginalVIDs = GetTriangle(TID);
	const FIndex3i OriginalEdges = GetTriEdges(TID);
	const FVector3d OriginalTriEdgeLengths = GetTriEdgeLengths(TID);
	const FVector3d OriginalEdgeDirs = IntrinsicEdgeAngles[TID];
	
	// Add a new vertex and faces to the IntrinsicMesh.   
	// Note: the r3 position will be wrong initially since this poke will just interpolate the corners of the tri.
	//       we fix this position as the last step in this function
	EMeshResult PokeResult = PokeTriangleTopology(TID, PokeInfo);
	if (PokeResult != EMeshResult::Ok)
	{
		return PokeResult;
	}
	

	FIndex3i NewTris(TID, PokeInfo.NewTriangles[0], PokeInfo.NewTriangles[1]);
	const int32 NewVID = PokeInfo.NewVertex;

	// Need to update intrinsic information for the 3 triangles that resulted from the poke
	// 1) update the edge lengths for the triangles
	// 2) update the internal angles for the triangles
	// 3) update the edge directions for the triangles. 
	// 4) update the surface point for the new vertex by tracing one of the new edges. 
	//    also update the edge directions leaving the new vertex to be relative to the direction defined on the extrinsic mesh


	// (1) compute intrinsic edge lengths for the 3 new edges
	{
		FVector3d DistancesFromOldCorner;
		for (int32 v = 0; v < 3; ++v)
		{
			FVector3d BaryCoordVertex(0., 0., 0.); BaryCoordVertex[v] = 1.;
			const double Distance = DistanceBetweenBarycentricPoints( OriginalTriEdgeLengths[0],
							  										  OriginalTriEdgeLengths[1],
																	  OriginalTriEdgeLengths[2],
																	  BaryCoordVertex, BaryCoordinates);
			DistancesFromOldCorner[v] = Distance;
		}
	
		{
			// original tri is and verts (a, b,c) and edges (ab, bc, ca)
			// the updated tri has verts (a, b, new) and edges (ab, bnew, newa)
			FIndex3i T0EIDs = GetTriEdges(NewTris[0]);
			EdgeLengths.InsertAt(DistancesFromOldCorner[1], T0EIDs[1]);
			EdgeLengths.InsertAt(DistancesFromOldCorner[0], T0EIDs[2]);

			// new tri[0] has verts (b, c, new) and edges (bc, cnew, newb)
			FIndex3i T1EIDs = GetTriEdges(PokeInfo.NewTriangles[0]);
			EdgeLengths.InsertAt(DistancesFromOldCorner[2], T1EIDs[1]);
		}
	}
	// (2) update internal angles for the triangles.
	{
		InternalAngles.InsertAt(ComputeTriInternalAnglesR(NewTris[2]), NewTris[2]);
		InternalAngles.InsertAt(ComputeTriInternalAnglesR(NewTris[1]), NewTris[1]);
		InternalAngles[NewTris[0]] = ComputeTriInternalAnglesR(NewTris[0]);
	}
	
	// (3) update the edge directions 
	FVector3d TriEdgeDir[3]; // one for each tri in order (TID, NewTris[0], NewTris[1] )
	// edges around the boundary of the original tri
	TriEdgeDir[0][0] = OriginalEdgeDirs[0]; // AtoB dir
	TriEdgeDir[1][0] = OriginalEdgeDirs[1]; // BtoC dir
	TriEdgeDir[2][0] = OriginalEdgeDirs[2]; // CtoA dir
	
	// edges from the corners of the original tri towards the new vertex at the "center"
	const double ToRadiansAtA = GeometricVertexInfo[OriginalVIDs[0]].ToRadians;
	const double ToRadiansAtB = GeometricVertexInfo[OriginalVIDs[1]].ToRadians;
	const double ToRadiansAtC = GeometricVertexInfo[OriginalVIDs[2]].ToRadians;
	TriEdgeDir[0][1] = AsZeroToTwoPi( OriginalEdgeDirs[1] + ToRadiansAtB * InternalAngles[NewTris[1]][0]);  // BtoNew dir
	TriEdgeDir[1][1] = AsZeroToTwoPi( OriginalEdgeDirs[2] + ToRadiansAtC * InternalAngles[NewTris[2]][0]);  // CtoNew dir
	TriEdgeDir[2][1] = AsZeroToTwoPi( OriginalEdgeDirs[0] + ToRadiansAtA * InternalAngles[NewTris[0]][0]);  // AtoNew dir   

	// edges from new vertex to a, to b, and to c, using new-to-A as the zero direction.  These will be updated later when we learn the correct angle for new-to-A
	TriEdgeDir[0][2] = 0.;                                                         // NewToA dir 
	TriEdgeDir[1][2] = InternalAngles[NewTris[0]][2];                                     // NewToB dir
	TriEdgeDir[2][2] = InternalAngles[NewTris[0]][2] + InternalAngles[NewTris[1]][2];     // NewToC dir
	GeometricVertexInfo.InsertAt(FGeometricInfo(), NewVID); // we want the default of false, and 1.

	// (4) update the surface point for the new vertex by tracing one of the new edges. 
	// 
	// --- fix the r3 position of the new vertex and compute its SurfacePoint Attributes by doing a trace on the Extrinsic Mesh along one of the edges incident on NewVID

	// Use first incident edge and find the distance and direction (angle) to trace
	// Q: would picking the shortest of the new edges be better than just the first?
	const int32 AtoNewEID = PokeInfo.NewEdges[0]; // a-to-new edge
	const double AtoNewDist = EdgeLengths[AtoNewEID];

	// trace from A vertex in the direction toward the new vertex the edge length distance. This updates the surface position entry for the new vert.
	const int32 AVID = OriginalVIDs[0];
	const double AtoNewDir = TriEdgeDir[2][1];
	const double NewToAAngle = UpdateVertexByEdgeTrace(NewVID, AVID, AtoNewDir, AtoNewDist);

	// update the Newto{A,B,C}  directions such that NewToA has the angle LocalEIDAngle.
	for (int32 e = 0; e < 3; ++e)
	{ 
		TriEdgeDir[e][2] = AsZeroToTwoPi(TriEdgeDir[e][2] + NewToAAngle);
	}
	
	// record the new edge directions.
	IntrinsicEdgeAngles.InsertAt(TriEdgeDir[2], NewTris[2]);
	IntrinsicEdgeAngles.InsertAt(TriEdgeDir[1], NewTris[1]);
	IntrinsicEdgeAngles[NewTris[0]] = TriEdgeDir[0];
	
	// record the coordinate actually used. 
	PokeInfo.BaryCoords = BaryCoordinates;

	return EMeshResult::Ok;
}


EMeshResult UE::Geometry::FIntrinsicTriangulation::SplitEdgeTopology(int32 eab, FEdgeSplitInfo& SplitInfo)
{
	SplitInfo = FEdgeSplitInfo();

	if (!IsEdge(eab))
	{
		return EMeshResult::Failed_NotAnEdge;
	}

	// look up primary edge & triangle
	const FEdge Edge = Edges[eab];
	const int32 t0 = Edge.Tri[0];
	if (t0 == InvalidID)
	{
		return EMeshResult::Failed_BrokenTopology;
	}
	const FIndex3i T0tv = GetTriangle(t0);
	const FIndex3i T0te = GetTriEdges(t0);
	const int32 IndexOfe = T0te.IndexOf(eab);
	const int32 a = T0tv[IndexOfe];
	const int32 b = T0tv[AddOneModThree[IndexOfe]];
	const int32 c = T0tv[AddTwoModThree[IndexOfe]];

	// look up edge bc, which needs to be modified
	const int32 ebc = T0te[AddOneModThree[IndexOfe]];

	// RefCount overflow check. Conservatively leave room for
	// extra increments from other operations.
	if (VertexRefCounts.GetRawRefCount(c) > FRefCountVector::INVALID_REF_COUNT - 3)
	{
		return EMeshResult::Failed_HitValenceLimit;
	}
	

	SplitInfo.OriginalEdge = eab;
	SplitInfo.OriginalVertices = FIndex2i(a, b);   // this is the oriented a,b
	SplitInfo.OriginalTriangles = FIndex2i(t0, InvalidID);
	SplitInfo.SplitT = 0.5;

	// quite a bit of code is duplicated between boundary and non-boundary case, but it
	//  is too hard to follow later if we factor it out...
	if (IsBoundaryEdge(eab))
	{
		// create vertex
		const FVector3d vNew = 0.5 * (GetVertex(a) + GetVertex(b));
		const int32 f = AppendVertex(vNew);
		

		// rewrite existing triangle
		Triangles[t0][AddOneModThree[IndexOfe]] =  f;

		// add second triangle
		const int32 t2 = AddTriangleInternal(f, b, c, InvalidID, InvalidID, InvalidID);
	

		// rewrite edge bc, create edge af
		ReplaceEdgeTriangle(ebc, t0, t2);
		const int32 eaf = eab;
		Edges[eaf].Vert = FIndex2i(FMath::Min(a,f), FMath::Max(a,f));
		//ReplaceEdgeVertex(eaf, b, f);
		if (a != b)
		{ 
			VertexEdgeLists.Remove(b, eab);
		}
		VertexEdgeLists.Insert(f, eaf);

		// create edges fb and fc
		const int32 efb = AddEdgeInternal(f, b, t2);
		const int32 efc = AddEdgeInternal(f, c, t0, t2);

		// update triangle edge-nbrs
		ReplaceTriangleEdge(t0, ebc, efc);
		TriangleEdges[t2] = FIndex3i(efb, ebc, efc);

		// update vertex refcounts
		VertexRefCounts.Increment(c);
		VertexRefCounts.Increment(f, 2);

		SplitInfo.bIsBoundary = true;
		SplitInfo.OtherVertices = FIndex2i(c, InvalidID);
		SplitInfo.NewVertex = f;
		SplitInfo.NewEdges = FIndex3i(efb, efc, InvalidID);
		SplitInfo.NewTriangles = FIndex2i(t2, InvalidID);

		return EMeshResult::Ok;

	}
	else 		// interior triangle branch
	{
		// look up other triangle
		const int32 t1 = Edges[eab].Tri[1];
		SplitInfo.OriginalTriangles.B = t1;
		const FIndex3i T1tv = GetTriangle(t1);
		const FIndex3i T1te = GetTriEdges(t1);
		const int32 T1IndexOfe = T1te.IndexOf(eab);
		checkSlow(T1tv[T1IndexOfe] == b);

		const int32 d = T1tv[AddTwoModThree[T1IndexOfe]];
		const int32 edb = T1te[AddTwoModThree[T1IndexOfe]];

		// RefCount overflow check. Conservatively leave room for
		// extra increments from other operations.
		if (VertexRefCounts.GetRawRefCount(d) > FRefCountVector::INVALID_REF_COUNT - 3)
		{
			return EMeshResult::Failed_HitValenceLimit;
		}

		// create vertex
		FVector3d vNew = 0.5 * (GetVertex(a) + GetVertex(b));
		int32 f = AppendVertex(vNew);


		// rewrite existing triangles, replacing b with f
		Triangles[t0][AddOneModThree[IndexOfe]] = f;
		Triangles[t1][T1IndexOfe] = f;
	

		// add two triangles to close holes we just created
		int32 t2 = AddTriangleInternal(f, b, c, InvalidID, InvalidID, InvalidID);
		int32 t3 = AddTriangleInternal(f, d, b, InvalidID, InvalidID, InvalidID);
		

		// update the edges we found above, to point to triangles
		ReplaceEdgeTriangle(ebc, t0, t2);
		ReplaceEdgeTriangle(edb, t1, t3);

		// edge eab became eaf
		int32 eaf = eab; //Edge * eAF = eAB;
		Edges[eaf].Vert = FIndex2i(FMath::Min(a, f), FMath::Max(a, f));

		// update a/b/f vertex-edges
		if (a != b)
		{ 
			VertexEdgeLists.Remove(b, eab);
		}
		VertexEdgeLists.Insert(f, eaf);

		// create edges connected to f  (also updates vertex-edges)
		int32 efb = AddEdgeInternal(f, b, t2, t3);
		int32 efc = AddEdgeInternal(f, c, t0, t2);
		int32 edf = AddEdgeInternal(d, f, t1, t3);

		// update triangle edge-nbrs
		ReplaceTriangleEdge(t0, ebc, efc);
		ReplaceTriangleEdge(t1, edb, edf);
		TriangleEdges[t2] = FIndex3i(efb, ebc, efc);
		TriangleEdges[t3] = FIndex3i(edf, edb, efb);

		// update vertex refcounts
		VertexRefCounts.Increment(c);
		VertexRefCounts.Increment(d);
		VertexRefCounts.Increment(f, 4);

		SplitInfo.bIsBoundary = false;
		SplitInfo.OtherVertices = FIndex2i(c, d);
		SplitInfo.NewVertex = f;
		SplitInfo.NewEdges = FIndex3i(efb, efc, edf);
		SplitInfo.NewTriangles = FIndex2i(t2, t3);

		return EMeshResult::Ok;
	}

}


UE::Geometry::EMeshResult  UE::Geometry::FIntrinsicTriangulation::SplitEdge(int32 EdgeAB, FEdgeSplitInfo& SplitInfo, double SplitParameterT)
{
	SplitParameterT = FMath::Clamp(SplitParameterT, 0., 1.);

	if (!IsEdge(EdgeAB))
	{
		return EMeshResult::Failed_NotAnEdge;
	}

	
	const FEdge OriginalEdge = GetEdge(EdgeAB);

	if (IsBoundaryEdge(EdgeAB))
	{
		// state before the split
		const int32 TID = OriginalEdge.Tri[0];
		const int32 IndexOfe = GetTriEdges(TID).IndexOf(EdgeAB);

		// Info about the original T0 tri, reordered to make the split edge the first edge..
		const FIndex3i OriginalT0VIDs  = Permute(IndexOfe, GetTriangle(TID));  // as (a, b, c)
		const FIndex3i OriginalT0Edges = Permute(IndexOfe, GetTriEdges(TID));  // as (ab, bc, ca)
		
		const FVector3d OriginalT0EdgeLengths = Permute(IndexOfe, GetTriEdgeLengths(TID));    // as (|ab|, |bc|, |ca|)
		const FVector3d OriginalT0EdgeDirs    = Permute(IndexOfe, IntrinsicEdgeAngles[TID]);  // as ( aTob, bToc, cToa)

		
		// Update the connectivity with the edge split
		EMeshResult Result = SplitEdgeTopology(EdgeAB, SplitInfo);

		if (Result != EMeshResult::Ok)
		{
			return Result;
		}
		const int32 NewVID = SplitInfo.NewVertex;
		const int32 NewTID = SplitInfo.NewTriangles[0]; // edges {fb, bc, cf}
		const int32 NewEdgeFB = SplitInfo.NewEdges[0];
		const int32 NewEdgeFC = SplitInfo.NewEdges[1];

		const double DistAF = SplitParameterT * OriginalT0EdgeLengths[0];
		const double DistFB = FMath::Max(0., OriginalT0EdgeLengths[0] - DistAF);
		// new edge length
		const double DistFC = DistanceBetweenBarycentricPoints( OriginalT0EdgeLengths[0],
			                                                    OriginalT0EdgeLengths[1],
																OriginalT0EdgeLengths[2],
			                                                    FVector3d(SplitParameterT, 1. - SplitParameterT, 0.), 
																FVector3d(0., 0., 1.));
		
		EdgeLengths.InsertAt(DistFC, NewEdgeFC);
		EdgeLengths.InsertAt(DistFB, NewEdgeFB);
		EdgeLengths[EdgeAB] = DistAF;
		
		// update the internal angles
		InternalAngles.InsertAt(ComputeTriInternalAnglesR(NewTID), NewTID);
		InternalAngles[TID] = ComputeTriInternalAnglesR(TID);
		const FVector3d TIDInternalAngles = Permute(IndexOfe, InternalAngles[TID]);
		
		// update the directions.  Say DirFtoB is zero
		const double ToRadiansAtC = GeometricVertexInfo[OriginalT0VIDs[2]].ToRadians;
		GeometricVertexInfo.InsertAt(FGeometricInfo(), NewVID); // we want the default of false, and 1.

		FVector3d TriEdgeDir[2]; // one for each tri in order (TID, NewTID )
		// edges around the boundary of the original tri
		TriEdgeDir[0][0] = OriginalT0EdgeDirs[0];    // AtoF dir
		TriEdgeDir[1][1] = OriginalT0EdgeDirs[1];    // BtoC dir
		TriEdgeDir[0][2] = OriginalT0EdgeDirs[2];    // CtoA dir  

		TriEdgeDir[1][0] = 0.;  // FtoB dir
		TriEdgeDir[1][2] = AsZeroToTwoPi(OriginalT0EdgeDirs[2] + ToRadiansAtC * TIDInternalAngles[2]);  // CtoF dir
		TriEdgeDir[0][1] = AsZeroToTwoPi(InternalAngles[NewTID][0]);                                    // FtoC dir relative to FtoB
		const double FtoADir = AsZeroToTwoPi(InternalAngles[NewTID][0] + TIDInternalAngles[1]);         // FtoA dir relative to FtoB
		// update the surface position of the new vertex and the relative directions from it.

		// trace from A vertex in the direction toward the new vertex the edge length distance. This updates the surface position entry for the new vert.
		const int32 AVID = OriginalT0VIDs[0];
		const double AtoFDir = TriEdgeDir[0][0];
		const double FToAAngle = UpdateVertexByEdgeTrace(NewVID, AVID, AtoFDir, DistAF);
		const double ToLocalDir = (FToAAngle - FtoADir); 
		// update the Fto{B,C}  directions such that the direction from F to A would agree with FtoAAngle
		TriEdgeDir[0][1] = AsZeroToTwoPi(TriEdgeDir[0][1] + ToLocalDir);
		TriEdgeDir[1][0] = AsZeroToTwoPi(TriEdgeDir[1][0] + ToLocalDir);
		
		// store angle results
		IntrinsicEdgeAngles.InsertAt(TriEdgeDir[1], NewTID);
		IntrinsicEdgeAngles[TID] = Permute((3 - IndexOfe)%3,  TriEdgeDir[0]);


		return Result;
	}
	else
	{
		// state before the split
		const int32 TID = OriginalEdge.Tri[0];
		const int32 IndexOfe = GetTriEdges(TID).IndexOf(EdgeAB);

		// Info about the original T0 tri, reordered to make the split edge the first edge..
		const FIndex3i OriginalT0VIDs  = Permute(IndexOfe, GetTriangle(TID));  // as (a, b, c)
		const FIndex3i OriginalT0Edges = Permute(IndexOfe, GetTriEdges(TID));  // as (ab, bc, ca)

		const FVector3d OriginalT0EdgeLengths = Permute(IndexOfe, GetTriEdgeLengths(TID));    // as (|ab|, |bc|, |ca|)
		const FVector3d OriginalT0EdgeDirs    = Permute(IndexOfe, IntrinsicEdgeAngles[TID]);  // as ( aTob, bToc, cToa)
		
		// Info about the original T0 tri, reordered to make the split edge the first edge..
		const int32 TID1 = OriginalEdge.Tri[1];
		const int32 IndexOfT1e = GetTriEdges(TID1).IndexOf(EdgeAB);

		const FIndex3i OriginalT1VIDs  = Permute(IndexOfT1e, GetTriangle(TID1));  // as (b, a, d)
		const FIndex3i OriginalT1Edges = Permute(IndexOfT1e, GetTriEdges(TID1));  // as (ba, ad, db)
		
		const FVector3d OriginalT1EdgeLengths = Permute(IndexOfT1e, GetTriEdgeLengths(TID1));   // as (|ba|, |ad|, |db})
		const FVector3d OriginalT1EdgeDirs    = Permute(IndexOfT1e, IntrinsicEdgeAngles[TID1]); // as (bToa, aTod, dTob) 
		

		// Update the connectivity with the edge split
		EMeshResult Result = SplitEdgeTopology(EdgeAB, SplitInfo);

		if (Result != EMeshResult::Ok)
		{
			return Result;
		}
		const int32 NewVID = SplitInfo.NewVertex;
		const int32 NewTID0 = SplitInfo.NewTriangles[0]; // edges {fb, bc, cf}
		const int32 NewTID1 = SplitInfo.NewTriangles[1]; // edges {fd, db, bc}
		const int32 EdgeAF = EdgeAB;
		const int32 NewEdgeFB = SplitInfo.NewEdges[0];
		const int32 NewEdgeFC = SplitInfo.NewEdges[1];
		const int32 NewEdgeFD = SplitInfo.NewEdges[2];

		const double DistAF = SplitParameterT * OriginalT0EdgeLengths[0];
		const double DistFB = FMath::Max(0., OriginalT0EdgeLengths[0] - DistAF);
		// new edge length
		const double DistFC = DistanceBetweenBarycentricPoints( OriginalT0EdgeLengths[0],
			                                                    OriginalT0EdgeLengths[1],
			                                                    OriginalT0EdgeLengths[2],
			                                                    FVector3d(SplitParameterT, 1. - SplitParameterT, 0.),
			                                                    FVector3d(0., 0., 1.));
		
		// new edge length
		const double DistFD = DistanceBetweenBarycentricPoints( OriginalT1EdgeLengths[0],
			                                                    OriginalT1EdgeLengths[1],
			                                                    OriginalT1EdgeLengths[2],
			                                                    FVector3d(1. - SplitParameterT, SplitParameterT, 0.),
			                                                    FVector3d(0., 0., 1.));

		EdgeLengths.InsertAt(DistFD, NewEdgeFD);
		EdgeLengths.InsertAt(DistFC, NewEdgeFC);
		EdgeLengths.InsertAt(DistFB, NewEdgeFB);
		EdgeLengths[EdgeAF] = DistAF;

		// update the internal angles (this uses edge lengths)
		InternalAngles.InsertAt(ComputeTriInternalAnglesR(NewTID1), NewTID1);
		InternalAngles.InsertAt(ComputeTriInternalAnglesR(NewTID0), NewTID0);
		InternalAngles[TID] = ComputeTriInternalAnglesR(TID);
		InternalAngles[TID1] = ComputeTriInternalAnglesR(TID1);

		// update the directions.  Say DirFtoB is zero
		const double ToRadiansAtC = GeometricVertexInfo[OriginalT0VIDs[2]].ToRadians;
		const double ToRadiansAtD = GeometricVertexInfo[OriginalT1VIDs[2]].ToRadians;
		GeometricVertexInfo.InsertAt(FGeometricInfo(), NewVID); // we want the default of false, and 1.

		const FVector3d TIDInternalAngles  = Permute(IndexOfe, InternalAngles[TID]);
		const FVector3d TID1InternalAngles = Permute(IndexOfT1e, InternalAngles[TID1]);

		FVector3d TriEdgeDir[4]; // one for each tri in order (TID, NewTID0, TID1,  NewTID1 )
		// edges around the boundary of the original tri
		TriEdgeDir[0][0] = OriginalT0EdgeDirs[0];    // AtoF dir
		TriEdgeDir[1][1] = OriginalT0EdgeDirs[1];    // BtoC dir
		TriEdgeDir[0][2] = OriginalT0EdgeDirs[2];    // CtoA dir  

		TriEdgeDir[3][2] = OriginalT1EdgeDirs[0];    // BtoF dir
		TriEdgeDir[2][1] = OriginalT1EdgeDirs[1];    // AtoD dir
		TriEdgeDir[3][1] = OriginalT1EdgeDirs[2];    // DtoB dir

		
		TriEdgeDir[1][2] = AsZeroToTwoPi(OriginalT0EdgeDirs[2] + ToRadiansAtC * TIDInternalAngles[2]);       // CtoF dir
		TriEdgeDir[2][2] = AsZeroToTwoPi(OriginalT1EdgeDirs[2] + ToRadiansAtD * InternalAngles[NewTID1][1]); // Dtof dir 

		TriEdgeDir[1][0] = 0.;                                                                          // FtoB dir
		TriEdgeDir[0][1] = AsZeroToTwoPi(InternalAngles[NewTID0][0]);                                   // FtoC dir relative to FtoB
		TriEdgeDir[2][0] = AsZeroToTwoPi(InternalAngles[NewTID0][0] + TIDInternalAngles[1]);            // FtoA dir relative to FtoB
		
		TriEdgeDir[3][0] = AsZeroToTwoPi(InternalAngles[NewTID0][0] + TIDInternalAngles[1] + TID1InternalAngles[0]); // FtoD dir relative to FtoB

		// trace from A vertex in the direction toward the new vertex the edge length distance. This updates the surface position entry for the new vert.
		const int32 AVID = OriginalT0VIDs[0];
		const double AtoFDir = TriEdgeDir[0][0];
		const double FToAAngle = UpdateVertexByEdgeTrace(NewVID, AVID, AtoFDir, DistAF);
		const double ToLocalDir = (FToAAngle - TriEdgeDir[2][0]);

		// fix angles relative to local dir.
		TriEdgeDir[1][0] = AsZeroToTwoPi(ToLocalDir);
		TriEdgeDir[0][1] = AsZeroToTwoPi(TriEdgeDir[0][1] + ToLocalDir);
		TriEdgeDir[2][0] = AsZeroToTwoPi(TriEdgeDir[2][0] + ToLocalDir);
		TriEdgeDir[3][0] = AsZeroToTwoPi(TriEdgeDir[3][0] + ToLocalDir);

		// store angle results
		IntrinsicEdgeAngles.InsertAt(TriEdgeDir[3], NewTID1);
		IntrinsicEdgeAngles.InsertAt(TriEdgeDir[1], NewTID0);
		IntrinsicEdgeAngles[TID]  = Permute((3 - IndexOfe) % 3, TriEdgeDir[0]);
		IntrinsicEdgeAngles[TID1] = Permute((3 - IndexOfT1e) % 3, TriEdgeDir[2]);

		return Result;
	}
}


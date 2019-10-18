// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

// Port of geometry3cpp TMeshAABBTree3

#pragma once

#include "Util/DynamicVector.h"
#include "Intersection/IntrRay3AxisAlignedBox3.h"
#include "MeshQueries.h"
#include "Spatial/SpatialInterfaces.h"
#include "Distance/DistTriangle3Triangle3.h"

template <class TriangleMeshType>
class TFastWindingTree;

template <class TriangleMeshType>
class TMeshAABBTree3 : public IMeshSpatial
{
	friend class TFastWindingTree<TriangleMeshType>;

protected:
	TriangleMeshType* Mesh;
	int MeshTimestamp = -1;
	int TopDownLeafMaxTriCount = 4;

public:
	static constexpr double DOUBLE_MAX = TNumericLimits<double>::Max();

	/**
	 * If non-null, only triangle IDs that pass this filter (ie filter is true) are considered
	 */
	TFunction<bool(int)> TriangleFilterF = nullptr;

	TMeshAABBTree3()
	{
		Mesh = nullptr;
	}

	TMeshAABBTree3(TriangleMeshType* SourceMesh, bool bAutoBuild = true)
	{
		SetMesh(SourceMesh, bAutoBuild);
	}

	void SetMesh(TriangleMeshType* SourceMesh, bool bAutoBuild = true)
	{
		Mesh = SourceMesh;
		MeshTimestamp = -1;

		if (bAutoBuild)
		{
			Build();
		}
	}

	const TriangleMeshType* GetMesh() const
	{
		return Mesh;
	}

	/**
	 * @return true if internal timestamp matches mesh timestamp
	 */
	bool IsValid() const 
	{
		if (MeshTimestamp == Mesh->GetShapeTimestamp())
		{
			check(RootIndex >= 0);
		}
		return MeshTimestamp == Mesh->GetShapeTimestamp();
	}

	void Build()
	{
		BuildTopDown(false);
		MeshTimestamp = Mesh->GetShapeTimestamp();
	}

	virtual bool SupportsNearestTriangle() override
	{
		return true;
	}

	/**
	 * Find the triangle closest to P, and distance to it, within distance MaxDist, or return InvalidID
	 * Use MeshQueries.TriangleDistance() to get more information
	 */
	virtual int FindNearestTriangle(const FVector3d& P, double& NearestDistSqr, double MaxDist = TNumericLimits<double>::Max()) override
	{
		check(MeshTimestamp == Mesh->GetShapeTimestamp());
		check(RootIndex >= 0);
		if (RootIndex < 0)
		{
			return IndexConstants::InvalidID;
		}

		NearestDistSqr = (MaxDist < DOUBLE_MAX) ? MaxDist * MaxDist : DOUBLE_MAX;
		int tNearID = IndexConstants::InvalidID;
		find_nearest_tri(RootIndex, P, NearestDistSqr, tNearID);
		return tNearID;
	}
	void find_nearest_tri(int IBox, const FVector3d& P, double& NearestDistSqr, int& TID)
	{
		int idx = BoxToIndex[IBox];
		if (idx < TrianglesEnd)
		{ // triangle-list case, array is [N t1 t2 ... tN]
			int num_tris = IndexList[idx];
			for (int i = 1; i <= num_tris; ++i)
			{
				int ti = IndexList[idx + i];
				if (TriangleFilterF != nullptr && TriangleFilterF(ti) == false)
				{
					continue;
				}
				double fTriDistSqr = TMeshQueries<TriangleMeshType>::TriDistanceSqr(*Mesh, ti, P);
				if (fTriDistSqr < NearestDistSqr)
				{
					NearestDistSqr = fTriDistSqr;
					TID = ti;
				}
			}
		}
		else
		{ // internal node, either 1 or 2 child boxes
			int iChild1 = IndexList[idx];
			if (iChild1 < 0)
			{ // 1 child, descend if nearer than cur min-dist
				iChild1 = (-iChild1) - 1;
				double fChild1DistSqr = BoxDistanceSqr(iChild1, P);
				if (fChild1DistSqr <= NearestDistSqr)
				{
					find_nearest_tri(iChild1, P, NearestDistSqr, TID);
				}
			}
			else
			{ // 2 children, descend closest first
				iChild1 = iChild1 - 1;
				int iChild2 = IndexList[idx + 1] - 1;

				double fChild1DistSqr = BoxDistanceSqr(iChild1, P);
				double fChild2DistSqr = BoxDistanceSqr(iChild2, P);
				if (fChild1DistSqr < fChild2DistSqr)
				{
					if (fChild1DistSqr < NearestDistSqr)
					{
						find_nearest_tri(iChild1, P, NearestDistSqr, TID);
						if (fChild2DistSqr < NearestDistSqr)
						{
							find_nearest_tri(iChild2, P, NearestDistSqr, TID);
						}
					}
				}
				else
				{
					if (fChild2DistSqr < NearestDistSqr)
					{
						find_nearest_tri(iChild2, P, NearestDistSqr, TID);
						if (fChild1DistSqr < NearestDistSqr)
						{
							find_nearest_tri(iChild1, P, NearestDistSqr, TID);
						}
					}
				}
			}
		}
	}

	virtual bool SupportsTriangleRayIntersection() override
	{
		return true;
	}

	virtual int FindNearestHitTriangle(const FRay3d& Ray, double MaxDist = TNumericLimits<double>::Max()) override
	{
		check(MeshTimestamp == Mesh->GetShapeTimestamp());
		check(RootIndex >= 0);
		if (RootIndex < 0)
		{
			return IndexConstants::InvalidID;
		}
		// TODO: check( ray_is_normalized)

		// [RMS] note: using float.MaxValue here because we need to use <= to compare Box hit
		//   to NearestT, and Box hit returns double.MaxValue on no-hit. So, if we set
		//   nearestT to double.MaxValue, then we will test all boxes (!)
		double NearestT = (MaxDist < TNumericLimits<double>::Max()) ? MaxDist : TNumericLimits<float>::Max();
		int tNearID = IndexConstants::InvalidID;
		FindHitTriangle(RootIndex, Ray, NearestT, tNearID);
		return tNearID;
	}

	void FindHitTriangle(int IBox, const FRay3d& Ray, double& NearestT, int& TID)
	{
		int idx = BoxToIndex[IBox];
		if (idx < TrianglesEnd)
		{ // triangle-list case, array is [N t1 t2 ... tN]
			FTriangle3d Triangle;
			int num_tris = IndexList[idx];
			for (int i = 1; i <= num_tris; ++i)
			{
				int ti = IndexList[idx + i];
				if (TriangleFilterF != nullptr && TriangleFilterF(ti) == false)
				{
					continue;
				}

				Mesh->GetTriVertices(ti, Triangle.V[0], Triangle.V[1], Triangle.V[2]);
				FIntrRay3Triangle3d Query = FIntrRay3Triangle3d(Ray, Triangle);
				if (Query.Find())
				{
					if (Query.RayParameter < NearestT)
					{
						NearestT = Query.RayParameter;
						TID = ti;
					}
				}
			}
		}
		else
		{ // internal node, either 1 or 2 child boxes
			double e = FMathd::ZeroTolerance;

			int iChild1 = IndexList[idx];
			if (iChild1 < 0)
			{ // 1 child, descend if nearer than cur min-dist
				iChild1 = (-iChild1) - 1;
				double fChild1T = box_ray_intersect_t(iChild1, Ray);
				if (fChild1T <= NearestT + e)
				{
					FindHitTriangle(iChild1, Ray, NearestT, TID);
				}
			}
			else
			{ // 2 children, descend closest first
				iChild1 = iChild1 - 1;
				int iChild2 = IndexList[idx + 1] - 1;

				double fChild1T = box_ray_intersect_t(iChild1, Ray);
				double fChild2T = box_ray_intersect_t(iChild2, Ray);
				if (fChild1T < fChild2T)
				{
					if (fChild1T <= NearestT + e)
					{
						FindHitTriangle(iChild1, Ray, NearestT, TID);
						if (fChild2T <= NearestT + e)
						{
							FindHitTriangle(iChild2, Ray, NearestT, TID);
						}
					}
				}
				else
				{
					if (fChild2T <= NearestT + e)
					{
						FindHitTriangle(iChild2, Ray, NearestT, TID);
						if (fChild1T <= NearestT + e)
						{
							FindHitTriangle(iChild1, Ray, NearestT, TID);
						}
					}
				}
			}
		}
	}

	/**
	 * Find nearest pair of triangles on this tree with otherTree, within max_dist.
	 * TransformF transforms vertices of otherTree into our coordinates. can be null.
	 * returns triangle-id pair (my_tri,other_tri), or Index2i::Invalid if not found within max_dist
	 * Use MeshQueries.TrianglesDistance() to get more information
	 */
	virtual FIndex2i FindNearestTriangles(TMeshAABBTree3& OtherTree, TFunction<FVector3d(const FVector3d&)> TransformF, double& Distance, double MaxDist = FMathd::MaxReal)
	{
		check(MeshTimestamp == Mesh->GetShapeTimestamp());
		check(RootIndex >= 0);
		if (RootIndex < 0)
		{
			return FIndex2i::Invalid();
		}

		double NearestSqr = FMathd::MaxReal;
		if (MaxDist < FMathd::MaxReal)
		{
			NearestSqr = MaxDist * MaxDist;
		}
		FIndex2i NearestPair = FIndex2i::Invalid();

		find_nearest_triangles(RootIndex, OtherTree, TransformF, OtherTree.RootIndex, 0, NearestSqr, NearestPair);
		Distance = (NearestSqr < FMathd::MaxReal) ? FMathd::Sqrt(NearestSqr) : FMathd::MaxReal;
		return NearestPair;
	}



	virtual bool SupportsPointContainment() override
	{
		return false;
	}

	virtual bool IsInside(const FVector3d& P) override
	{
		return false;
	}

	class FTreeTraversal
	{
	public:
		// return false to terminate this branch
		// arguments are Box and Depth in tree
		TFunction<bool(const FAxisAlignedBox3d&, int)> NextBoxF = [](const FAxisAlignedBox3d& Box, int Depth) { return true; };
		TFunction<void(int)> NextTriangleF = [](int TID) {};
	};

	/**
	 * Hierarchically descend through the tree Nodes, calling the TreeTrversal functions at each level
	 */
	virtual void DoTraversal(FTreeTraversal& Traversal)
	{
		check(MeshTimestamp == Mesh->GetShapeTimestamp());
		check(RootIndex >= 0);
		if (RootIndex < 0)
		{
			return;
		}

		TreeTraversalImpl(RootIndex, 0, Traversal);
	}

	// Traversal implementation. you can override to customize this if necessary.
	virtual void TreeTraversalImpl(int IBox, int Depth, FTreeTraversal& Traversal)
	{
		int idx = BoxToIndex[IBox];

		if (idx < TrianglesEnd)
		{
			// triangle-list case, array is [N t1 t2 ... tN]
			int n = IndexList[idx];
			for (int i = 1; i <= n; ++i)
			{
				int ti = IndexList[idx + i];
				if (TriangleFilterF != nullptr && TriangleFilterF(ti) == false)
				{
					continue;
				}
				Traversal.NextTriangleF(ti);
			}
		}
		else
		{
			int i0 = IndexList[idx];
			if (i0 < 0)
			{
				// negative index means we only have one 'child' Box to descend into
				i0 = (-i0) - 1;
				if (Traversal.NextBoxF(GetBox(i0), Depth + 1))
				{
					TreeTraversalImpl(i0, Depth + 1, Traversal);
				}
			}
			else
			{
				// positive index, two sequential child Box indices to descend into
				i0 = i0 - 1;
				if (Traversal.NextBoxF(GetBox(i0), Depth + 1))
				{
					TreeTraversalImpl(i0, Depth + 1, Traversal);
				}
				int i1 = IndexList[idx + 1] - 1;
				if (Traversal.NextBoxF(GetBox(i1), Depth + 1))
				{
					TreeTraversalImpl(i1, Depth + 1, Traversal);
				}
			}
		}
	}

protected:
	//
	// Internals - data structures, construction, etc
	//

	FAxisAlignedBox3d GetBox(int IBox) const
	{
		const FVector3d& c = BoxCenters[IBox];
		const FVector3d& e = BoxExtents[IBox];
		FVector3d Min = c - e, Max = c + e;
		return FAxisAlignedBox3d(Min, Max);
	}
	FAxisAlignedBox3d GetBox(int iBox, TFunction<FVector3d(const FVector3d&)> TransformF)
	{
		if (TransformF != nullptr)
		{
			FAxisAlignedBox3d box = GetBox(iBox);
			return FAxisAlignedBox3d(box, TransformF);
		}
		else
		{
			return GetBox(iBox);
		}
	}

	FAxisAlignedBox3d GetBoxEps(int IBox, double Epsilon = FMathd::ZeroTolerance) const
	{
		const FVector3d& c = BoxCenters[IBox];
		FVector3d e = BoxExtents[IBox];
		e[0] += Epsilon;
		e[1] += Epsilon;
		e[2] += Epsilon;
		FVector3d Min = c - e, Max = c + e;
		return FAxisAlignedBox3d(Min, Max);
	}

	const double BoxEps = FMathd::ZeroTolerance;

	double BoxDistanceSqr(int IBox, const FVector3d& V) const
	{
		const FVector3d& c = BoxCenters[IBox];
		const FVector3d& e = BoxExtents[IBox];

		// per-axis delta is max(abs(P-c) - e, 0)... ?
		double dx = FMath::Max(fabs(V.X - c.X) - e.X, 0.0);
		double dy = FMath::Max(fabs(V.Y - c.Y) - e.Y, 0.0);
		double dz = FMath::Max(fabs(V.Z - c.Z) - e.Z, 0.0);
		return dx * dx + dy * dy + dz * dz;
	}

	bool box_contains(int IBox, const FVector3d& P) const
	{
		FAxisAlignedBox3d Box = GetBoxEps(IBox, BoxEps);
		return Box.Contains(P);
	}

	double box_ray_intersect_t(int IBox, const FRay3d& Ray) const
	{
		const FVector3d& c = BoxCenters[IBox];
		FVector3d e = BoxExtents[IBox] + BoxEps;
		FAxisAlignedBox3d Box(c - e, c + e);

		double ray_t = TNumericLimits<double>::Max();
		if (FIntrRay3AxisAlignedBox3d::FindIntersection(Ray, Box, ray_t))
		{
			return ray_t;
		}
		else
		{
			return TNumericLimits<double>::Max();
		}
	}

	// storage for Box Nodes.
	//   - BoxToIndex is a pointer into IndexList
	//   - BoxCenters and BoxExtents are the Centers/extents of the bounding boxes
	TDynamicVector<int> BoxToIndex;
	TDynamicVector<FVector3d> BoxCenters;
	TDynamicVector<FVector3d> BoxExtents;

	// list of indices for a given Box. There is *no* marker/sentinel between
	// boxes, you have to get the starting index from BoxToIndex[]
	//
	// There are three kinds of records:
	//   - if i < TrianglesEnd, then the list is a number of Triangles,
	//       stored as [N t1 t2 t3 ... tN]
	//   - if i > TrianglesEnd and IndexList[i] < 0, this is a single-child
	//       internal Box, with index (-IndexList[i])-1     (shift-by-one in case actual value is 0!)
	//   - if i > TrianglesEnd and IndexList[i] > 0, this is a two-child
	//       internal Box, with indices IndexList[i]-1 and IndexList[i+1]-1
	TDynamicVector<int> IndexList;

	// IndexList[i] for i < TrianglesEnd is a triangle-index list, otherwise Box-index pair/single
	int TrianglesEnd = -1;

	// BoxToIndex[RootIndex] is the root node of the tree
	int RootIndex = -1;

	struct FBoxesSet
	{
		TDynamicVector<int> BoxToIndex;
		TDynamicVector<FVector3d> BoxCenters;
		TDynamicVector<FVector3d> BoxExtents;
		TDynamicVector<int> IndexList;
		int IBoxCur;
		int IIndicesCur;
		FBoxesSet()
		{
			IBoxCur = 0;
			IIndicesCur = 0;
		}
	};

	void BuildTopDown(bool bSorted)
	{
		// build list of valid Triangles & Centers. We skip any
		// Triangles that have infinite/garbage vertices...
		int i = 0;
		TArray<int> Triangles;
		Triangles.SetNumUninitialized(Mesh->TriangleCount());
		TArray<FVector3d> Centers;
		Centers.SetNumUninitialized(Mesh->TriangleCount());
		for (int ti = 0; ti < Mesh->MaxTriangleID(); ti++)
		{
			if (!Mesh->IsTriangle(ti))
			{
				continue;
			}
			FVector3d centroid = TMeshQueries<TriangleMeshType>::GetTriCentroid(*Mesh, ti);
			double d2 = centroid.SquaredLength();
			bool bInvalid = FMathd::IsNaN(d2) || (FMathd::IsFinite(d2) == false);
			check(bInvalid == false);
			if (bInvalid == false)
			{
				Triangles[i] = ti;
				Centers[i] = TMeshQueries<TriangleMeshType>::GetTriCentroid(*Mesh, ti);
				i++;
			} // otherwise skip this tri
		}

		FBoxesSet Tris;
		FBoxesSet Nodes;
		FAxisAlignedBox3d rootBox;
		int rootnode =
			//(bSorted) ? split_tri_set_sorted(Triangles, Centers, 0, Mesh->TriangleCount, 0, TopDownLeafMaxTriCount, Tris, Nodes, out rootBox) :
			SplitTriSetMidpoint(Triangles, Centers, 0, Mesh->TriangleCount(), 0, TopDownLeafMaxTriCount, Tris, Nodes, rootBox);

		BoxToIndex = Tris.BoxToIndex;
		BoxCenters = Tris.BoxCenters;
		BoxExtents = Tris.BoxExtents;
		IndexList = Tris.IndexList;
		TrianglesEnd = Tris.IIndicesCur;
		int iIndexShift = TrianglesEnd;
		int iBoxShift = Tris.IBoxCur;

		// ok now append internal node boxes & index ptrs
		for (i = 0; i < Nodes.IBoxCur; ++i)
		{
			FVector3d NodeBoxCenter = Nodes.BoxCenters[i];		// cannot pass as argument in case a resize happens
			BoxCenters.InsertAt(NodeBoxCenter, iBoxShift + i);
			FVector3d NodeBoxExtents = Nodes.BoxExtents[i];
			BoxExtents.InsertAt(NodeBoxExtents, iBoxShift + i);
			// internal node indices are shifted
			int NodeBoxIndex = Nodes.BoxToIndex[i];
			BoxToIndex.InsertAt(iIndexShift + NodeBoxIndex, iBoxShift + i);
		}

		// now append index list
		for (i = 0; i < Nodes.IIndicesCur; ++i)
		{
			int child_box = Nodes.IndexList[i];
			if (child_box < 0)
			{ // this is a Triangles Box
				child_box = (-child_box) - 1;
			}
			else
			{
				child_box += iBoxShift;
			}
			child_box = child_box + 1;
			IndexList.InsertAt(child_box, iIndexShift + i);
		}

		RootIndex = rootnode + iBoxShift;
	}

	int SplitTriSetMidpoint(
		TArray<int>& Triangles,
		TArray<FVector3d>& Centers,
		int IStart, int ICount, int Depth, int MinTriCount,
		FBoxesSet& Tris, FBoxesSet& Nodes, FAxisAlignedBox3d& Box)
	{
		Box = FAxisAlignedBox3d::Empty();
		int IBox = -1;

		if (ICount < MinTriCount)
		{
			// append new Triangles Box
			IBox = Tris.IBoxCur++;
			Tris.BoxToIndex.InsertAt(Tris.IIndicesCur, IBox);

			Tris.IndexList.InsertAt(ICount, Tris.IIndicesCur++);
			for (int i = 0; i < ICount; ++i)
			{
				Tris.IndexList.InsertAt(Triangles[IStart + i], Tris.IIndicesCur++);
				Box.Contain(TMeshQueries<TriangleMeshType>::GetTriBounds(*Mesh, Triangles[IStart + i]));
			}

			Tris.BoxCenters.InsertAt(Box.Center(), IBox);
			Tris.BoxExtents.InsertAt(Box.Extents(), IBox);

			return -(IBox + 1);
		}

		//compute interval along an axis and find midpoint
		int axis = Depth % 3;
		FInterval1d interval = FInterval1d::Empty();
		for (int i = 0; i < ICount; ++i)
		{
			interval.Contain(Centers[IStart + i][axis]);
		}
		double midpoint = interval.Center();

		int n0, n1;
		if (interval.Length() > FMathd::ZeroTolerance)
		{
			// we have to re-sort the Centers & Triangles lists so that Centers < midpoint
			// are first, so that we can recurse on the two subsets. We walk in from each side,
			// until we find two out-of-order locations, then we swap them.
			int l = 0;
			int r = ICount - 1;
			while (l < r)
			{
				// [RMS] is <= right here? if V.axis == midpoint, then this loop
				//   can get stuck unless one of these has an equality test. But
				//   I did not think enough about if this is the right thing to do...
				while (Centers[IStart + l][axis] <= midpoint)
				{
					l++;
				}
				while (Centers[IStart + r][axis] > midpoint)
				{
					r--;
				}
				if (l >= r)
				{
					break; //done!
						   //swap
				}
				FVector3d tmpc = Centers[IStart + l];
				Centers[IStart + l] = Centers[IStart + r];
				Centers[IStart + r] = tmpc;
				int tmpt = Triangles[IStart + l];
				Triangles[IStart + l] = Triangles[IStart + r];
				Triangles[IStart + r] = tmpt;
			}

			n0 = l;
			n1 = ICount - n0;
			check(n0 >= 1 && n1 >= 1);
		}
		else
		{
			// interval is near-empty, so no point trying to do sorting, just split half and half
			n0 = ICount / 2;
			n1 = ICount - n0;
		}

		// create child boxes
		FAxisAlignedBox3d box1;
		int child0 = SplitTriSetMidpoint(Triangles, Centers, IStart, n0, Depth + 1, MinTriCount, Tris, Nodes, Box);
		int child1 = SplitTriSetMidpoint(Triangles, Centers, IStart + n0, n1, Depth + 1, MinTriCount, Tris, Nodes, box1);
		Box.Contain(box1);

		// append new Box
		IBox = Nodes.IBoxCur++;
		Nodes.BoxToIndex.InsertAt(Nodes.IIndicesCur, IBox);

		Nodes.IndexList.InsertAt(child0, Nodes.IIndicesCur++);
		Nodes.IndexList.InsertAt(child1, Nodes.IIndicesCur++);

		Nodes.BoxCenters.InsertAt(Box.Center(), IBox);
		Nodes.BoxExtents.InsertAt(Box.Extents(), IBox);

		return IBox;
	}


	void find_nearest_triangles(int iBox, TMeshAABBTree3& otherTree, TFunction<FVector3d(const FVector3d&)> TransformF, int oBox, int depth, double &nearest_sqr, FIndex2i &nearest_pair)
	{
		int idx = BoxToIndex[iBox];
		int odx = otherTree.BoxToIndex[oBox];

		if (idx < TrianglesEnd && odx < otherTree.TrianglesEnd)
		{
			// ok we are at triangles for both trees, do triangle-level testing
			FTriangle3d tri, otri;
			int num_tris = IndexList[idx], onum_tris = otherTree.IndexList[odx];

			FDistTriangle3Triangle3d dist;

			// outer iteration is "other" tris that need to be transformed (more expensive)
			for (int j = 1; j <= onum_tris; ++j)
			{
				int tj = otherTree.IndexList[odx + j];
				if (otherTree.TriangleFilterF != nullptr && otherTree.TriangleFilterF(tj) == false)
				{
					continue;
				}
				otherTree.Mesh->GetTriVertices(tj, otri.V[0], otri.V[1], otri.V[2]);
				if (TransformF != nullptr)
				{
					otri.V[0] = TransformF(otri.V[0]);
					otri.V[1] = TransformF(otri.V[1]);
					otri.V[2] = TransformF(otri.V[2]);
				}
				dist.Triangle[0] = otri;

				// inner iteration over "our" triangles
				for (int i = 1; i <= num_tris; ++i)
				{
					int ti = IndexList[idx + i];
					if (TriangleFilterF != nullptr && TriangleFilterF(ti) == false)
					{
						continue;
					}
					Mesh->GetTriVertices(ti, tri.V[0], tri.V[1], tri.V[2]);
					dist.Triangle[1] = tri;
					double dist_sqr = dist.GetSquared();
					if (dist_sqr < nearest_sqr)
					{
						nearest_sqr = dist_sqr;
						nearest_pair = FIndex2i(ti, tj);
					}
				}
			}

			return;
		}

		// we either descend "our" tree or the other tree
		//   - if we have hit triangles on "our" tree, we have to descend other
		//   - if we hit triangles on "other", we have to descend ours
		//   - otherwise, we alternate at each depth. This produces wider
		//     branching but is significantly faster (~10x) for both hits and misses
		bool bDescendOther = (idx < TrianglesEnd || depth % 2 == 0);
		if (bDescendOther && odx < otherTree.TrianglesEnd)
		{
			bDescendOther = false;      // can't
		}

		if (bDescendOther)
		{
			// ok we reached triangles on our side but we need to still reach triangles on
			// the other side, so we descend "their" children
			FAxisAlignedBox3d bounds = GetBox(iBox);

			int oChild1 = otherTree.IndexList[odx];
			if (oChild1 < 0)		// 1 child, descend if nearer than cur min-dist
			{
				oChild1 = (-oChild1) - 1;
				FAxisAlignedBox3d oChild1Box = otherTree.GetBox(oChild1, TransformF);
				if (oChild1Box.DistanceSquared(bounds) < nearest_sqr)
				{
					find_nearest_triangles(iBox, otherTree, TransformF, oChild1, depth + 1, nearest_sqr, nearest_pair);
				}
			}
			else                            // 2 children
			{
				oChild1 = oChild1 - 1;
				int oChild2 = otherTree.IndexList[odx + 1] - 1;

				FAxisAlignedBox3d oChild1Box = otherTree.GetBox(oChild1, TransformF);
				FAxisAlignedBox3d oChild2Box = otherTree.GetBox(oChild2, TransformF);

				// descend closer box first
				double d1Sqr = oChild1Box.DistanceSquared(bounds);
				double d2Sqr = oChild2Box.DistanceSquared(bounds);
				if (d2Sqr < d1Sqr)
				{
					if (d2Sqr < nearest_sqr)
					{
						find_nearest_triangles(iBox, otherTree, TransformF, oChild2, depth + 1, nearest_sqr, nearest_pair);
					}
					if (d1Sqr < nearest_sqr)
					{
						find_nearest_triangles(iBox, otherTree, TransformF, oChild1, depth + 1, nearest_sqr, nearest_pair);
					}
				}
				else
				{
					if (d1Sqr < nearest_sqr)
					{
						find_nearest_triangles(iBox, otherTree, TransformF, oChild1, depth + 1, nearest_sqr, nearest_pair);
					}
					if (d2Sqr < nearest_sqr)
					{
						find_nearest_triangles(iBox, otherTree, TransformF, oChild2, depth + 1, nearest_sqr, nearest_pair);
					}
				}
			}
		}
		else
		{
			// descend our tree nodes if they intersect w/ current bounds of other tree
			FAxisAlignedBox3d oBounds = otherTree.GetBox(oBox, TransformF);

			int iChild1 = IndexList[idx];
			if (iChild1 < 0)                  // 1 child, descend if nearer than cur min-dist
			{
				iChild1 = (-iChild1) - 1;
				if (box_box_distsqr(iChild1, oBounds) < nearest_sqr)
				{
					find_nearest_triangles(iChild1, otherTree, TransformF, oBox, depth + 1, nearest_sqr, nearest_pair);
				}
			}
			else                             // 2 children
			{
				iChild1 = iChild1 - 1;
				int iChild2 = IndexList[idx + 1] - 1;

				// descend closer box first
				double d1Sqr = box_box_distsqr(iChild1, oBounds);
				double d2Sqr = box_box_distsqr(iChild2, oBounds);
				if (d2Sqr < d1Sqr)
				{
					if (d2Sqr < nearest_sqr)
					{
						find_nearest_triangles(iChild2, otherTree, TransformF, oBox, depth + 1, nearest_sqr, nearest_pair);
					}
					if (d1Sqr < nearest_sqr)
					{
						find_nearest_triangles(iChild1, otherTree, TransformF, oBox, depth + 1, nearest_sqr, nearest_pair);
					}
				}
				else
				{
					if (d1Sqr < nearest_sqr)
					{
						find_nearest_triangles(iChild1, otherTree, TransformF, oBox, depth + 1, nearest_sqr, nearest_pair);
					}
					if (d2Sqr < nearest_sqr)
					{
						find_nearest_triangles(iChild2, otherTree, TransformF, oBox, depth + 1, nearest_sqr, nearest_pair);
					}
				}
			}
		}
	}

	double box_box_distsqr(int iBox, const FAxisAlignedBox3d& testBox)
	{
		// [TODO] could compute this w/o constructing box
		FAxisAlignedBox3d box = GetBoxEps(iBox, BoxEps);
		return box.DistanceSquared(testBox);
	}



public:
	// 1) make sure we can reach every tri in Mesh through tree (also demo of how to traverse tree...)
	// 2) make sure that Triangles are contained in parent boxes
	void TestCoverage()
	{
		TArray<int> tri_counts;
		tri_counts.SetNumZeroed(Mesh->MaxTriangleID());

		TArray<int> parent_indices;
		parent_indices.SetNumZeroed(BoxToIndex.GetLength());

		test_coverage(tri_counts, parent_indices, RootIndex);

		for (int ti = 0; ti < Mesh->MaxTriangleID(); ti++)
		{
			if (!Mesh->IsTriangle(ti))
			{
				continue;
			}
			check(tri_counts[ti] == 1);
		}
	}

	/**
	 * Total sum of volumes of all boxes in the tree. Mainly useful to evaluate tree quality.
	 */
	double TotalVolume()
	{
		double volSum = 0;
		FTreeTraversal t;
		t.NextBoxF = [&](const FAxisAlignedBox3d& Box, int)
		{
			volSum += Box.Volume();
			return true;
		};
		DoTraversal(t);
		return volSum;
	}

private:
	// accumulate triangle counts and track each Box-parent index.
	// also checks that Triangles are contained in boxes
	void test_coverage(TArray<int>& tri_counts, TArray<int>& parent_indices, int IBox)
	{
		int idx = BoxToIndex[IBox];

		debug_check_child_tris_in_box(IBox);

		if (idx < TrianglesEnd)
		{
			// triangle-list case, array is [N t1 t2 ... tN]
			int n = IndexList[idx];
			FAxisAlignedBox3d Box = GetBoxEps(IBox);
			for (int i = 1; i <= n; ++i)
			{
				int ti = IndexList[idx + i];
				tri_counts[ti]++;

				FIndex3i tv = Mesh->GetTriangle(ti);
				for (int j = 0; j < 3; ++j)
				{
					FVector3d V = Mesh->GetVertex(tv[j]);
					check(Box.Contains(V));
				}
			}
		}
		else
		{
			int i0 = IndexList[idx];
			if (i0 < 0)
			{
				// negative index means we only have one 'child' Box to descend into
				i0 = (-i0) - 1;
				parent_indices[i0] = IBox;
				test_coverage(tri_counts, parent_indices, i0);
			}
			else
			{
				// positive index, two sequential child Box indices to descend into
				i0 = i0 - 1;
				parent_indices[i0] = IBox;
				test_coverage(tri_counts, parent_indices, i0);
				int i1 = IndexList[idx + 1];
				i1 = i1 - 1;
				parent_indices[i1] = IBox;
				test_coverage(tri_counts, parent_indices, i1);
			}
		}
	}
	// do full tree Traversal below IBox and make sure that all Triangles are further
	// than Box-distance-sqr
	void debug_check_child_tri_distances(int IBox, const FVector3d& P)
	{
		double fBoxDistSqr = BoxDistanceSqr(IBox, P);

		// [TODO]
		FTreeTraversal t;
		t.NextTriangleF = [&](int TID)
		{
			double fTriDistSqr = TMeshQueries<TriangleMeshType>::TriDistanceSqr(*Mesh, TID, P);
			if (fTriDistSqr < fBoxDistSqr)
			{
				check(fabs(fTriDistSqr - fBoxDistSqr) <= FMathd::ZeroTolerance * 100);
			}
		};
		TreeTraversalImpl(IBox, 0, t);
	}

	// do full tree Traversal below IBox to make sure that all child Triangles are contained
	void debug_check_child_tris_in_box(int IBox)
	{
		FAxisAlignedBox3d Box = GetBoxEps(IBox);
		FTreeTraversal t;
		t.NextTriangleF = [&](int TID)
		{
			FIndex3i tv = Mesh->GetTriangle(TID);
			for (int j = 0; j < 3; ++j)
			{
				FVector3d V = Mesh->GetVertex(tv[j]);
				check(Box.Contains(V));
			}
		};
		TreeTraversalImpl(IBox, 0, t);
	}
};

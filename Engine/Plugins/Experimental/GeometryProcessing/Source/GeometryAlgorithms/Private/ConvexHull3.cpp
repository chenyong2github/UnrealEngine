// Copyright Epic Games, Inc. All Rights Reserved.

// Adaptation/Port of "ThirdParty/GTEngine/Mathematics/GteConvexHull3.h"

#include "ConvexHull3.h"
#include "ExactPredicates.h"

#include "Async/ParallelFor.h"




/**
 * Helper class to find the dimensions spanned by a point cloud
 * and (if it spans 3 dimensions) the indices of four 'extreme' points
 * forming a (non-degenerate, volume > 0) tetrahedron
 *
 * The extreme points are chosen to be far apart, and are used as the starting point for
 * incremental convex hull construction.
 * Construction method is adapted from GteVector3.h's IntrinsicsVector3
 */
template<typename RealType>
struct TExtremePoints3
{
	int Dimension = 0;
	int Extreme[4]{ 0, 0, 0, 0 };

	// Coordinate frame spanned by input points
	FVector3<RealType> Origin{ 0,0,0 };
	FVector3<RealType> Basis[3]{ {0,0,0}, {0,0,0}, {0,0,0} };

	TExtremePoints3(int32 NumPoints, TFunctionRef<FVector3<RealType>(int32)> GetPointFunc, TFunctionRef<bool(int32)> FilterFunc, double Epsilon = 0)
	{
		FVector3<RealType> FirstPoint;
		int FirstPtIdx = -1;
		for (FirstPtIdx = 0; FirstPtIdx < NumPoints; FirstPtIdx++)
		{
			if (FilterFunc(FirstPtIdx))
			{
				FirstPoint = GetPointFunc(FirstPtIdx);
				break;
			}
		}
		if (FirstPtIdx == -1)
		{
			// no points passed filter
			Dimension = 0;
			return;
		}

		FVector3<RealType> Min = GetPointFunc(FirstPtIdx), Max = GetPointFunc(FirstPtIdx);
		FIndex3i IndexMin(FirstPtIdx, FirstPtIdx, FirstPtIdx), IndexMax(FirstPtIdx, FirstPtIdx, FirstPtIdx);
		for (int Idx = FirstPtIdx + 1; Idx < NumPoints; Idx++)
		{
			if (!FilterFunc(Idx))
			{
				continue;
			}
			for (int Dim = 0; Dim < 3; Dim++)
			{
				RealType Val = GetPointFunc(Idx)[Dim];
				if (Val < Min[Dim])
				{
					Min[Dim] = Val;
					IndexMin[Dim] = Idx;
				}
				else if (Val > Max[Dim])
				{
					Max[Dim] = Val;
					IndexMax[Dim] = Idx;
				}
			}
		}

		RealType MaxRange = Max[0] - Min[0];
		int MaxRangeDim = 0;
		for (int Dim = 1; Dim < 3; Dim++)
		{
			RealType Range = Max[Dim] - Min[Dim];
			if (Range > MaxRange)
			{
				MaxRange = Range;
				MaxRangeDim = Dim;
			}
		}
		Extreme[0] = IndexMin[MaxRangeDim];
		Extreme[1] = IndexMax[MaxRangeDim];

		// all points within box of Epsilon extent; Dimension must be 0
		if (MaxRange <= Epsilon)
		{
			Dimension = 0;
			Extreme[3] = Extreme[2] = Extreme[1] = Extreme[0];
			return;
		}

		Origin = GetPointFunc(Extreme[0]);
		Basis[0] = GetPointFunc(Extreme[1]) - Origin;
		Basis[0].Normalize();

		// find point furthest from the line formed by the first two extreme points
		{
			TLine3<RealType> Basis0Line(Origin, Basis[0]);
			RealType MaxDistSq = 0;
			for (int Idx = FirstPtIdx; Idx < NumPoints; Idx++)
			{
				if (!FilterFunc(Idx))
				{
					continue;
				}
				RealType DistSq = Basis0Line.DistanceSquared(GetPointFunc(Idx));
				if (DistSq > MaxDistSq)
				{
					MaxDistSq = DistSq;
					Extreme[2] = Idx;
				}
			}

			// Nearly collinear points
			if (TMathUtil<RealType>::Sqrt(MaxDistSq) <= Epsilon * MaxRange)
			{
				Dimension = 1;
				Extreme[3] = Extreme[2] = Extreme[1];
				return;
			}
		}


		Basis[1] = GetPointFunc(Extreme[2]) - Origin;
		// project Basis[1] to be orthogonal to Basis[0]
		Basis[1] -= (Basis[0].Dot(Basis[1])) * Basis[0];
		Basis[1].Normalize();
		Basis[2] = Basis[0].Cross(Basis[1]);

		{
			TPlane3<RealType> Plane(Basis[2], Origin);
			RealType MaxDist = 0, MaxSign = 0;
			for (int Idx = FirstPtIdx; Idx < NumPoints; Idx++)
			{
				if (!FilterFunc(Idx))
				{
					continue;
				}
				RealType DistSigned = Plane.DistanceTo(GetPointFunc(Idx));
				RealType Dist = TMathUtil<RealType>::Abs(DistSigned);
				if (Dist > MaxDist)
				{
					MaxDist = Dist;
					MaxSign = TMathUtil<RealType>::Sign(DistSigned);
					Extreme[3] = Idx;
				}
			}

			// Nearly coplanar points
			if (MaxDist <= Epsilon * MaxRange)
			{
				Dimension = 2;
				Extreme[3] = Extreme[2];
				return;
			}

			// make sure the tetrahedron is CW-oriented
			if (MaxSign > 0)
			{
				Swap(Extreme[3], Extreme[2]);
			}
		}

		Dimension = 3;
	}

};





template<class RealType>
bool TConvexHull3<RealType>::Solve(int32 NumPoints, TFunctionRef<FVector3<RealType>(int32)> GetPointFunc, TFunctionRef<bool(int32)> FilterFunc)
{
	Hull.Reset();

	TExtremePoints3<RealType> InitialTet(NumPoints, GetPointFunc, FilterFunc);
	Dimension = InitialTet.Dimension;
	if (Dimension < 3)
	{
		if (Dimension == 1)
		{
			Line = TLine3<RealType>(InitialTet.Origin, InitialTet.Basis[0]);
		}
		else if (Dimension == 2)
		{
			Plane = TPlane3<RealType>(InitialTet.Basis[1], InitialTet.Origin);
		}
		return false;
	}

	// safety check; seems possible the InitialTet chosen points were actually coplanar, because it was constructed w/ inexact math
	if (ExactPredicates::Orient3D(GetPointFunc(InitialTet.Extreme[0]), GetPointFunc(InitialTet.Extreme[1]), GetPointFunc(InitialTet.Extreme[2]), GetPointFunc(InitialTet.Extreme[3])) == 0)
	{
		Plane = TPlane3<RealType>(InitialTet.Basis[1], InitialTet.Origin);
		Dimension = 2;
		return false;
	}

	// Add triangles from InitialTet
	Hull.Add(FIndex3i(InitialTet.Extreme[1], InitialTet.Extreme[2], InitialTet.Extreme[3]));
	Hull.Add(FIndex3i(InitialTet.Extreme[0], InitialTet.Extreme[3], InitialTet.Extreme[2]));
	Hull.Add(FIndex3i(InitialTet.Extreme[0], InitialTet.Extreme[1], InitialTet.Extreme[3]));
	Hull.Add(FIndex3i(InitialTet.Extreme[0], InitialTet.Extreme[2], InitialTet.Extreme[1]));

	// The set of processed points is maintained to eliminate exact duplicates
	//  (should not be needed for correctness but lets us count unique points and is probably faster)
	TSet<FVector3<RealType>> Processed;
	for (int i = 0; i < 4; ++i)
	{
		Processed.Add(GetPointFunc(InitialTet.Extreme[i]));
	}
	// Incrementally update the hull.  
	for (int i = 0; i < NumPoints; ++i)
	{
		if (FilterFunc(i) && !Processed.Contains(GetPointFunc(i)))
		{
			Insert(GetPointFunc, i);
			Processed.Add(GetPointFunc(i));
		}
	}
	NumUniquePoints = Processed.Num();

	return true;
}


template<class RealType>
void TConvexHull3<RealType>::Insert(TFunctionRef<FVector3<RealType>(int32)> GetPointFunc, int32 PtIdx)
{
	TArray<int> PtSides; PtSides.SetNum(Hull.Num());
	FVector3d Pt = (FVector3d)GetPointFunc(PtIdx); // double precision point to use double precision plane test
	bool bSingleThread = true; // TODO: have yet to find a dataset where the ParallelFor actually helps -- maybe if Hull.Num() gets very large?
	ParallelFor(Hull.Num(), [this, &GetPointFunc, &PtSides, Pt](int32 TriIdx)
		{
			FIndex3i Tri = Hull[TriIdx];
			double Side = FMath::Sign(ExactPredicates::Orient3D((FVector3d)GetPointFunc(Tri.A), (FVector3d)GetPointFunc(Tri.B), (FVector3d)GetPointFunc(Tri.C), Pt));
			PtSides[TriIdx] = Side > 0 ? 1 : (Side < 0 ? -1 : 0);
		}, bSingleThread);

	TSet<FIndex2i> TerminatorEdges; // set of 'terminator' edges -- edges that we see only once among the removed triangles -- to be connected to the new vertex
	for (int TriIdx = 0; TriIdx < Hull.Num(); TriIdx++)
	{
		int PtSide = PtSides[TriIdx];
		if (PtSide > 0)
		{
			const FIndex3i& Tri = Hull[TriIdx];
			for (int E0 = 2, E1 = 0; E1 < 3; E0 = E1++)
			{
				int V0 = Tri[E0], V1 = Tri[E1];
				FIndex2i Edge(V0, V1);
				// if we've seen this edge before, it would be backwards -- try to remove it
				FIndex2i BackEdge(V1, V0);
				if (TerminatorEdges.Remove(BackEdge) == 0)
				{
					// we hadn't seen it before, so add it as a candidate terminator
					TerminatorEdges.Add(Edge);
				}
			}

			Hull.RemoveAtSwap(TriIdx, 1, false);
			PtSides.RemoveAtSwap(TriIdx, 1, false);
			TriIdx--;
		}
	}

	for (const FIndex2i& Edge : TerminatorEdges)
	{
		Hull.Add(FIndex3i(PtIdx, Edge.A, Edge.B)); // add triangle connecting inserted point to terminator edge
	}
}


template class TConvexHull3<float>;
template class TConvexHull3<double>;

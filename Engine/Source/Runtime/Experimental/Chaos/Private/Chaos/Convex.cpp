// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/Convex.h"

//PRAGMA_DISABLE_OPTIMIZATION

namespace Chaos
{

	int32 FConvex::FindMostOpposingFace(const FVec3& Position, const FVec3& UnitDir, int32 HintFaceIndex, FReal SearchDist) const
	{
		//NOTE: this approach assumes we never have conincident planes, which at the moment is not true.
		//Need to make sure convex hull is cleaned up so that there are n-gon faces so this is unique
		SearchDist = FMath::Max(SearchDist,FMath::Abs(BoundingBox().Extents().GetAbsMax()) * 1e-4f);
		//todo: use hill climbing
		int32 MostOpposingIdx = INDEX_NONE;
		FReal MostOpposingDot = TNumericLimits<FReal>::Max();
		for (int32 Idx = 0; Idx < Planes.Num(); ++Idx)
		{
			const TPlaneConcrete<FReal, 3>& Plane = Planes[Idx];
			const FReal Distance = Plane.SignedDistance(Position);
			if (FMath::Abs(Distance) < SearchDist)
			{
				// TPlane has an override for Normal() that doesn't call PhiWithNormal().
				const FReal Dot = FVec3::DotProduct(Plane.Normal(), UnitDir);
				if (Dot < MostOpposingDot)
				{
					MostOpposingDot = Dot;
					MostOpposingIdx = Idx;
				}
			}
		}
		CHAOS_ENSURE(MostOpposingIdx != INDEX_NONE);
		return MostOpposingIdx;
	}

	int32 FConvex::FindClosestFaceAndVertices(const FVec3& Position, TArray<FVec3>& FaceVertices, FReal SearchDist) const
	{
		//
		//  @todo(chaos) : Collision Manifold 
		//     Create a correspondence between the faces and surface particles on construction.
		//     The correspondence will provide an index between the Planes and the Vertices,
		//     removing the need for the exhaustive search here. 
		//

		int32 ReturnIndex = INDEX_NONE;
		TSet<int32> IncludedParticles;
		for (int32 Idx = 0; Idx < Planes.Num(); ++Idx)
		{
			const TPlaneConcrete<FReal, 3>& Plane = Planes[Idx];
			FReal AbsOfSignedDistance = FMath::Abs(Plane.SignedDistance(Position));
			if (AbsOfSignedDistance < SearchDist)
			{
				for (int32 Fdx = 0; Fdx < (int32)Vertices.Num(); Fdx++)
				{
					if (!IncludedParticles.Contains(Fdx))
					{
						if (FMath::Abs(Plane.SignedDistance(Vertices[Fdx])) < SearchDist)
						{
							FaceVertices.Add(Vertices[Fdx]);
							IncludedParticles.Add(Fdx);
						}
					}
				}
				ReturnIndex = Idx;
			}
		}
		return ReturnIndex;
	}

	int32 FConvex::GetMostOpposingPlane(const FVec3& Normal) const
	{
		// @todo(chaos): this approach assumes we never have conincident planes, which at the moment is not true.
		// Need to make sure convex hull is cleaned up so that there are n-gon faces so this is unique
		// @todo(chaos): use hill climbing
		int32 MostOpposingIdx = INDEX_NONE;
		FReal MostOpposingDot = TNumericLimits<FReal>::Max();
		for (int32 Idx = 0; Idx < Planes.Num(); ++Idx)
		{
			const TPlaneConcrete<FReal, 3>& Plane = Planes[Idx];
			const FReal Dot = FVec3::DotProduct(Plane.Normal(), Normal);
			if (Dot < MostOpposingDot)
			{
				MostOpposingDot = Dot;
				MostOpposingIdx = Idx;
			}
		}
		CHAOS_ENSURE(MostOpposingIdx != INDEX_NONE);
		return MostOpposingIdx;
	}

	int32 FConvex::GetMostOpposingPlaneWithVertex(int32 VertexIndex, const FVec3& Normal) const
	{
		const TArrayView<const int32> VertexPlaneIndices = GetVertexPlanes(VertexIndex);

		if ((VertexIndex == INDEX_NONE) || (VertexPlaneIndices.Num() == 0))
		{
			return GetMostOpposingPlane(Normal);
		}

		int32 MostOpposingIdx = INDEX_NONE;
		FReal MostOpposingDot = TNumericLimits<FReal>::Max();
		for (int32 VertexPlaneIndex = 0; VertexPlaneIndex < VertexPlaneIndices.Num(); ++VertexPlaneIndex)
		{
			const int32 PlaneIndex = VertexPlaneIndices[VertexPlaneIndex];
			const TPlaneConcrete<FReal, 3>& Plane = Planes[PlaneIndex];
			const FReal Dot = FVec3::DotProduct(Plane.Normal(), Normal);
			if (Dot < MostOpposingDot)
			{
				MostOpposingDot = Dot;
				MostOpposingIdx = PlaneIndex;
			}
		}
		CHAOS_ENSURE(MostOpposingIdx != INDEX_NONE);
		return MostOpposingIdx;
	}

	FVec3 FConvex::GetClosestEdgePosition(int32 PlaneIndex, const FVec3& Position) const
	{
		FVec3 ClosestEdgePosition = FVec3(0);
		FReal ClosestDistanceSq = FLT_MAX;

		const TArrayView<const int32> PlaneVertexIndices = GetPlaneVertices(PlaneIndex);
		if (PlaneVertexIndices.Num() > 0)
		{
			FVec3 P0 = GetVertex(PlaneVertexIndices[PlaneVertexIndices.Num() - 1]);
			for (int32 PlaneVertexIndex = 0; PlaneVertexIndex < PlaneVertexIndices.Num(); ++PlaneVertexIndex)
			{
				const int32 VertexIndex = PlaneVertexIndices[PlaneVertexIndex];
				const FVec3 P1 = GetVertex(VertexIndex);
				
				const FVec3 EdgePosition = FMath::ClosestPointOnLine(P0, P1, Position);
				const FReal EdgeDistanceSq = (EdgePosition - Position).SizeSquared();

				if (EdgeDistanceSq < ClosestDistanceSq)
				{
					ClosestDistanceSq = EdgeDistanceSq;
					ClosestEdgePosition = EdgePosition;
				}

				P0 = P1;
			}
		}

		return ClosestEdgePosition;
	}

	TArrayView<const int32> FConvex::GetVertexPlanes(int32 VertexIndex) const
	{
		if (StructureData.IsValid())
		{
			return StructureData.GetVertexPlanes(VertexIndex);
		}

		static TArray<const int32> EmptyPlanes;
		return MakeArrayView(EmptyPlanes);
	}

	TArrayView<const int32> FConvex::GetPlaneVertices(int32 FaceIndex) const
	{
		if (StructureData.IsValid())
		{
			return StructureData.GetPlaneVertices(FaceIndex);
		}

		static TArray<const int32> EmptyVertices;
		return MakeArrayView(EmptyVertices);
	}

	// Store the structure data with the convex. This is used by manifold generation, for example
	void FConvex::CreateStructureData(TArray<TArray<int32>>&& PlaneVertexIndices)
	{
		StructureData.SetPlaneVertices(MoveTemp(PlaneVertexIndices), Vertices.Num());
	}

	void FConvex::MovePlanesAndRebuild(const FReal InDelta)
	{
		TArray<FPlane> NewPlanes;
		TArray<FVec3> NewPoints;
		TArray<TArray<int32>> NewPointPlanes;
		NewPlanes.Reserve(Planes.Num());

		// Move all the planes inwards
		for (int32 PlaneIndex = 0; PlaneIndex < Planes.Num(); ++PlaneIndex)
		{
			const FPlane NewPlane = FPlane(Planes[PlaneIndex].X() + InDelta * Planes[PlaneIndex].Normal(), Planes[PlaneIndex].Normal());
			NewPlanes.Add(NewPlane);
		}

		// Recalculate the set of points from the intersection of all combinations of 3 planes
		// There will be NC3 of these (N! / (3! * (N-3)!)
		const FReal PointTolerance = 1e-2f;
		for (int32 PlaneIndex0 = 0; PlaneIndex0 < NewPlanes.Num(); ++PlaneIndex0)
		{
			for (int32 PlaneIndex1 = PlaneIndex0 + 1; PlaneIndex1 < NewPlanes.Num(); ++PlaneIndex1)
			{
				for (int32 PlaneIndex2 = PlaneIndex1 + 1; PlaneIndex2 < NewPlanes.Num(); ++PlaneIndex2)
				{
					FVec3 PlanesPos;
					if (FMath::IntersectPlanes3(PlanesPos, NewPlanes[PlaneIndex0], NewPlanes[PlaneIndex1], NewPlanes[PlaneIndex2]))
					{
						// Reject duplicate points
						int32 NewPointIndex = INDEX_NONE;
						for (int32 PointIndex = 0; PointIndex < NewPoints.Num(); ++PointIndex)
						{
							if ((PlanesPos - NewPoints[PointIndex]).SizeSquared() < PointTolerance * PointTolerance)
							{
								NewPointIndex = PointIndex;
								break;
							}
						}
						if (NewPointIndex == INDEX_NONE)
						{
							NewPointIndex = NewPoints.Add(PlanesPos);
							NewPointPlanes.AddDefaulted();
						}

						// Keep track of planes that contribute to the point
						NewPointPlanes[NewPointIndex].AddUnique(PlaneIndex0);
						NewPointPlanes[NewPointIndex].AddUnique(PlaneIndex1);
						NewPointPlanes[NewPointIndex].AddUnique(PlaneIndex2);
					}
				}
			}
		}

		// Reject points outside the planes
		const FReal PointPlaneTolerance = PointTolerance;
		for (int32 PointIndex = 0; PointIndex < NewPoints.Num(); ++PointIndex)
		{
			for (int32 PlaneIndex = 0; PlaneIndex < NewPlanes.Num(); ++PlaneIndex)
			{
				const FReal PointPlaneDistance = NewPlanes[PlaneIndex].PlaneDot(NewPoints[PointIndex]);
				if (PointPlaneDistance > PointPlaneTolerance)
				{
					NewPoints.RemoveAtSwap(PointIndex);
					NewPointPlanes.RemoveAtSwap(PointIndex);
					--PointIndex;
					break;
				}
			}
		}

		// Reject planes that are not used
		TArray<bool> NewPlaneUsed;
		NewPlaneUsed.SetNumZeroed(NewPlanes.Num());
		for (int32 PointIndex = 0; PointIndex < NewPoints.Num(); ++PointIndex)
		{
			for (int32 PointPlaneIndex = 0; PointPlaneIndex < NewPointPlanes[PointIndex].Num(); ++PointPlaneIndex)
			{
				const int32 PlaneIndex = NewPointPlanes[PointIndex][PointPlaneIndex];
				NewPlaneUsed[PlaneIndex] = true;
			}
		}

		for (int32 PlaneIndex = 0; PlaneIndex < NewPlanes.Num(); ++PlaneIndex)
		{
			if (!NewPlaneUsed[PlaneIndex])
			{
				NewPlanes.RemoveAtSwap(PlaneIndex);
				NewPlaneUsed.RemoveAtSwap(PlaneIndex);
				--PlaneIndex;
			}
		}

		// Use the new planes
		Planes.Reset(NewPlanes.Num());
		for (int32 PlaneIndex = 0; PlaneIndex < NewPlanes.Num(); ++PlaneIndex)
		{
			Planes.Emplace(TPlaneConcrete<FReal, 3>(NewPlanes[PlaneIndex].GetOrigin(), NewPlanes[PlaneIndex].GetNormal()));
		}


		// Use the new surface points
		Vertices = MoveTemp(NewPoints);
	}
}
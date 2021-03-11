// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/Convex.h"
#include "Chaos/GJK.h"
#include "Chaos/Sphere.h"

//PRAGMA_DISABLE_OPTIMIZATION

namespace Chaos
{
	bool FConvex::Raycast(const FVec3& StartPoint, const FVec3& Dir, const FReal Length, const FReal Thickness, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex) const
	{
		OutFaceIndex = INDEX_NONE;	//finding face is expensive, should be called directly by user
		const FRigidTransform3 StartTM(StartPoint, FRotation3::FromIdentity());
		const TSphere<FReal, 3> Sphere(FVec3(0), Thickness);
		return GJKRaycast(*this, Sphere, StartTM, Dir, Length, OutTime, OutPosition, OutNormal);
	}


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
		const int32 VertexPlaneNum = NumVertexPlanes(VertexIndex);
		if ((VertexIndex == INDEX_NONE) || (VertexPlaneNum == 0))
		{
			return GetMostOpposingPlane(Normal);
		}

		int32 MostOpposingIdx = INDEX_NONE;
		FReal MostOpposingDot = TNumericLimits<FReal>::Max();
		for (int32 VertexPlaneIndex = 0; VertexPlaneIndex < VertexPlaneNum; ++VertexPlaneIndex)
		{
			const int32 PlaneIndex = GetVertexPlane(VertexIndex, VertexPlaneIndex);
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

		const int32 PlaneVerticesNum = NumPlaneVertices(PlaneIndex);
		if (PlaneVerticesNum > 0)
		{
			FVec3 P0 = GetVertex(GetPlaneVertex(PlaneIndex, PlaneVerticesNum - 1));
			for (int32 PlaneVertexIndex = 0; PlaneVertexIndex < PlaneVerticesNum; ++PlaneVertexIndex)
			{
				const int32 VertexIndex = GetPlaneVertex(PlaneIndex, PlaneVertexIndex);
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

	bool FConvex::GetClosestEdgeVertices(int32 PlaneIndex, const FVec3& Position, int32& OutVertexIndex0, int32& OutVertexIndex1) const
	{
		OutVertexIndex0 = INDEX_NONE;
		OutVertexIndex1 = INDEX_NONE;

		FReal ClosestDistanceSq = FLT_MAX;
		const int32 PlaneVerticesNum = NumPlaneVertices(PlaneIndex);
		if (PlaneVerticesNum > 0)
		{
			int32 VertexIndex0 = GetPlaneVertex(PlaneIndex, PlaneVerticesNum - 1);
			FVec3 P0 = GetVertex(VertexIndex0);

			for (int32 PlaneVertexIndex = 0; PlaneVertexIndex < PlaneVerticesNum; ++PlaneVertexIndex)
			{
				const int32 VertexIndex1 = GetPlaneVertex(PlaneIndex, PlaneVertexIndex);
				const FVec3 P1 = GetVertex(VertexIndex1);

				const FVec3 EdgePosition = FMath::ClosestPointOnLine(P0, P1, Position);
				const FReal EdgeDistanceSq = (EdgePosition - Position).SizeSquared();

				if (EdgeDistanceSq < ClosestDistanceSq)
				{
					OutVertexIndex0 = VertexIndex0;
					OutVertexIndex1 = VertexIndex1;
					ClosestDistanceSq = EdgeDistanceSq;
				}

				VertexIndex0 = VertexIndex1;
				P0 = P1;
			}
			return true;
		}
		return false;
	}


	int32 FConvex::NumVertexPlanes(int32 VertexIndex) const
	{
		if (StructureData.IsValid())
		{
			return StructureData.NumVertexPlanes(VertexIndex);
		}
		return 0;
	}

	int32 FConvex::GetVertexPlane(int32 VertexIndex, int32 VertexPlaneIndex) const
	{
		if (StructureData.IsValid())
		{
			return StructureData.GetVertexPlane(VertexIndex, VertexPlaneIndex);
		}
		return INDEX_NONE;
	}

	int32 FConvex::NumPlaneVertices(int32 PlaneIndex) const
	{
		if (StructureData.IsValid())
		{
			return StructureData.NumPlaneVertices(PlaneIndex);
		}
		return 0;
	}

	int32 FConvex::GetPlaneVertex(int32 PlaneIndex, int32 PlaneVertexIndex) const
	{
		if (StructureData.IsValid())
		{
			return StructureData.GetPlaneVertex(PlaneIndex, PlaneVertexIndex);
		}
		return INDEX_NONE;
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
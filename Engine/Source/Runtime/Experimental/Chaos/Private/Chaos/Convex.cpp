// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/Convex.h"

//PRAGMA_DISABLE_OPTIMIZATION

namespace Chaos
{

	// Convex margin type. The margin is required for stable GJK/EPA collision detection
	//	0: no margins
	//	1: external margin. This is the easiest to implement, but the result is that convex shapes grow in size, so there will be gaps between them. Does not work yet - needs to handle scale
	//	2: internal margin: This is the nicest solution as it does not affect overall convex size. Does not work yet - needs to handle scale (but this may be impossible)
	//
	int32 Chaos_Collision_ConvexMarginType = 0;
	FAutoConsoleVariableRef  CVarChaos_Collision_ConvexMarginType(TEXT("p.Chaos.Collision.ConvexMarginType"), Chaos_Collision_ConvexMarginType, TEXT("How the handle margins on convex shapes. 0 - No margin; 1 - External margin; 2 - Internal margin (WIP)"));


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
		//     The correspondence will provide an index between the Planes and the SurfaceParticles,
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
				for (int32 Fdx = 0; Fdx < (int32)SurfaceParticles.Size(); Fdx++)
				{
					if (!IncludedParticles.Contains(Fdx))
					{
						if (FMath::Abs(Plane.SignedDistance(SurfaceParticles.X(Fdx))) < SearchDist)
						{
							FaceVertices.Add(SurfaceParticles.X(Fdx));
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
		if ((VertexIndex == INDEX_NONE) || (StructureData == nullptr))
		{
			return GetMostOpposingPlane(Normal);
		}

		int32 MostOpposingIdx = INDEX_NONE;
		FReal MostOpposingDot = TNumericLimits<FReal>::Max();
		const TArray<int32>& VertexPlaneIndices = StructureData->VertexPlanes[VertexIndex];
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

	TArrayView<int32> FConvex::GetPlaneVertices(int32 FaceIndex) const
	{
		if (StructureData != nullptr)
		{
			return MakeArrayView(StructureData->PlaneVertices[FaceIndex]);
		}

		static TArray<int32> EmptyVertices;
		return MakeArrayView(EmptyVertices);
	}



	// Reduce the core convex shape by the specified margin. For this we need to:
	//	- move all planes in my the margin
	//	- regenerate the set of points from the planes (some points might merge depending on face size and margin size)
	//	- remove any unused planes (if points were removed, some planes are probably no longer contributing)
	//
	// @todo(chaos): optimize memory use
	// @todo(chaos): optimize point rejection
	void FConvex::ApplyMargin(FReal InMargin)
	{
		if (InMargin <= 0.0f)
		{ 
			return;
		}

		// If margins are disabled, do nothing
		if (Chaos_Collision_ConvexMarginType == 0)
		{
			return;
		}

		SetMargin(InMargin);

		// If we are using external margin (i.e., the shape effectively grows by the margin size) we just need to adjust the bounds.
		if (Chaos_Collision_ConvexMarginType == 1)
		{
			LocalBoundingBox.Thicken(GetMargin());
		}

		// If we want a margin without affecting the total size of the shape, we need to recalculate all the planes and points/
		// @todo(chaos): this is not supported except for experimantation because Convex shapes are cooked and reused, but they can be non-uniformly
		// scaled on a per-instance basis, so we really need a runtime solution. This is hard...
		if (Chaos_Collision_ConvexMarginType == 2)
		{
			ShrinkCore(GetMargin());
		}
	}

	void FConvex::ShrinkCore(const FReal InMargin)
	{
		TArray<FPlane> NewPlanes;
		TArray<FVec3> NewPoints;
		TArray<TArray<int32>> NewPointPlanes;
		NewPlanes.Reserve(Planes.Num());

		// Move all the planes inwards
		for (int32 PlaneIndex = 0; PlaneIndex < Planes.Num(); ++PlaneIndex)
		{
			const FPlane NewPlane = FPlane(Planes[PlaneIndex].X() - InMargin * Planes[PlaneIndex].Normal(), Planes[PlaneIndex].Normal());
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
		TParticles<FReal, 3> NewParticles;
		NewParticles.AddParticles(NewPoints.Num());
		for (int32 PointIndex = 0; PointIndex < NewPoints.Num(); ++PointIndex)
		{
			NewParticles.X(PointIndex) = NewPoints[PointIndex];
		}
		SurfaceParticles = MoveTemp(NewParticles);
	}
}
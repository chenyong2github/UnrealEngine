// Copyright Epic Games, Inc. All Rights Reserved.

#include "ClothCollisionPrim.h"

void FClothCollisionPrim_Convex::RebuildSurfacePoints()
{
	const int32 NumPlanes = Planes.Num();
	if (NumPlanes >= 3)
	{
		SurfacePoints.Reset(NumPlanes * (NumPlanes - 1) * (NumPlanes - 2) / 6);  // Maximum number of intersections

		auto PointInHull = [this](const FVector& Point) -> bool
		{
			for (const FPlane& Plane : Planes)
			{
				if (Plane.PlaneDot(Point) > KINDA_SMALL_NUMBER)
				{
					return false;
				}
			}
			return true;
		};

		for (int32 Index0 = 0; Index0 < NumPlanes; ++Index0)
		{
			for (int32 Index1 = Index0 + 1; Index1 < NumPlanes; ++Index1)
			{
				for (int32 Index2 = Index1 + 1; Index2 < NumPlanes; ++Index2)
				{
					FVector Intersection;
					if (FMath::IntersectPlanes3(Intersection, Planes[Index0], Planes[Index1], Planes[Index2]) &&
						PointInHull(Intersection))
					{
						SurfacePoints.Add(Intersection);
					}
				}
			}
		}
	}
	else
	{
		SurfacePoints.Reset();
	}
}

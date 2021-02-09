// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GeometryCollection/ManagedArrayCollection.h"

class FGeometryCollection;

namespace Chaos
{
	class FConvex;
}

class CHAOS_API FGeometryCollectionConvexUtility
{
public:

	struct FGeometryCollectionConvexData
	{
		TManagedArray<int32>& TransformToConvexIndex;
		TManagedArray<TUniquePtr<Chaos::FConvex>>& ConvexHull;
	};

	/** Ensure that convex hull data exists for the Geometry Collection and construct it if not (or if some data is missing. */
	static FGeometryCollectionConvexData GetValidConvexHullData(FGeometryCollection* GeometryCollection);

	/** Returns the convex hull of the vertices contained in the specified geometry. */
	static TUniquePtr<Chaos::FConvex> FindConvexHull(const FGeometryCollection* GeometryCollection, int32 GeometryIndex);

	/** Delete the convex hulls pointed at by the transform indices provided. */
	static void RemoveConvexHulls(FGeometryCollection* GeometryCollection, const TArray<int32>& SortedTransformDeletes);

	/** Set default values for convex hull related managed arrays. */
	static void SetDefaults(FGeometryCollection* GeometryCollection, FName Group, uint32 StartSize, uint32 NumElements);

};


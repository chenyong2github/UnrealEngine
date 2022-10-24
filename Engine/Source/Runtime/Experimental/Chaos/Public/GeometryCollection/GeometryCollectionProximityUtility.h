// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "UObject/ObjectMacros.h"

class FGeometryCollection;
namespace UE::GeometryCollectionConvexUtility
{
	struct FConvexHulls;
}

UENUM()
enum class EProximityMethod : int32
{
	// Precise proximity mode looks for geometry with touching vertices or touching, coplanar, opposite-facing triangles. This works well with geometry fractured using our fracture tools.
	Precise,
	// Convex Hull proximity mode looks for geometry with overlapping convex hulls (with an optional offset)
	ConvexHull
};

class CHAOS_API FGeometryCollectionProximityUtility
{
public:
	FGeometryCollectionProximityUtility(FGeometryCollection* InCollection);

	void UpdateProximity(UE::GeometryCollectionConvexUtility::FConvexHulls* OptionalComputedHulls = nullptr);

	// Update proximity data if it is not already present
	void RequireProximity(UE::GeometryCollectionConvexUtility::FConvexHulls* OptionalComputedHulls = nullptr);

	void InvalidateProximity();

	void CopyProximityToConnectionGraph();
	void ClearConnectionGraph();

private:
	FGeometryCollection* Collection;
};


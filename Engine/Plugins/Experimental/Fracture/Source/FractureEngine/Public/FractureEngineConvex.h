// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/ArrayView.h"

namespace Chaos { class FConvex; }
struct FManagedArrayCollection;

namespace UE::FractureEngine::Convex
{
	// Options to control the simplification of an existing convex hull
	struct FSimplifyHullSettings
	{
		bool bUseGeometricTolerance = true;
		double ErrorTolerance = 5;

		bool bUseTargetTriangleCount = false;
		int32 TargetTriangleCount = 20;

		bool bUseExistingVertexPositions = true;
	};

	/**
	 * Simplify the convex hulls on the given Collection. Optionally only simplify the hulls on the transforms in TransformSelection.
	 * @return true if the collection has convex hulls and the hulls were either simplified or did not need to be
	 */
	bool FRACTUREENGINE_API SimplifyConvexHulls(FManagedArrayCollection& Collection, const FSimplifyHullSettings& Settings, bool bRestrictToSelection = false, const TArrayView<const int32> TransformSelection = TArrayView<const int32>());

	/**
	 * Simplify a convex hull using the given Settings.OutConvexHull can optionally be the same pointer as InConvexHull.
	 * @return true if the hull had valid data and either was simplified or did not need to be (e.g. already had few enough triangles)
	 */
	bool FRACTUREENGINE_API SimplifyConvexHull(const ::Chaos::FConvex* InConvexHull, ::Chaos::FConvex* OutConvexHull, const FSimplifyHullSettings& Settings);

}


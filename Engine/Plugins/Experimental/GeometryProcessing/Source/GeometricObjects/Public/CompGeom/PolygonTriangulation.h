// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "VectorTypes.h"
#include "IndexTypes.h"

namespace PolygonTriangulation
{

	/**
	 * Compute triangulation of simple polygon using ear-clipping
	 * @param VertexPositions ordered vertices of polygon
	 * @param OutTriangles computed triangulation. Each triangle is a tuple of indices into VertexPositions.
	 */
	template<typename RealType>
	void GEOMETRICOBJECTS_API TriangulateSimplePolygon(const TArray<FVector2<RealType>>& VertexPositions, TArray<FIndex3i>& OutTriangles);

}
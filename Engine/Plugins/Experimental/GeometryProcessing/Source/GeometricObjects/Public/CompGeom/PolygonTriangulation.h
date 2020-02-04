// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "VectorTypes.h"
#include "IndexTypes.h"

namespace PolygonTriangulation
{

	/**
	 * Compute triangulation of simple 2D polygon using ear-clipping
	 * @param VertexPositions ordered vertices of 2D polygon
	 * @param OutTriangles computed triangulation. Each triangle is a tuple of indices into VertexPositions.
	 */
	template<typename RealType>
	void GEOMETRICOBJECTS_API TriangulateSimplePolygon(const TArray<FVector2<RealType>>& VertexPositions, TArray<FIndex3i>& OutTriangles);


	template<typename RealType>
	void GEOMETRICOBJECTS_API ComputePolygonPlane(const TArray<FVector3<RealType>>& VertexPositions, FVector3<RealType>& PlaneNormalOut, FVector3<RealType>& PlanePointOut );


	/**
	 * Compute triangulation of 3D simple polygon using ear-clipping
	 * @param VertexPositions ordered vertices of 3D polygon
	 * @param OutTriangles computed triangulation. Each triangle is a tuple of indices into VertexPositions.
	 */
	template<typename RealType>
	void GEOMETRICOBJECTS_API TriangulateSimplePolygon(const TArray<FVector3<RealType>>& VertexPositions, TArray<FIndex3i>& OutTriangles);

}
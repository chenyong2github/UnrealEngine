// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "VectorTypes.h"
#include "IndexTypes.h"

namespace PolygonTriangulation
{
	using namespace UE::Geometry;
	using namespace UE::Math;

	/**
	 * Compute triangulation of simple 2D polygon using ear-clipping
	 * @param VertexPositions ordered vertices of 2D polygon
	 * @param OutTriangles computed triangulation. Each triangle is a tuple of indices into VertexPositions.
	 */
	template<typename RealType>
	void GEOMETRYCORE_API TriangulateSimplePolygon(const TArray<TVector2<RealType>>& VertexPositions, TArray<FIndex3i>& OutTriangles);


	template<typename RealType>
	void GEOMETRYCORE_API ComputePolygonPlane(const TArray<TVector<RealType>>& VertexPositions, TVector<RealType>& PlaneNormalOut, TVector<RealType>& PlanePointOut );


	/**
	 * Compute triangulation of 3D simple polygon using ear-clipping
	 * @param VertexPositions ordered vertices of 3D polygon
	 * @param OutTriangles computed triangulation. Each triangle is a tuple of indices into VertexPositions.
	 */
	template<typename RealType>
	void GEOMETRYCORE_API TriangulateSimplePolygon(const TArray<TVector<RealType>>& VertexPositions, TArray<FIndex3i>& OutTriangles);

}
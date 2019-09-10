// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BoxTypes.h"
#include "IndexTypes.h"
#include "Polygon2.h"
#include "Curve/DynamicGraph2.h"
#include "Curve/GeneralPolygon2.h"

#include "CoreMinimal.h"


template<typename RealType>
struct TConstrainedDelaunay2
{
	//
	// inputs
	//
	TArray<FVector2<RealType>> Vertices;

	// Edges and HoleEdges must not be intersecting; use Arrangment2D to pre-process any input w/ intersecting edges
	TArray<FIndex2i> Edges;	// Edges can be boundaries or not based on the EFillRule setting
	TArray<FIndex2i> HoleEdges; // Any triangles inside 'hole' edges *must* be cut out

	bool bOrientedEdges = true;
	bool bOutputCCW = false;

	enum class EFillRule {
		Odd = 0,
		// bOrientedEdges must be true for the below
		NonZero,
		Positive,
		Negative
	};
	EFillRule FillRule = EFillRule::Odd;

	inline bool ClassifyFromRule(int Winding)
	{
		switch (FillRule)
		{
		case EFillRule::Odd:
			return Winding % 2 != 0;
		case EFillRule::NonZero:
			return Winding != 0;
		case EFillRule::Positive:
			return Winding > 0;
		case EFillRule::Negative:
			return Winding < 0;
		default:
			check(false);
			return false;
		}
	}

	template<class InputRealType>
	void GEOMETRYALGORITHMS_API Add(const FDynamicGraph2<InputRealType>& Graph);
	template<class InputRealType>
	void GEOMETRYALGORITHMS_API Add(const TPolygon2<InputRealType>& Polygon, bool bIsHole = false);
	template<class InputRealType>
	void GEOMETRYALGORITHMS_API Add(const TGeneralPolygon2<InputRealType>& Polygon);

	//
	// outputs
	//
	TArray<FIndex3i> Triangles;

	/**
	 * Populate Triangles
	 *
	 * @return false if Triangulation failed
	 */
	bool GEOMETRYALGORITHMS_API Triangulate();
};

template<typename RealType>
TArray<FIndex3i> GEOMETRYALGORITHMS_API ConstrainedDelaunayTriangulate(const TGeneralPolygon2<RealType>& GeneralPolygon);

typedef TConstrainedDelaunay2<float> FConstrainedDelaunay2f;
typedef TConstrainedDelaunay2<double> FConstrainedDelaunay2d;


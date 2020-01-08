// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MeshShapeGenerator.h"

/**
 * Generate planar triangulation of a Polygon.
 */
class GEOMETRICOBJECTS_API FFlatTriangulationMeshGenerator : public FMeshShapeGenerator
{
public:
	/** Vertices of 2D triangulation. */
	TArray<FVector2d> Vertices2D;

	/** Source triangle indices */
	TArray<FIndex3i> Triangles2D;

	/** Normal vector of all vertices will be set to this value. Default is +Z axis. */
	FVector3f Normal;

	/** How to map 2D indices to 3D. Default is (0,1) = (x,y,0). */
	FIndex2i IndicesMap;

public:
	FFlatTriangulationMeshGenerator();

	/** Generate the triangulation */
	virtual FMeshShapeGenerator& Generate() override;


	/** Create vertex at position under IndicesMap, shifted to Origin*/
	inline FVector3d MakeVertex(double x, double y)
	{
		FVector3d v(0, 0, 0);
		v[IndicesMap.A] = x;
		v[IndicesMap.B] = y;
		return v;
	}
};
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MeshShapeGenerator.h"
#include "FrameTypes.h"

namespace UE
{
namespace Geometry
{

/** Mesh generator that generates a quad for each edge of a closed polygon */
class GEOMETRYCORE_API FPolygonEdgeMeshGenerator : public FMeshShapeGenerator
{

private:

	// Polygon to triangulate. Assumed to be closed (i.e. last edge is (LastVertex, FirstVertex)). If Polygon has 
	// self-intersections or degenerate edges, result is undefined.
	TArray<FFrame3d> Polygon;

	// For each polygon vertex, a scale factor for the patch width at that vertex. Helps keep the width constant
	// going around acute corners. 
	TArray<double> OffsetScaleFactors;

	// Width of quads to generate.
	double Width = 1.0;

	// Normal vector of all vertices will be set to this value. Default is +Z axis.
	FVector3d Normal = FVector3d::UnitZ();

public:

	double UVWidth = 1.0;
	double UVHeight = 1.0;
	bool bScaleUVByAspectRatio = true;

	/** If true, output mesh has a single polygroup, otherwise each quad gets a separate group */
	bool bSinglePolyGroup = false;

	FPolygonEdgeMeshGenerator(const TArray<FFrame3d>& InPolygon,
		const TArray<double>& InOffsetScaleFactors,
		double InWidth = 1.0,
		FVector3d InNormal = FVector3d::UnitZ());

	// Generate triangulation
	// TODO: Enable more subdivisions along the width and length dimensions if requested
	virtual FMeshShapeGenerator& Generate() final;

};


} // end namespace UE::Geometry
} // end namespace UE

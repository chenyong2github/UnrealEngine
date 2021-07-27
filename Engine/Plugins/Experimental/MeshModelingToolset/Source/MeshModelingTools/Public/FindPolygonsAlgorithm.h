// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"

using UE::Geometry::FDynamicMesh3;

class FFindPolygonsAlgorithm
{
public:

	FFindPolygonsAlgorithm() {}
	FFindPolygonsAlgorithm(FDynamicMesh3* MeshIn);

	FDynamicMesh3* Mesh = nullptr;
	TArray<TArray<int>> FoundPolygons;
	TArray<int> PolygonTags;
	TArray<FVector3d> PolygonNormals;

	// if > 1, groups with a triangle count smaller than this will be merged with a neighbouring group
	int32 MinGroupSize = 0;

	TArray<int> PolygonEdges;

	bool FindPolygonsFromFaceNormals(double DotTolerance = 0.0001);
	bool FindPolygonsFromUVIslands();
	bool FindPolygonsFromConnectedTris();

	enum class EWeightingType
	{
		None,
		NormalDeviation
	};

	/**
	 * Incrementally compute approximate-geodesic-based furthest-point sampling of the mesh until NumPoints
	 * samples have been found, then compute local geodesic patches (eg approximate surface voronoi diagaram).
	 * Optionally weight geodesic-distance computation, which will produce different patch shapes.
	 */
	bool FindPolygonsFromFurthestPointSampling(int32 NumPoints, EWeightingType WeightingType, FVector3d WeightingCoeffs = FVector3d::One());


	bool FindPolygonEdges();


protected:

	void PostProcessPolygons(bool bApplyMerging);
	void OptimizePolygons();

	void SetGroupsFromPolygons();
};
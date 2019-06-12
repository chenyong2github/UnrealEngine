// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"

bool VORONOI_API VoronoiNeighbors(const TArrayView<const FVector> &Sites, TArray<TArray<int>> &Neighbors, bool bExcludeBounds = true, float SquaredDistSkipPtThreshold = KINDA_SMALL_NUMBER);
bool VORONOI_API GetVoronoiEdges(const TArrayView<const FVector> &Sites, const FBox& Bounds, TArray<TTuple<FVector, FVector>> &Edges, TArray<int32> &CellMember, float SquaredDistSkipPtThreshold = KINDA_SMALL_NUMBER);

// All the info you would typically want about a single cell in the Voronoi diagram, in the format that is easiest to compute
struct FVoronoiCellInfo
{
	TArray<FVector> Vertices;
	TArray<int32> Faces;
	TArray<int32> Neighbors;
	TArray<FVector> Normals;
};

// third party library voro++'s container class; stores voronoi sites in a uniform grid accel structure
namespace voro {
	class container;
}

class VORONOI_API FVoronoiDiagram
{
	int32 NumSites;
	TUniquePtr<voro::container> Container;
	FBox Bounds;

public:
	// we typically add extra space to the bounds of the Voronoi diagram, to avoid numerical issues of a Voronoi site being on the boundary
	const static float DefaultBoundingBoxSlack;

	/**
	 * @param Sites							Voronoi sites for the diagram
	 * @param ExtraBoundingSpace			Voronoi diagram will be computed within the bounding box of the sites + this amount of extra space in each dimension
	 * @param SquaredDistSkipPtThreshold	A safety threshold to avoid creating invalid cells: sites that are within this distance of an already-added site will not be added
	 *										(If you know there will be no duplicate sites, can set to zero for faster perf.)
	 */
	FVoronoiDiagram(const TArrayView<const FVector>& Sites, float ExtraBoundingSpace, float SquaredDistSkipPtThreshold = KINDA_SMALL_NUMBER);
	/**
	* @param Sites							Voronoi sites for the diagram
	* @param Bounds							Bounding box within which to compute the Voronoi diagram
	* @param ExtraBoundingSpace				Voronoi diagram will be computed within the input Bounds + this amount of extra space in each dimension
	* @param SquaredDistSkipPtThreshold		A safety threshold to avoid creating invalid cells: sites that are within this distance of an already-added site will not be added.
	*										(If you know there will be no duplicate sites, can set to zero for faster perf.)
	*
	*/
	FVoronoiDiagram(const TArrayView<const FVector>& Sites, const FBox &Bounds, float ExtraBoundingSpace, float SquaredDistSkipPtThreshold = KINDA_SMALL_NUMBER);
	~FVoronoiDiagram();

	int32 Num() const
	{
		return NumSites;
	}

	void AddSites(const TArrayView<const FVector>& Sites, float SquaredDistSkipPtThreshold = 0.0f);

	void ComputeAllCells(TArray<FVoronoiCellInfo> &AllCells);

	/**
	 * Find the id of the Voronoi cell containing the given position (or -1 if position is outside diagram)
	 */
	int32 FindCell(const FVector& Pos);
};



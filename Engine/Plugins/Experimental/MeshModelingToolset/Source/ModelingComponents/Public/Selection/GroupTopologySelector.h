// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GroupTopology.h"
#include "Spatial/GeometrySet3.h"
#include "DynamicMeshAABBTree3.h"
#include "RayTypes.h"

class FToolDataVisualizer;
struct FViewCameraState;

/**
 * FGroupTopologySelection represents a set of selected elements of a FGroupTopology
 */
struct MODELINGCOMPONENTS_API FGroupTopologySelection
{
	TArray<int> SelectedGroupIDs;
	TArray<int> SelectedCornerIDs;
	TArray<int> SelectedEdgeIDs;

	FGroupTopologySelection() { Clear(); }
	void Clear();
};


/**
 * FGroupTopologySelector implements selection behavior for a FGroupTopology mesh.
 * Groups, Group Edges, and Corners can be selected (optionally configurable via UpdateEnableFlags).
 * Internally a FGeometrySet3 is constructed to support ray-hit testing against the edges and corners.
 * 
 * Note that to hit-test against the mesh you have to provide a your own FDynamicMeshAABBTree3.
 * You do this by providing a callback via SetSpatialSource(). The reason for this is that
 * (1) owners of FGroupTopologySelector likely already have a BVTree and (2) if the use case
 * is deformation, we need to make sure the owner has recomputed the BVTree before we call functions 
 * on it. The callback you provide should do that!
 * 
 * DrawSelection() can be used to visualize a selection via line/circle drawing.
 * 
 * @todo optionally have an internal mesh AABBTree that can be used when owner does not provide one?
 */
class MODELINGCOMPONENTS_API FGroupTopologySelector
{
public:

	//
	// Configuration variables
	// 

	/** 
	 * This is the function we use to determine if a point on a corner/edge is close enough
	 * to the hit-test ray to treat as a "hit". By default this is Euclidean distance with
	 * a tolerance of 1.0. You probably need to replace this with your own function.
	 */
	TFunction<bool(const FVector3d&, const FVector3d&)> PointsWithinToleranceTest;

public:
	FGroupTopologySelector();

	/**
	 * Initialize the selector with the given Mesh and Topology.
	 * This does not create the internal data structures, this happens lazily on GetGeometrySet()
	 */
	void Initialize(const FDynamicMesh3* Mesh, const FGroupTopology* Topology);

	/**
	 * Provide a function that will return an AABBTree for the Mesh.
	 * See class comment for why this is necessary.
	 */
	void SetSpatialSource(TFunction<FDynamicMeshAABBTree3*(void)> GetSpatialFunc)
	{
		GetSpatial = GetSpatialFunc;
	}

	/**
	 * Notify the Selector that the mesh has changed. 
	 * @param bTopologyDeformed if this is true, the mesh vertices have been moved so we need to update bounding boxes/etc
	 * @param bTopologyModified if this is true, topology has changed and we need to rebuild spatial data structures from scratch
	 */
	void Invalidate(bool bTopologyDeformed, bool bTopologyModified);

	/**
	 * @return the internal GeometrySet. This does lazy updating of the GeometrySet, so this function may take some time.
	 */
	const FGeometrySet3& GetGeometrySet();

	/**
	 * Configure whether faces, edges, and corners will be returned by hit-tests
	 */
	void UpdateEnableFlags(bool bFaceHits, bool bEdgeHits, bool bCornerHits);

	/**
	 * Find which element was selected for a given ray
	 * @param Ray hit-test ray
	 * @param ResultOut resulting selection. At most one of the Groups/Corners/Edges members will contain one element.
	 * @param SelectedPositionOut The point on the ray nearest to the selected element
	 * @param SelectedNormalOut the normal at that point if ResultOut contains a selected face, otherwise uninitialized
	 */
	bool FindSelectedElement(const FRay3d& Ray, FGroupTopologySelection& ResultOut, 
		FVector3d& SelectedPositionOut, FVector3d& SelectedNormalOut);


	/**
	 * Render the given selection with the default settings of the FToolDataVisualizer.
	 * Selected edges are drawn as lines, and selected corners are drawn as small view-facing circles.
	 * (Currently seleced faces are not draw)
	 */
	void DrawSelection(const FGroupTopologySelection& Selection, FToolDataVisualizer* Renderer, const FViewCameraState* CameraState);


public:
	// internal rendering parameters
	float VisualAngleSnapThreshold = 0.5;


protected:
	const FDynamicMesh3* Mesh = nullptr;
	const FGroupTopology* Topology = nullptr;

	TFunction<FDynamicMeshAABBTree3*(void)> GetSpatial = nullptr;

	bool bGeometryInitialized = false;
	bool bGeometryUpToDate = false;
	FGeometrySet3 GeometrySet;


	bool bEnableFaceHits = true;
	bool bEnableEdgeHits = true;
	bool bEnableCornerHits = true;
};
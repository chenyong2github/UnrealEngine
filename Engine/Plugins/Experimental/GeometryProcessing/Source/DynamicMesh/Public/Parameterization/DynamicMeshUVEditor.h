// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DynamicMesh3.h"
#include "DynamicMeshAttributeSet.h"



struct DYNAMICMESH_API FUVEditResult
{
	TArray<int32> NewUVElements;

};


/**
 * FDynamicMeshUVEditor implements various UV overlay editing operations.
 */
class DYNAMICMESH_API FDynamicMeshUVEditor
{
public:
	/** The mesh we will be editing */
	FDynamicMesh3* Mesh = nullptr;
	/** The UV Overlay we will be editing */
	FDynamicMeshUVOverlay* UVOverlay = nullptr;

	/**
	 * Construct UV Editor for a UV Overlay of the given Mesh.
	 * @param UVLayerIndex index of target UV layer
	 * @param bCreateIfMissing if true, target UV layers up to UVLayerIndex will be created if it is not there. Otherwise UVOverlay will be nullptr and class is incomplete.
	 */
	explicit FDynamicMeshUVEditor(FDynamicMesh3* MeshIn, int32 UVLayerIndex, bool bCreateIfMissing);

	FDynamicMesh3* GetMesh() { return Mesh; }
	const FDynamicMesh3* GetMesh() const { return Mesh; }

	FDynamicMeshUVOverlay* GetOverlay() { return UVOverlay; }
	const FDynamicMeshUVOverlay* GetOverlay() const { return UVOverlay; }

	/**
	 * Create specified UVLayer if it does not exist
	 */
	void CreateUVLayer(int32 UVLayerIndex);

	/**
	 * Create new UV island for each Triangle, by planar projection onto plane of Triangle. No transforms/etc are applied.
	 */
	void SetPerTriangleUVs(const TArray<int32>& Triangles, double ScaleFactor = 1.0, FUVEditResult* Result = nullptr);

	/**
	 * Create new UV island for given Triangles, and set UVs by planar projection to ProjectionFrame. No transforms/etc are applied.
	 */
	void SetPerTriangleUVs(double ScaleFactor = 1.0, FUVEditResult* Result = nullptr);


	/**
	 * Create new UV island for given Triangles, and set UVs by planar projection to ProjectionFrame. No transforms/etc are applied.
	 */
	void SetTriangleUVsFromProjection(const TArray<int32>& Triangles, const FFrame3d& ProjectionFrame, FUVEditResult* Result = nullptr);

	/**
	 * Create new UV island for given Triangles, and set UVs for that island using Discrete Exponential Map.
	 * ExpMap center-point is calculated by finding maximum (Dijkstra-approximated) geodesic distance from border of island.
	 * @warning computes a single ExpMap, so input triangle set must be connected, however this is not verified internally
	 */
	bool SetTriangleUVsFromExpMap(const TArray<int32>& Triangles, FUVEditResult* Result = nullptr);

	/**
	 * Create new UV island for given Triangles, and set UVs for that island using Discrete Natural Conformal Map (equivalent to Least-Squares Conformal Map)
	 * @warning computes a single parameterization, so input triangle set must be connected, however this is not verified internally
	 */
	bool SetTriangleUVsFromFreeBoundaryConformal(const TArray<int32>& Triangles, FUVEditResult* Result = nullptr);
};
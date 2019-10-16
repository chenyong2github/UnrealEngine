// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

// Port of geometry3Sharp RemoveOccludedTriangles

#pragma once

#include "MathUtil.h"
#include "VectorTypes.h"
#include "DynamicMesh3.h"
#include "Spatial/MeshAABBTree3.h"
#include "Spatial/FastWinding.h"

#include "MeshAdapter.h"

#include "Util/ProgressCancel.h"

enum class EOcclusionTriangleSampling
{
	Centroids,
	Vertices,
	VerticesAndCentroids
};

enum class EOcclusionCalculationMode
{
	FastWindingNumber,
	SimpleOcclusionTest
};

/**
 * Remove "occluded" triangles, i.e. triangles on the "inside" of the mesh(es).
 * This is a fuzzy definition, current implementation has a couple of options
 * including a winding number-based version and an ambient-occlusion-ish version,
 * where if face is occluded for all test rays, then we classify it as inside and remove it.
 */
template<typename OccluderTriangleMeshType>
class TRemoveOccludedTriangles
{
public:

	FDynamicMesh3* Mesh;

	TRemoveOccludedTriangles(FDynamicMesh3* Mesh) : Mesh(Mesh)
	{
	}
	virtual ~TRemoveOccludedTriangles() {}

	/**
	 * Remove the occluded triangles, considering the given occluder AABB tree (which may represent more geometry than a single mesh)
	 * See alternative versions below if you would like to not precompute acceleration structures; these are mainly provided so the N-mutual-occluding-objects case can run on each mesh separately without 
	 *
	 * @param LocalToWorld Transform to take the local mesh into the space of the occluders
	 * @param Occluders AABB trees for all occluders to consider
	 * @param FastWindingTrees Precomputed fast winding trees for occluder
	 * @return true on success
	 */
	virtual bool Apply(FTransform3d MeshLocalToOccluderSpace, TMeshAABBTree3<OccluderTriangleMeshType>* Occluder, TFastWindingTree<OccluderTriangleMeshType>* FastWindingTree);

	/**
	 * Remove the occluded triangles
	 *
	 * @param LocalToWorld Transform to take the local mesh into the space of the occluder geometry
	 * @param Occluder AABB tree of occluding geometry
	 * @return true on success
	 */
	virtual bool Apply(FTransform3d MeshLocalToOccluderSpace, TMeshAABBTree3<OccluderTriangleMeshType>* Occluder)
	{
		TFastWindingTree<OccluderTriangleMeshType> FastWindingTree(Occluder, InsideMode == EOcclusionCalculationMode::FastWindingNumber);
		return Apply(MeshLocalToOccluderSpace, Occluder, &FastWindingTree);
	}

	/**
	 * @return EOperationValidationResult::Ok if we can apply operation, or error code if we cannot
	 */
	virtual EOperationValidationResult Validate()
	{
		// TODO: validate input
		return EOperationValidationResult::Ok;
	}

	//
	// Input settings
	//

	// how/where to sample triangles when testing for occlusion

	EOcclusionTriangleSampling TriangleSamplingMethod = EOcclusionTriangleSampling::Vertices;

	// we nudge points out by this amount to try to counteract numerical issues
	double NormalOffset = FMathd::ZeroTolerance;

	// use this as winding isovalue for WindingNumber mode
	double WindingIsoValue = 0.5;


	EOcclusionCalculationMode InsideMode = EOcclusionCalculationMode::FastWindingNumber;

	/** Number of additional ray directions to add to raycast-based occlusion checks, beyond the default +/- major axis directions */
	int AddRandomRays = 0;

	/**
	 * Set this to be able to cancel running operation
	 */
	FProgressCancel* Progress = nullptr;


	//
	// Outputs
	//

	// indices of removed triangles. will be empty if nothing removed
	TArray<int> RemovedT;

	// true if it wanted to remove triangles but the actual remove operation failed
	bool bRemoveFailed = false;


protected:
	/**
	 * if this returns true, abort computation. 
	 */
	virtual bool Cancelled()
	{
		return (Progress == nullptr) ? false : Progress->Cancelled();
	}

};



template class DYNAMICMESH_API TRemoveOccludedTriangles<TIndexMeshArrayAdapter<int, double, FVector3d>>;

template class DYNAMICMESH_API TRemoveOccludedTriangles<FDynamicMesh3>;



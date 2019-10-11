// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

// Port of geometry3Sharp RemoveOccludedTriangles

#pragma once

#include "MathUtil.h"
#include "VectorTypes.h"
#include "DynamicMesh3.h"
#include "DynamicMeshAABBTree3.h"
#include "Spatial/FastWinding.h"

#include "Util/ProgressCancel.h"

/**
 * Remove "occluded" triangles, i.e. triangles on the "inside" of the mesh. 
 * This is a fuzzy definition, current implementation has a couple of options
 * including a winding number-based version and an ambient-occlusion-ish version,
 * where if face is occluded for all test rays, then we classify it as inside and remove it.
 */
class DYNAMICMESH_API FRemoveOccludedTriangles
{
public:

	FDynamicMesh3* Mesh;

	FRemoveOccludedTriangles(FDynamicMesh3* Mesh) : Mesh(Mesh)
	{
	}
	virtual ~FRemoveOccludedTriangles() {}

	/**
	 * Remove the occluded triangles
	 *
	 * @param Spatial optional precomputed AABBTree for the mesh; if null it will be computed as needed
	 * @return true on success
	 */
	virtual bool Apply(FDynamicMeshAABBTree3* Spatial = nullptr);

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
	enum class ETriangleSampling
	{
		PerVertex,
		PerCentroid
	};
	ETriangleSampling TriangleSamplingMethod = ETriangleSampling::PerCentroid;

	// we nudge points out by this amount to try to counteract numerical issues
	double NormalOffset = FMathd::ZeroTolerance;

	// use this as winding isovalue for WindingNumber mode
	double WindingIsoValue = 0.5;

	enum class ECalculationMode
	{
		FastWindingNumber,
		SimpleOcclusionTest
	};
	ECalculationMode InsideMode = ECalculationMode::FastWindingNumber;

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


// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MathUtil.h"
#include "VectorTypes.h"
#include "GeometryTypes.h"
#include "MeshRegionBoundaryLoops.h"


class FDynamicMesh3;
class FDynamicMeshChangeTracker;
class FMeshNormals;

/**
 * FOffsetMeshRegion implements local extrusion/offset of a mesh region. 
 * The selected triangles are separated and then stitched back together, creating
 * an new strip of triangles around their border (s). The offset region is
 * then transformed using the OffsetPositionFunc.
 *
 * Complex input regions are handled, eg it can be multiple disconnected components, donut-shaped, etc
 * 
 * Each quad of the border loop is assigned it's own normal and UVs (ie each is a separate UV-island)
 */
class DYNAMICMESH_API FOffsetMeshRegion
{
public:

	//
	// Inputs
	//

	/** The mesh that we are modifying */
	FDynamicMesh3* Mesh;

	/** The triangle region we are modifying */
	TArray<int32> Triangles;

	/** This function is called to generate the offset vertex position. Default returns (Position + DefaultOffsetDistance * Normal) */
	TFunction<FVector3d(const FVector3d&, const FVector3f&, int)> OffsetPositionFunc;

	/** If no Offset function is set, we will displace by DefaultOffsetDistance*Normal */
	double DefaultOffsetDistance = 1.0;

	/** quads on the stitch loop are planar-projected and scaled by this amount */
	float UVScaleFactor = 1.0f;

	/** If a sub-region of Triangles is a full connected component, offset into a solid instead of leaving a shell*/
	bool bOffsetFullComponentsAsSolids = true;

	/** if offset is "negative" (ie negative distance, inset, etc) and inset is an entire mesh region, needs to  */
	bool bIsPositiveOffset = true;

	/** if true, offset each vertex along each face normal and average result, rather than using estimated vertex normal  */
	bool bUseFaceNormals = false;


	/** If set, change tracker will be updated based on edit */
	TUniquePtr<FDynamicMeshChangeTracker> ChangeTracker;

	//
	// Outputs
	//

	/**
	 * Offset information for a single connected component
	 */
	struct FOffsetInfo
	{
		/** Set of triangles for this region */
		TArray<int32> InitialTriangles;
		/** Initial loops on the mesh */
		TArray<FEdgeLoop> BaseLoops;
		/** Offset loops on the mesh */
		TArray<FEdgeLoop> OffsetLoops;
		/** Groups on offset faces */
		TArray<int32> OffsetGroups;

		/** Lists of triangle-strip "tubes" that connect each loop-pair */
		TArray<TArray<int>> StitchTriangles;
		/** List of group ids / polygon ids on each triangle-strip "tube" */
		TArray<TArray<int>> StitchPolygonIDs;

		/** If true, full region was thickened into a solid */
		bool bIsSolid = false;
	};

	/**
	 * List of offset regions/components
	 */
	TArray<FOffsetInfo> OffsetRegions;

	/**
	 * List of all triangles created/modified by this operation
	 */
	TArray<int32> AllModifiedTriangles;

public:
	FOffsetMeshRegion(FDynamicMesh3* mesh);

	virtual ~FOffsetMeshRegion() {}


	/**
	 * @return EOperationValidationResult::Ok if we can apply operation, or error code if we cannot
	 */
	virtual EOperationValidationResult Validate()
	{
		// @todo calculate MeshBoundaryLoops and make sure it is valid

		// is there any reason we couldn't do this??

		return EOperationValidationResult::Ok;
	}


	/**
	 * Apply the Offset operation to the input mesh.
	 * @return true if the algorithm succeeds
	 */
	virtual bool Apply();


protected:

	virtual bool ApplyOffset(FOffsetInfo& Region, FMeshNormals* UseNormals = nullptr);

	virtual bool ApplySolidExtrude(FOffsetInfo& Region, FMeshNormals* UseNormals = nullptr);
};
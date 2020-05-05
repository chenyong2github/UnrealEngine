// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "MathUtil.h"
#include "VectorTypes.h"
#include "GeometryTypes.h"

#include "DynamicMeshEditor.h"

#include "Spatial/FastWinding.h"
#include "DynamicMeshAABBTree3.h"

#include "Util/ProgressCancel.h"






/**
 * MeshSelfUnion -- perform a "Mesh Boolean" style union of a mesh on itself, resolving any self intersections and welding the new boundaries as needed
 */
class DYNAMICMESH_API FMeshSelfUnion
{
public:

	//
	// Inputs
	//
	
	/** Whether to do additional processing to try to remove degenerate edges */
	bool bCollapseDegenerateEdgesOnCut = true;
	double DegenerateEdgeTol = FMathd::ZeroTolerance * 10;

	/** Tolerance distance for considering a point to be on a vertex or edge, especially during mesh-mesh cutting */
	double SnapTolerance = FMathf::ZeroTolerance * 10.0;

	/** Amount we nudge samples off the surface when evaluating winding number, to avoid numerical issues */
	double NormalOffset = FMathd::ZeroTolerance;

	/** Threshold to determine whether triangle in one mesh is inside or outside of the other */
	double WindingThreshold = .5;


	/** Set this to be able to cancel running operation */
	FProgressCancel* Progress = nullptr;


	//
	// Input & Output (to be modified by algorithm)
	//

	// The input mesh, to be modified
	FDynamicMesh3* Mesh;

	//
	// Output
	//

	/** Boundary edges created by the mesh boolean algorithm failing to cleanly weld (doesn't include boundaries that already existed in source mesh) */
	TArray<int> CreatedBoundaryEdges;

public:

	FMeshSelfUnion(FDynamicMesh3* MeshIn)
		: Mesh(MeshIn)
	{
		check(MeshIn != nullptr);
	}

	virtual ~FMeshSelfUnion()
	{}
	
	/**
	 * @return EOperationValidationResult::Ok if we can apply operation, or error code if we cannot
	 */
	EOperationValidationResult Validate()
	{
		// @todo validate inputs
		return EOperationValidationResult::Ok;
	}

	/**
	 * Compute the plane cut by splitting mesh edges that cross the cut plane, and then deleting any triangles
	 * on the positive side of the cutting plane.
	 * @return true if operation succeeds
	 */
	bool Compute();

protected:
	/** If this returns true, abort computation.  */
	virtual bool Cancelled()
	{
		return (Progress == nullptr) ? false : Progress->Cancelled();
	}

private:

	int FindNearestEdge(const TArray<int>& EIDs, const TArray<int>& BoundaryNbrEdges, FVector3d Pos);

	bool MergeEdges(const TArray<int>& CutBoundaryEdges, const TMap<int, int>& AllVIDMatches);

};

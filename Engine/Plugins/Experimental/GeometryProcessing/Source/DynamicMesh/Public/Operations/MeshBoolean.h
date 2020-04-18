// Copyright Epic Games, Inc. All Rights Reserved.

// Port of geometry3Sharp MeshBoolean

#pragma once

#include "MathUtil.h"
#include "VectorTypes.h"
#include "GeometryTypes.h"

#include "DynamicMeshEditor.h"

#include "Spatial/FastWinding.h"
#include "DynamicMeshAABBTree3.h"

#include "Util/ProgressCancel.h"






/**
 * MeshBoolean -- perform a boolean operation on two input meshes.
 */
class DYNAMICMESH_API FMeshBoolean
{
public:

	//
	// Inputs
	//
	const FDynamicMesh3* Meshes[2];
	const FTransform3d Transforms[2];

	enum class EBooleanOp
	{
		Union,
		Difference,
		Intersect
	};
	EBooleanOp Operation;
	
	/** Whether to do additional processing to try to remove degenerate edges */
	bool bCollapseDegenerateEdgesOnCut = true;
	double DegenerateEdgeTol = FMathd::ZeroTolerance * 10;

	/** Tolerance distance for considering a point to be on a vertex or edge, especially during mesh-mesh cutting */
	double SnapTolerance = FMathf::ZeroTolerance * 10.0;

	/** Threshold to determine whether triangle in one mesh is inside or outside of the other */
	double WindingThreshold = .5;

	/** Set this to be able to cancel running operation */
	FProgressCancel* Progress = nullptr;


	//
	// Input & Output (to be modified by algorithm)
	//

	// An existing mesh, to be filled with the boolean result
	FDynamicMesh3* Result;

	//
	// Output
	//

	/** Transform taking the result mesh back to the original space of the inputs */
	FTransform3d ResultTransform;

	/** Boundary edges created by the mesh boolean algorithm failing to cleanly weld (doesn't include boundaries that already existed in source meshes) */
	TArray<int> CreatedBoundaryEdges;

public:

	/**
	 * Perform a boolean operation to combine two meshes into a provided output mesh.
	 * @param MeshA First mesh to combine
	 * @param TransformA Transform of MeshA
	 * @param MeshB Second mesh to combine
	 * @param TransformB Transform of MeshB
	 * @param OutputMesh Mesh to store output
	 * @param Operation How to combine meshes
	 */
	FMeshBoolean(const FDynamicMesh3* MeshA, const FTransform3d& TransformA, const FDynamicMesh3* MeshB, const FTransform3d& TransformB,
				 FDynamicMesh3* OutputMesh, EBooleanOp Operation)
		: Meshes{ MeshA, MeshB }, Transforms{ TransformA, TransformB }, Operation(Operation), Result(OutputMesh)
	{
		check(MeshA != nullptr && MeshB != nullptr && OutputMesh != nullptr);
	}

	virtual ~FMeshBoolean()
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

	int FindNearestEdge(const FDynamicMesh3& OnMesh, const TArray<int>& EIDs, FVector3d Pos);

	bool MergeEdges(const FMeshIndexMappings& IndexMaps, FDynamicMesh3* CutMesh[2], const TArray<int> CutBoundaryEdges[2], const TMap<int, int>& AllVIDMatches);

};

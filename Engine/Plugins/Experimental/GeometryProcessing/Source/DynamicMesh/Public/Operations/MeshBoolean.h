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
		Intersect,
		TrimInside,
		TrimOutside,
		NewGroupInside,
		NewGroupOutside
	};
	EBooleanOp Operation;
	
	

	/** Tolerance distance for considering a point to be on a vertex or edge, especially during mesh-mesh cutting */
	double SnapTolerance = FMathf::ZeroTolerance * 1.0;

	/** Whether to do additional processing to try to remove degenerate edges */
	bool bCollapseDegenerateEdgesOnCut = true;
	/** Tolerance factor (multiplied by SnapTolerance) for removing short edges created by the cutting process; should be no more than 2 */
	double DegenerateEdgeTolFactor = 1.5;

	/** Threshold to determine whether triangle in one mesh is inside or outside of the other */
	double WindingThreshold = .5;

	/** Put the Result mesh in the same space as the input.  If true, ResultTransform will be the identity transform. */
	bool bPutResultInInputSpace = true;

	/** Weld newly-created cut edges where the input meshes meet.  If false, the input meshes will remain topologically disconnected. */
	bool bWeldSharedEdges = true;

	/** Control whether new edges should be tracked */
	bool bTrackAllNewEdges = false;

	/** Set this to be able to cancel running operation */
	FProgressCancel* Progress = nullptr;

	/** Control whether we attempt to auto-simplify the small planar triangles that the boolean operation tends to generate */
	bool bSimplifyAlongNewEdges = false;
	//
    // Simplification-specific settings (only relevant if bSimplifyAlongNewEdges==true):
	//
	/** Degrees of deviation from coplanar that we will still simplify */
	double SimplificationAngleTolerance = .1;
	/**
	 * If triangle quality (aspect ratio) is worse than this threshold, only simplify in ways that improve quality.  If <= 0, triangle quality is ignored.
	 *  Note: For aspect ratio we use definition: 4*TriArea / (sqrt(3)*MaxEdgeLen^2), ref: https://people.eecs.berkeley.edu/~jrs/papers/elemj.pdf p.53
	 *  Equilateral triangles have value 1; Smaller values -> lower quality
	 */
	double TryToImproveTriQualityThreshold = .5;
	/** Prevent simplification from distorting vertex UVs */
	bool bPreserveVertexUVs = true;
	/** Prevent simplification from distorting overlay UVs */
	bool bPreserveOverlayUVs = true;
	/** When preserving UVs, sets maximum allowed change in UV coordinates from collapsing an edge, measured at the removed vertex */
	float UVDistortTolerance = FMathf::ZeroTolerance;
	/** If > -1, then only preserve the UVs of one of the input meshes.  Useful when cutting an artist-created asset w/ procedural geometry. */
	int PreserveUVsOnlyForMesh = -1;



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

	/** All edges created by mesh boolean algorithm. Only populated if bTrackAllNewEdges = true */
	TSet<int32> AllNewEdges;

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

	FMeshBoolean(const FDynamicMesh3* MeshA, const FDynamicMesh3* MeshB, FDynamicMesh3* OutputMesh, EBooleanOp Operation)
		: Meshes{ MeshA, MeshB }, Transforms{ FTransform3d::Identity(), FTransform3d::Identity() }, Operation(Operation), Result(OutputMesh)
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

	void SimplifyAlongNewEdges(int NumMeshesToProcess, FDynamicMesh3* CutMesh[2], TArray<int> CutBoundaryEdges[2], TMap<int, int>& AllVIDMatches);

public:
	
	// specialized helper functions useful for implementing mesh boolean-like features (shared with the extremely similar FMeshSelfUnion algorithm)

	/**
	 * Test if the triangles connected to a vertex are all coplanar
	 * @param Mesh The mesh to query
	 * @param VID The vertex to query
	 * @param DotTolerance If the dot product of two normals are >= this tolerance, the normals are considered equivalent
	 * @param The normal of the first triangle attached to the vertex.
	 * @return Whether all the triangles were coplanar
	 */
	static bool IsFlat(const FDynamicMesh3& Mesh, int VID, double DotTolerance, FVector3d& OutFirstNormal);

	/**
	 * Test if a given edge collapse would cause a triangle flip or other unacceptable decrease in mesh quality
	 * Specialized for collapsing at flat triangles
	 * TODO: Currently this only detects triangle flips; need to extend it to also detect other mesh quality issues
	 */
	static bool CollapseWouldHurtTriangleQuality(
		const FDynamicMesh3& Mesh, const FVector3d& ExpectNormal,
		int32 RemoveV, const FVector3d& RemoveVPos, int32 KeepV, const FVector3d& KeepVPos,
		double TryToImproveTriQualityThreshold
	);

	/**
	 * Test if a given edge collapse would change the mesh shape or UVs unacceptably
	 */
	static bool CollapseWouldChangeShapeOrUVs(
		const FDynamicMesh3& Mesh, const TSet<int>& CutBoundaryEdgeSet, double DotTolerance, int SourceEID,
		int32 RemoveV, const FVector3d& RemoveVPos, int32 KeepV, const FVector3d& KeepVPos,
		const FVector3d& EdgeDir, bool bPreserveUVsForMesh, bool bPreserveVertexUVs, bool bPreserveOverlayUVs, float UVToleranceSq);
};

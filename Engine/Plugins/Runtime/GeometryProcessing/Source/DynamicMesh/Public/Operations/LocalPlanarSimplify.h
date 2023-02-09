// Copyright Epic Games, Inc. All Rights Reserved.

// Port of geometry3Sharp MeshPlaneCut

#pragma once

#include "MathUtil.h"
#include "VectorTypes.h"
#include "GeometryTypes.h"
#include "DynamicMesh/InfoTypes.h"


namespace UE
{
namespace Geometry
{

class FDynamicMesh3;


//
// Class to support local planar simplification, to reduce excess edges created by plane cuts, edge loop insertions, and similar operations
// (Note a custom version of this logic also exists on the FMeshBoolean class)
//
class DYNAMICMESH_API FLocalPlanarSimplify
{
public:
	// Simplification settings

	/** Degrees of deviation from coplanar that we will still simplify */
	double SimplificationAngleTolerance = .1;
	/**
	 * If triangle quality (aspect ratio) is worse than this threshold, only simplify in ways that improve quality.  If <= 0, triangle quality is ignored.
	 *  Note: For aspect ratio we use definition: 4*TriArea / (sqrt(3)*MaxEdgeLen^2), ref: https://people.eecs.berkeley.edu/~jrs/papers/elemj.pdf p.53
	 *  Equilateral triangles have value 1; Smaller values -> lower quality
	 */
	double TryToImproveTriQualityThreshold = .25;
	/** Prevent simplification from distorting triangle groups */
	bool bPreserveTriangleGroups = true;
	/** Prevent simplification from distorting vertex UVs */
	bool bPreserveVertexUVs = true;
	/** Prevent simplification from distorting overlay UVs */
	bool bPreserveOverlayUVs = true;
	/** When preserving UVs, sets maximum allowed change in UV coordinates from collapsing an edge, measured at the removed vertex */
	float UVDistortTolerance = FMathf::ZeroTolerance;
	/** Prevent simplification from distorting vertex normals */
	bool bPreserveVertexNormals = true;
	/** When preserving normals, sets maximum allowed change in normals from collapsing an edge, measured at the removed vertex in degrees */
	float NormalDistortTolerance = .01f;


	/**
	 * Simplify Mesh along the given edges, and update the edge set accordingly
	 * @param Mesh				Mesh to simplify
	 * @param InOutEdges		Edges to simplify along -- will be updated to remove any edges that have been collapsed by simplification
	 * @param ProcessCollapse	Optional function to be called whenever the simplification collapses an edge
	 */
	void SimplifyAlongEdges(FDynamicMesh3& Mesh, TSet<int32>& InOutEdges, TUniqueFunction<void(const DynamicMeshInfo::FEdgeCollapseInfo&)> ProcessCollapse = nullptr) const;




	//
	// The below helper functions are used by SimplifyAlongEdges, but also the similar customized versions of the same algorithm in FMeshBoolean and FMeshSelfUnion as well
	//

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
	 */
	static bool CollapseWouldHurtTriangleQuality(
		const FDynamicMesh3& Mesh, const FVector3d& ExpectNormal,
		int32 RemoveV, const FVector3d& RemoveVPos, int32 KeepV, const FVector3d& KeepVPos,
		double TryToImproveTriQualityThreshold
	);

	/**
	 * Test if a given edge collapse would change the mesh shape, mesh triangle group shape, or UVs unacceptably
	 */
	static bool CollapseWouldChangeShapeOrUVs(
		const FDynamicMesh3& Mesh, const TSet<int>& CutBoundaryEdgeSet, double DotTolerance, int SourceEID,
		int32 RemoveV, const FVector3d& RemoveVPos, int32 KeepV, const FVector3d& KeepVPos,
		const FVector3d& EdgeDir, bool bPreserveTriangleGroups, bool bPreserveUVsForMesh,
		bool bPreserveVertexUVs, bool bPreserveOverlayUVs, float UVToleranceSq,
		bool bPreserveVertexNormals, float NormalEqualCosThreshold);

};

} // end namespace UE::Geometry
} // end namespace UE
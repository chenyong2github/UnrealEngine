// Copyright Epic Games, Inc. All Rights Reserved.

// Port of geometry3cpp MeshConstraintsUtil

#pragma once

#include "DynamicMesh3.h"
#include "MeshConstraints.h"
#include "DynamicMeshAttributeSet.h"


/**
 * Utility functions for configuring a FMeshConstraints instance
 */
class DYNAMICMESH_API FMeshConstraintsUtil
{
public:

	
	/**
	 * Constrain all attribute seams for all overlays of a mesh
	 * @param Constraints the set of constraints to add to
	 * @param Mesh the mesh to constrain
	 * @param bAllowSplits should we allow constrained edges to be split
	 * @param bAllowSmoothing should we allow constrained vertices to be smoothed
	 */
	static void ConstrainAllSeams(FMeshConstraints& Constraints, const FDynamicMesh3& Mesh, bool bAllowSplits, bool bAllowSmoothing, bool bParallel = true);


	/**
	 * Constrain all attribute seams for all overlays of a mesh, for edges in the edge array
	 * @param Constraints the set of constraints to add to
	 * @param Mesh the mesh to constrain
	 * @param EdgeROI list of edges to try to constrain
	 * @param bAllowSplits should we allow constrained edges to be split
	 * @param bAllowSmoothing should we allow constrained vertices to be smoothed
	 */
	static void ConstrainEdgeROISeams(FMeshConstraints& Constraints, const FDynamicMesh3& Mesh, const TArray<int>& EdgeROI, bool bAllowSplits, bool bAllowSmoothing, bool bParallel = true);


	/**
	 * Constrain attribute seams of the given overlay
	 * @param Constraints the set of constraints to add to
	 * @param Mesh the mesh to constrain
	 * @param Overlay the attribute overlay to find seams in
	 */
	template<typename RealType, int ElementSize>
	static void ConstrainSeams(FMeshConstraints& Constraints, const FDynamicMesh3& Mesh, const TDynamicMeshOverlay<RealType, ElementSize>& Overlay)
	{
		for (int EdgeID : Mesh.EdgeIndicesItr())
		{
			if (Overlay.IsSeamEdge(EdgeID))
			{
				Constraints.SetOrUpdateEdgeConstraint(EdgeID, FEdgeConstraint::FullyConstrained());
				FIndex2i EdgeVerts = Mesh.GetEdgeV(EdgeID);
				Constraints.SetOrUpdateVertexConstraint(EdgeVerts.A, FVertexConstraint::Pinned());
				Constraints.SetOrUpdateVertexConstraint(EdgeVerts.B, FVertexConstraint::Pinned());
			}
		}
	}



	/** 
	 * For all edges, disable flip/split/collapse. For all vertices, pin in current position.
	 * @param Constraints the set of constraints to add to
	 * @param Mesh the mesh to constrain
	 */
	template<typename iter>
	static void FullyConstrainEdges(FMeshConstraints& Constraints, const FDynamicMesh3& Mesh, iter BeginEdges, iter EndEdges)
	{
		while (BeginEdges != EndEdges)
		{
			int EdgeID = *BeginEdges;
			if (Mesh.IsEdge(EdgeID))
			{
				Constraints.SetOrUpdateEdgeConstraint(EdgeID, FEdgeConstraint::FullyConstrained());
				FIndex2i EdgeVerts = Mesh.GetEdgeV(EdgeID);
				Constraints.SetOrUpdateVertexConstraint(EdgeVerts.A, FVertexConstraint::Pinned());
				Constraints.SetOrUpdateVertexConstraint(EdgeVerts.B, FVertexConstraint::Pinned());
			}
			BeginEdges++;
		}
	}



private:
	FMeshConstraintsUtil() = delete;		// this class is not constructible


};
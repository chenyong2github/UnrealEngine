// Copyright Epic Games, Inc. All Rights Reserved.

// Port of geometry3Sharp MeshNormals

#pragma once

#include "DynamicMesh3.h"
#include "DynamicMeshAttributeSet.h"

/**
 * FMeshNormals is a utility class that can calculate and store various types of
 * normal vectors for a FDynamicMesh. 
 */
class DYNAMICMESH_API FMeshNormals
{
protected:
	/** Target Mesh */
	const FDynamicMesh3* Mesh;
	/** Set of computed normals */
	TArray<FVector3d> Normals;

public:
	FMeshNormals()
	{
		Mesh = nullptr;
	}

	FMeshNormals(const FDynamicMesh3* Mesh)
	{
		SetMesh(Mesh);
	}


	void SetMesh(const FDynamicMesh3* MeshIn)
	{
		this->Mesh = MeshIn;
	}

	const TArray<FVector3d>& GetNormals() const { return Normals; }

	FVector3d& operator[](int i) { return Normals[i]; }
	const FVector3d& operator[](int i) const { return Normals[i]; }


	/**
	 * Set the size of the Normals array to Count, and optionally clear all values to (0,0,0)
	 */
	void SetCount(int Count, bool bClearToZero);

	/**
	 * Compute standard per-vertex normals by averaging one-ring face normals
	 */
	void ComputeVertexNormals(bool bWeightByArea = true, bool bWeightByAngle = true)
	{
		Compute_FaceAvg(bWeightByArea, bWeightByAngle);
	}

	/**
	 * Compute per-triangle normals
	 */
	void ComputeTriangleNormals()
	{
		Compute_Triangle();
	}

	/**
	 * Recompute the per-element normals of the given overlay by averaging one-ring face normals
	 * @warning NormalOverlay must be attached to ParentMesh or an exact copy
	 */
	void RecomputeOverlayNormals(const FDynamicMeshNormalOverlay* NormalOverlay, bool bWeightByArea = true, bool bWeightByAngle = true)
	{
		Compute_Overlay_FaceAvg(NormalOverlay, bWeightByArea, bWeightByAngle);
	}


	/**
	 * Copy the current set of normals to the vertex normals of SetMesh
	 * @warning assumes that the computed normals are vertex normals
	 * @param bInvert if true, normals are flipped
	 */
	void CopyToVertexNormals(FDynamicMesh3* SetMesh, bool bInvert = false) const;

	/**
	 * Copy the current set of normals to the NormalOverlay attribute layer
	 * @warning assumes that the computed normals are attribute normals
	 * @param bInvert if true, normals are flipped
	 */
	void CopyToOverlay(FDynamicMeshNormalOverlay* NormalOverlay, bool bInvert = false) const;


	/**
	 * Compute per-vertex normals for the given Mesh
	 * @param bInvert if true, normals are flipped
	 */
	static void QuickComputeVertexNormals(FDynamicMesh3& Mesh, bool bInvert = false);


	/**
	 * @return the vertex normal at vertex VertIdx of Mesh
	 */
	static FVector3d ComputeVertexNormal(const FDynamicMesh3& Mesh, int VertIdx);


	/**
	 * @return the computed overlay normal at an element of the overlay (ie based on normals of triangles connected to this element)
	 */
	static FVector3d ComputeOverlayNormal(const FDynamicMesh3& Mesh, FDynamicMeshNormalOverlay* NormalOverlay, int ElemIdx);

	/**
	 * Initialize the given NormalOverlay with per-vertex normals, ie single overlay element for each mesh vertex.
	 * @param bUseMeshVertexNormalsIfAvailable if true and the parent mesh has per-vertex normals, use them instead of calculating new ones
	 */
	static void InitializeOverlayToPerVertexNormals(FDynamicMeshNormalOverlay* NormalOverlay, bool bUseMeshVertexNormalsIfAvailable = true);


protected:
	/** Compute per-vertex normals using area-weighted averaging of one-ring triangle normals */
	void Compute_FaceAvg_AreaWeighted();
	/** Compute per-vertex normals using a custom combination of area-weighted and angle-weighted averaging of one-ring triangle normals */
	void Compute_FaceAvg(bool bWeightByArea, bool bWeightByAngle);

	/** Compute per-triangle normals */
	void Compute_Triangle();

	/** Recompute the element Normals of the given attribute overlay using area-weighted averaging of one-ring triangle normals */
	void Compute_Overlay_FaceAvg_AreaWeighted(const FDynamicMeshNormalOverlay* NormalOverlay);
	/** Recompute the element Normals of the given attribute overlay using a custom combination of area-weighted and angle-weighted averaging of one-ring triangle normals */
	void Compute_Overlay_FaceAvg(const FDynamicMeshNormalOverlay* NormalOverlay, bool bWeightByArea, bool bWeightByAngle);

	inline FVector3d GetVertexWeightsOnTriangle(int TriID, double TriArea, bool bWeightByArea, bool bWeightByAngle) const
	{
		FVector3d TriNormalWeights = FVector3d::One();
		if (bWeightByAngle)
		{
			TriNormalWeights = Mesh->GetTriInternalAnglesR(TriID); // component-wise multiply by per-vertex internal angles
		}
		if (bWeightByArea)
		{
			TriNormalWeights *= TriArea;
		}
		return TriNormalWeights;
	}

};
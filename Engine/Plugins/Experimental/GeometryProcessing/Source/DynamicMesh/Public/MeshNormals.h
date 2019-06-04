// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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
	void ComputeVertexNormals()
	{
		Compute_FaceAvg_AreaWeighted();
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
	void RecomputeOverlayNormals(FDynamicMeshNormalOverlay* NormalOverlay)
	{
		Compute_Overlay_FaceAvg_AreaWeighted(NormalOverlay);
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
	 * @return the vertex normal at vertex VertIDx of Mesh
	 */
	static FVector3d ComputeVertexNormal(const FDynamicMesh3& Mesh, int VertIdx);


protected:
	/** Compute per-vertex normals using area-weighted averaging of one-ring triangles */
	void Compute_FaceAvg_AreaWeighted();
	/** Compute per-triangle normals */
	void Compute_Triangle();
	/** Recompute the element Normals of the given attribute overlay using area-weighted averaging of one-ring triangles */
	void Compute_Overlay_FaceAvg_AreaWeighted(const FDynamicMeshNormalOverlay* NormalOverlay);

};
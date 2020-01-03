// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DynamicMesh3.h"
#include "DynamicMeshAttributeSet.h"

/**
 * TMeshTangents is a utility class that can calculate and store various types of
 * tangent vectors for a FDynamicMesh.
 */
template<typename RealType>
class DYNAMICMESH_API TMeshTangents
{
protected:
	/** Target Mesh */
	const FDynamicMesh3* Mesh;
	/** Set of computed tangents */
	TArray<FVector3<RealType>> Tangents;
	/** Set of computed bitangents */
	TArray<FVector3<RealType>> Bitangents;

public:
	TMeshTangents()
	{
		Mesh = nullptr;
	}

	TMeshTangents(const FDynamicMesh3* Mesh)
	{
		SetMesh(Mesh);
	}

	void SetMesh(const FDynamicMesh3* MeshIn)
	{
		this->Mesh = MeshIn;
	}

	const TArray<FVector3<RealType>>& GetTangents() const { return Tangents; }

	const TArray<FVector3<RealType>>& GetBitangents() const { return Bitangents; }


	/**
	 * Return tangent and bitangent at a vertex of triangle for per-triangle computed tangents
	 * @param TriangleID triangle index in mesh
	 * @param TriVertIdx vertex index in range 0,1,2
	 */
	void GetPerTriangleTangent(int TriangleID, int TriVertIdx, FVector3<RealType>& TangentOut, FVector3<RealType>& BitangentOut) const
	{
		int k = TriangleID * 3 + TriVertIdx;
		TangentOut = Tangents[k];
		BitangentOut = Bitangents[k];
	}


	/**
	 * Set tangent and bitangent at a vertex of triangle for per-triangle computed tangents.
	 * @param TriangleID triangle index in mesh
	 * @param TriVertIdx vertex index in range 0,1,2
	 */
	void SetPerTriangleTangent(int TriangleID, int TriVertIdx, const FVector3<RealType>& Tangent, const FVector3<RealType>& Bitangent)
	{
		int k = TriangleID * 3 + TriVertIdx;
		Tangents[k] = Tangent;
		Bitangents[k] = Bitangent;
	}


	/**
	 * Calculate per-triangle tangent spaces based on the given per-triangle normal and UV overlays.
	 * In this mode there is no averaging of tangents across triangles. So if we have N triangles
	 * in the mesh, then 3*N tangents are generated. These tangents are computed in parallel.
	 */
	void ComputePerTriangleTangents(const FDynamicMeshNormalOverlay* NormalOverlay, const FDynamicMeshUVOverlay* UVOverlay)
	{
		Internal_ComputePerTriangleTangents(NormalOverlay, UVOverlay);
	}


	/**
	 * Set internal buffer sizes suitable for calculating per-triangle tangents.
	 * This is intended to be used if you wish to calculate your own tangents and use SetPerTriangleTangent()
	 */
	void InitializePerTriangleTangents(bool bClearToZero)
	{
		SetTangentCount(Mesh->MaxTriangleID() * 3, bClearToZero);
	}



protected:
	/**
	 * Set the size of the Tangents array to Count, and optionally clear all values to (0,0,0)
	 */
	void SetTangentCount(int Count, bool bClearToZero);

	// Calculate per-triangle tangents
	void Internal_ComputePerTriangleTangents(const FDynamicMeshNormalOverlay* NormalOverlay, const FDynamicMeshUVOverlay* UVOverlay);
};

typedef TMeshTangents<float> FMeshTangentsf;
typedef TMeshTangents<double> FMeshTangentsd;
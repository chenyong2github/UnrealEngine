// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DynamicMesh3.h"
#include "DynamicMeshAttributeSet.h"


/**
 * Options used by TMeshTangents for tangents computation
 */
struct DYNAMICMESH_API FComputeTangentsOptions
{
	/** if true, per-face tangents are blended to create triange-vertex tangents */
	bool bAveraged = true;		
	/** if true, per-face tangents are weighted by opening angle at triangle-vertices when blending */
	bool bAngleWeighted = true;
};


/**
 * TMeshTangents is a utility class that can calculate and store various types of
 * tangent vectors for a FDynamicMesh.
 */
template<typename RealType>
class TMeshTangents
{
protected:
	/** Target Mesh */
	const FDynamicMesh3* Mesh;
	/** Set of computed tangents */
	TArray<FVector3<RealType>> Tangents;
	/** Set of computed bitangents */
	TArray<FVector3<RealType>> Bitangents;
	/** Indices of degenerate triangles. This may be useful to know externally, as those tangets are often meaningless */
	TArray<int32> AllDegenerateTris;

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

	const TArray<int32>& GetDegenerateTris() const { return AllDegenerateTris; }


	//
	// Per-Triangle-Vertex Tangents
	// Number of Tangents is NumTriangles * 3
	//

	/**
	 * Set internal buffer sizes suitable for calculating per-triangle tangents.
	 * This is intended to be used if you wish to calculate your own tangents and use SetPerTriangleTangent()
	 */
	void InitializeTriVertexTangents(bool bClearToZero)
	{
		SetTangentCount(Mesh->MaxTriangleID() * 3, bClearToZero);
	}

	/**
	 * Initialize Tangents from other Tangents set
	 */
	template<typename OtherRealType>
	void CopyTriVertexTangents(const TMeshTangents<OtherRealType>& OtherTangents);

	/**
	 * Calculate per-triangle tangent spaces based on the given per-triangle normal and UV overlays.
	 * In this mode there is no averaging of tangents across triangles. So if we have N triangles
	 * in the mesh, then 3*N tangents are generated. These tangents are computed in parallel.
	 */
	void ComputeTriVertexTangents(const FDynamicMeshNormalOverlay* NormalOverlay, const FDynamicMeshUVOverlay* UVOverlay, const FComputeTangentsOptions& Options);

	/**
	 * Return tangent and bitangent at a vertex of triangle for per-triangle computed tangents
	 * @param TriangleID triangle index in mesh
	 * @param TriVertIdx vertex index in range 0,1,2
	 */
	void GetPerTriangleTangent(int32 TriangleID, int32 TriVertIdx, FVector3<RealType>& TangentOut, FVector3<RealType>& BitangentOut) const
	{
		int32 k = TriangleID * 3 + TriVertIdx;
		TangentOut = Tangents[k];
		BitangentOut = Bitangents[k];
	}


	/**
	 * Return tangent and bitangent at a vertex of triangle for per-triangle computed tangents
	 * @param TriangleID triangle index in mesh
	 * @param TriVertIdx vertex index in range 0,1,2
	 */
	template<typename VectorType, typename OtherRealType>
	void GetPerTriangleTangent(int32 TriangleID, int32 TriVertIdx, VectorType& TangentOut, VectorType& BitangentOut) const
	{
		int32 k = TriangleID * 3 + TriVertIdx;
		TangentOut.X = (OtherRealType)Tangents[k].X;
		TangentOut.Y = (OtherRealType)Tangents[k].Y;
		TangentOut.Z = (OtherRealType)Tangents[k].Z;
		BitangentOut.X = (OtherRealType)Bitangents[k].X;
		BitangentOut.Y = (OtherRealType)Bitangents[k].Y;
		BitangentOut.Z = (OtherRealType)Bitangents[k].Z;
	}


	/**
	 * @return Interpolated tangent and bitangent at barycentric coordinates inside a triangle, for per-triangle computed tangents
	 * @param TriangleID triangle index in mesh
	 * @param BaryCoords barycentric coordinates in triangle
	 * @param TangentOut interpolated tangent
	 * @param BitangentOut interpolated bitangent
	 */
	void GetInterpolatedTriangleTangent(int32 TriangleID, const FVector3<RealType>& BaryCoords, FVector3<RealType>& TangentOut, FVector3<RealType>& BitangentOut) const
	{
		int32 k = TriangleID * 3;
		if (k >= 0 && (k+2) < Tangents.Num())
		{
			TangentOut = FVector3<RealType>::Blend3(Tangents[k], Tangents[k+1], Tangents[k+2], BaryCoords.X, BaryCoords.Y, BaryCoords.Z);
			TangentOut.Normalize();
			BitangentOut = FVector3<RealType>::Blend3(Bitangents[k], Bitangents[k+1], Bitangents[k+2], BaryCoords.X, BaryCoords.Y, BaryCoords.Z);
			BitangentOut.Normalize();
		}
		else
		{
			TangentOut = FVector3<RealType>::UnitX();
			BitangentOut = FVector3<RealType>::UnitY();
		}
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


	//
	// Per-Triangle Tangents
	// Number of Tangents is NumTriangles
	//

	/** Initialize buffer sizes to one tangent/bitangent per mesh triangle */
	void InitializeTriangleTangents(bool bClearToZero)
	{
		SetTangentCount(Mesh->MaxTriangleID(), bClearToZero);
	}

	/**
	 * Compute per-triangle tangents for the given UV Overlay 
	 * @param bOrthogonalize if true, we orthogonalize the tangents by computing the Bitangent sign and regenerating it. Otherwise the tangents are left non-orthogonal.
	 */
	void ComputeTriangleTangents(const FDynamicMeshUVOverlay* UVOverlay, bool bOrthogonalize = true);


	/**
	 * Set Tangents on mesh overlays
	 * @param MeshToSet Mesh to copy overlays to; does not need to be the same as the Mesh member of this class
	 */
	bool CopyToOverlays(FDynamicMesh3& MeshToSet);

protected:
	/**
	 * Set the size of the Tangents array to Count, and optionally clear all values to (0,0,0)
	 */
	void SetTangentCount(int Count, bool bClearToZero);

	// calculate per-triangle tangents and then projected to overlay normals at each triangle-vertex
	void ComputeSeparatePerTriangleTangents(const FDynamicMeshNormalOverlay* NormalOverlay, const FDynamicMeshUVOverlay* UVOverlay, const FComputeTangentsOptions& Options);

	// Calculate MikkT-space-style tangents
	void ComputeMikkTStyleTangents(const FDynamicMeshNormalOverlay* NormalOverlay, const FDynamicMeshUVOverlay* UVOverlay, const FComputeTangentsOptions& Options);
};

typedef TMeshTangents<float> FMeshTangentsf;
typedef TMeshTangents<double> FMeshTangentsd;




template<typename RealType>
template<typename OtherRealType>
void TMeshTangents<RealType>::CopyTriVertexTangents(const TMeshTangents<OtherRealType>& OtherMeshTangents)
{
	const TArray<FVector3<OtherRealType>>& OtherTangents = OtherMeshTangents.GetTangents();
	const TArray<FVector3<OtherRealType>>& OtherBitangents = OtherMeshTangents.GetBitangents();

	int32 Num = OtherTangents.Num();
	check(Mesh->MaxTriangleID() * 3 == Num);
	InitializeTriVertexTangents(false);
	for (int32 k = 0; k < Num; ++k)
	{
		Tangents[k] = FVector3<RealType>(OtherTangents[k]);
		Bitangents[k] = FVector3<RealType>(OtherBitangents[k]);
	}
	AllDegenerateTris = OtherMeshTangents.GetDegenerateTris();
}
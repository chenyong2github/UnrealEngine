// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FDynamicMesh3;
class UMaterialInterface;
struct FComponentMaterialSet;

/**
 * FMeshRenderDecomposition represents a decomposition of a mesh into "chunks" of triangles, with associated materials.
 * This is passed to the rendering components to split a mesh into multiple RenderBuffers, for more efficient updating.
 */
class MODELINGCOMPONENTS_API FMeshRenderDecomposition
{
public:

	// Movable
	FMeshRenderDecomposition(FMeshRenderDecomposition&&) = default;
	FMeshRenderDecomposition& operator=(FMeshRenderDecomposition&&) = default;
	// TArray<TUniquePtr> member cannot be default-constructed (in this case we just make it NonCopyable)
	FMeshRenderDecomposition() = default;
	FMeshRenderDecomposition(const FMeshRenderDecomposition&) = delete;
	FMeshRenderDecomposition& operator=(const FMeshRenderDecomposition&) = delete;

	struct FGroup
	{
		TArray<int32> Triangles;
		UMaterialInterface* Material;
	};
	TArray<TUniquePtr<FGroup>> Groups;


	/** Mapping from TriangleID to Groups array index. Initialized by BuildAssociations() */
	TArray<int32> TriangleToGroupMap;

	void Initialize(int32 Count)
	{
		Groups.SetNum(Count);
		for (int32 k = 0; k < Count; ++k)
		{
			Groups[k] = MakeUnique<FGroup>();
		}
	}

	int32 AppendGroup()
	{
		int32 N = Groups.Num();
		Groups.SetNum(N + 1);
		Groups[N] = MakeUnique<FGroup>();
		return N;
	}

	int32 Num() const
	{
		return Groups.Num();
	}

	FGroup& GetGroup(int32 Index)
	{
		return *Groups[Index];
	}

	const FGroup& GetGroup(int32 Index) const
	{
		return *Groups[Index];
	}

	int32 GetGroupForTriangle(int32 TriangleID) const
	{
		return TriangleToGroupMap[TriangleID];
	}

	/**
	 * Construct mappings between mesh and groups (eg TriangleToGroupMap)
	 */
	void BuildAssociations(const FDynamicMesh3* Mesh);


	/**
	 * Build decomposition with one group for each MaterialID of mesh
	 */
	static void BuildMaterialDecomposition(FDynamicMesh3* Mesh, const FComponentMaterialSet* MaterialSet, FMeshRenderDecomposition& Decomp);

	/**
	 * Build per-material decomposition, and then split each of those into chunks of at most MaxChunkSize
	 * (actual chunk sizes will be highly variable and some may be very small...)
	 */
	static void BuildChunkedDecomposition(FDynamicMesh3* Mesh, const FComponentMaterialSet* MaterialSet, FMeshRenderDecomposition& Decomp, int32 MaxChunkSize = 1 << 14 /* 16k */ );
};

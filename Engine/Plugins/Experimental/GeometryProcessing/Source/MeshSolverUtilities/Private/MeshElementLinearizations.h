// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh3.h"


/**
* Used linearize the VtxIds in a mesh as a single array and allow mapping from array offset to mesh VtxId.
* Generally, the array offset will correspond to a matrix row when forming a Laplacian
*/
class FMeshElementLinearization
{
public:

	FMeshElementLinearization() = default;

	// Lookup   ToVtxId(Index) = VtxId;
	const TArray<int32>& ToId() const { return ToIdMap; }

	// Lookup   ToIndex(VtxId) = Index;  may return FDynamicMesh3::InvalidID 
	const TArray<int32>& ToIndex() const { return ToIndexMap; }

	int32 NumIds() const { return ToIdMap.Num(); }

	// Following the FDynamicMesh3 convention this is really MaxId + 1
	int32 MaxId() const { return ToIndexMap.Num(); }

	void Empty() { ToIdMap.Empty();  ToIndexMap.Empty(); }

	void Populate(const int32 MaxId, const int32 Count, FRefCountVector::IndexEnumerable ElementItr)
	{
		ToIndexMap.SetNumUninitialized(MaxId);
		ToIdMap.SetNumUninitialized(Count);

		for (int32 i = 0; i < MaxId; ++i)
		{
			ToIndexMap[i] = FDynamicMesh3::InvalidID;
		}

		// create the mapping
		{
			int32 N = 0;
			for (int32 Id : ElementItr)
			{
				ToIdMap[N] = Id;
				ToIndexMap[Id] = N;
				N++;
			}
		}
	}

protected:
	TArray<int32>  ToIdMap;
	TArray<int32>  ToIndexMap;

private:
	FMeshElementLinearization(const FMeshElementLinearization&);
};


/**
* Used linearize the VtxIds in a mesh as a single array and allow mapping from array offset to mesh VtxId.
* Generally, the array offset will correspond to a matrix row when forming a Laplacian
*/
class FVertexLinearization : public FMeshElementLinearization
{
public:
	FVertexLinearization() = default;
	FVertexLinearization(const FDynamicMesh3& DynamicMesh)
	{
		Reset(DynamicMesh);
	}

	void Reset(const FDynamicMesh3& DynamicMesh)
	{
		Empty();
		FMeshElementLinearization::Populate(DynamicMesh.MaxVertexID(), DynamicMesh.VertexCount(), DynamicMesh.VertexIndicesItr());
	}

	int32 NumVerts() const { return FMeshElementLinearization::NumIds(); }

private:
	FVertexLinearization(const FVertexLinearization&);
};

/**
* Used linearize the TriIds in a mesh as a single array and allow mapping from array offset to mesh TriId.
*
*/
class FTriangleLinearization : public FMeshElementLinearization
{
public:
	FTriangleLinearization() = default;

	FTriangleLinearization(const FDynamicMesh3& DynamicMesh)
	{
		Reset(DynamicMesh);
	}

	void Reset(const FDynamicMesh3& DynamicMesh)
	{
		Empty();
		FMeshElementLinearization::Populate(DynamicMesh.MaxTriangleID(), DynamicMesh.TriangleCount(), DynamicMesh.TriangleIndicesItr());
	}

	int32 NumTris() const { return FMeshElementLinearization::NumIds(); }


private:
	FTriangleLinearization(const FTriangleLinearization&);
};
